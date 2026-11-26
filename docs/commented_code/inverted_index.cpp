#include "inverted_index.hpp"
#include <sstream>
#include <fstream>
#include <cctype>
#include <iomanip>

namespace search {

InvertedIndex::InvertedIndex() : shards_(NUM_SHARDS) {}

void InvertedIndex::clear() {
    for (auto& shard : shards_) {
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        shard.table.clear();
    }
    {
        std::unique_lock<std::shared_mutex> lock(docs_mutex_);
        documents_.clear();
        total_words_all_docs_.store(0);
    }
}

uint32_t InvertedIndex::register_document(const std::string& filename, const std::string& filepath,
                                         uint32_t total_words, const std::string& full_text) {
    std::unique_lock<std::shared_mutex> lock(docs_mutex_);
    uint32_t id = static_cast<uint32_t>(documents_.size());

    // Зберігаємо перші 200 символів як кешований сніпет у пам'яті
    std::string preview = full_text.substr(0, 200);
    std::replace(preview.begin(), preview.end(), '\n', ' ');
    std::replace(preview.begin(), preview.end(), '\r', ' ');
    if (full_text.length() > 200) {
        preview += "...";
    }

    documents_.push_back(DocumentInfo{id, filename, filepath, total_words, preview});
    total_words_all_docs_.fetch_add(total_words);

    return id;
}

void InvertedIndex::add_document_terms(uint32_t doc_id,
                                       const StringHashMap<uint32_t>& term_counts) {
    // 1. Групуємо всі терміни документа за 64 шардами у локальні кошики потоку
    std::vector<std::vector<std::pair<std::string, uint32_t>>> shard_batches(NUM_SHARDS);
    term_counts.for_each([&](const std::string& term, uint32_t count) {
        if (!term.empty()) {
            shard_batches[get_shard_index(term)].emplace_back(term, count);
        }
    });

    // 2. Блокуємо кожен непустий шард ЛИШЕ ОДИН РАЗ на документ і записуємо у StringHashMap
    for (size_t shard_idx = 0; shard_idx < NUM_SHARDS; ++shard_idx) {
        if (shard_batches[shard_idx].empty()) continue;
        auto& shard = shards_[shard_idx];
        std::unique_lock<std::shared_mutex> lock(shard.mutex);
        for (const auto& [term, count] : shard_batches[shard_idx]) {
            shard.table.get_or_insert(term).push_back(Posting{doc_id, count});
        }
    }
}

std::vector<SearchResult> InvertedIndex::search(const std::vector<std::string>& query_terms, size_t limit) const {
    if (query_terms.empty()) {
        return {};
    }

    // Захоплюємо shared_lock на метадані 1 раз на весь запит (читачі не блокують один одного!)
    std::shared_lock<std::shared_mutex> docs_lock(docs_mutex_);
    size_t total_docs = documents_.size();
    if (total_docs == 0) {
        return {};
    }

    double avgdl = static_cast<double>(total_words_all_docs_.load()) / static_cast<double>(total_docs);
    if (avgdl <= 0.0) avgdl = 1.0;

    const double k1 = 1.2;
    const double b = 0.75;

    // Використовуємо пласкі масиви за індексом doc_id замість std::unordered_map:
    // прямий доступ O(1) без колізій і алокацій вузлів у купі!
    std::vector<double> doc_scores(total_docs, 0.0);
    std::vector<uint8_t> doc_seen(total_docs, 0);
    std::vector<uint32_t> matched_docs;
    matched_docs.reserve(256);

    for (const auto& raw_term : query_terms) {
        std::string term = raw_term;
        std::transform(term.begin(), term.end(), term.begin(), [](unsigned char c) { return std::tolower(c); });

        size_t shard_idx = get_shard_index(term);
        const auto& shard = shards_[shard_idx];

        // Копіюємо posting-список із нашої StringHashMap під коротким shared_lock
        std::vector<Posting> postings;
        {
            std::shared_lock<std::shared_mutex> lock(shard.mutex);
            const auto* found = shard.table.find(term);
            if (found != nullptr) {
                postings = *found;
            }
        }

        if (postings.empty()) continue;

        size_t doc_freq = postings.size();
        double idf = std::log(1.0 + (static_cast<double>(total_docs) - static_cast<double>(doc_freq) + 0.5) /
                                    (static_cast<double>(doc_freq) + 0.5));
        if (idf < 0.0) idf = 0.01;

        for (const auto& posting : postings) {
            uint32_t doc_id = posting.doc_id;
            if (doc_id >= total_docs) continue;
            uint32_t tf = posting.term_frequency;

            uint32_t doc_len = documents_[doc_id].total_words;
            if (doc_len == 0) doc_len = static_cast<uint32_t>(avgdl);

            double numerator = static_cast<double>(tf) * (k1 + 1.0);
            double denominator = static_cast<double>(tf) + k1 * (1.0 - b + b * (static_cast<double>(doc_len) / avgdl));
            double term_score = idf * (numerator / denominator);

            if (!doc_seen[doc_id]) {
                doc_seen[doc_id] = 1;
                matched_docs.push_back(doc_id);
            }
            doc_scores[doc_id] += term_score;
        }
    }

    if (matched_docs.empty()) {
        return {};
    }

    // Часткове сортування (std::partial_sort) знаходить топ-20 швидше, ніж повне сортування всіх збігів
    std::vector<std::pair<uint32_t, double>> sorted_results;
    sorted_results.reserve(matched_docs.size());
    for (uint32_t doc_id : matched_docs) {
        sorted_results.emplace_back(doc_id, doc_scores[doc_id]);
    }

    size_t top_k = std::min(limit, sorted_results.size());
    std::partial_sort(sorted_results.begin(), sorted_results.begin() + top_k, sorted_results.end(),
                      [](const auto& a, const auto& b) {
                          return a.second > b.second;
                      });
    sorted_results.resize(top_k);

    double max_score = sorted_results.front().second;
    if (max_score <= 0.0) max_score = 1.0;

    std::vector<SearchResult> results;
    results.reserve(sorted_results.size());

    for (const auto& [doc_id, raw_score] : sorted_results) {
        std::string filename = documents_[doc_id].filename;
        std::string snippet = documents_[doc_id].snippet;
        if (filename.empty()) {
            filename = "doc_" + std::to_string(doc_id);
        }

        double normalized_score = std::round((raw_score / max_score) * 100.0) / 100.0;
        if (normalized_score <= 0.0) normalized_score = 0.01;

        results.push_back(SearchResult{filename, normalized_score, snippet});
    }

    return results;
}

size_t InvertedIndex::get_document_count() const {
    std::shared_lock<std::shared_mutex> lock(docs_mutex_);
    return documents_.size();
}

double InvertedIndex::get_avg_doc_length() const {
    size_t count = get_document_count();
    if (count == 0) return 0.0;
    return static_cast<double>(total_words_all_docs_.load()) / static_cast<double>(count);
}

std::vector<std::string> InvertedIndex::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    tokens.reserve(text.size() / 6);
    std::string current_word;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current_word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if ((c == '\'' || c == '-') && !current_word.empty() &&
                   i + 1 < text.size() && std::isalnum(static_cast<unsigned char>(text[i + 1]))) {
            current_word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!current_word.empty()) {
            if (current_word.length() >= 2 && !is_stop_word(current_word)) {
                tokens.push_back(std::move(current_word));
            }
            current_word.clear();
        }
    }

    if (!current_word.empty()) {
        if (current_word.length() >= 2 && !is_stop_word(current_word)) {
            tokens.push_back(std::move(current_word));
        }
    }

    return tokens;
}

void InvertedIndex::count_tokens(const std::string& text,
                                 StringHashMap<uint32_t>& term_counts,
                                 uint32_t& total_words) {
    // Прямий підрахунок частот слів у нашу власну StringHashMap без проміжного std::vector<std::string>
    std::string current_word;
    current_word.reserve(32);
    total_words = 0;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (std::isalnum(static_cast<unsigned char>(c))) {
            current_word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if ((c == '\'' || c == '-') && !current_word.empty() &&
                   i + 1 < text.size() && std::isalnum(static_cast<unsigned char>(text[i + 1]))) {
            current_word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!current_word.empty()) {
            if (current_word.length() >= 2 && !is_stop_word(current_word)) {
                ++term_counts.get_or_insert(current_word);
                ++total_words;
            }
            current_word.clear();
        }
    }

    if (!current_word.empty()) {
        if (current_word.length() >= 2 && !is_stop_word(current_word)) {
            ++term_counts.get_or_insert(current_word);
            ++total_words;
        }
    }
}

std::string InvertedIndex::generate_snippet(uint32_t doc_id, const std::vector<std::string>& query_terms) const {
    std::string filepath;
    std::string default_snippet;
    {
        std::shared_lock<std::shared_mutex> lock(docs_mutex_);
        if (doc_id < documents_.size()) {
            filepath = documents_[doc_id].path;
            default_snippet = documents_[doc_id].snippet;
        }
    }

    if (filepath.empty() || query_terms.empty()) {
        return default_snippet;
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return default_snippet;
    }

    std::string text;
    text.resize(65536);
    file.read(&text[0], text.size());
    text.resize(static_cast<size_t>(file.gcount()));

    if (text.empty()) {
        return default_snippet;
    }

    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), [](unsigned char c) {
        return std::tolower(c);
    });

    size_t match_pos = std::string::npos;
    for (const auto& term : query_terms) {
        if (term.empty()) continue;
        std::string lower_term = term;
        std::transform(lower_term.begin(), lower_term.end(), lower_term.begin(), [](unsigned char c) {
            return std::tolower(c);
        });

        size_t pos = lower_text.find(lower_term);
        if (pos != std::string::npos && (match_pos == std::string::npos || pos < match_pos)) {
            match_pos = pos;
        }
    }

    if (match_pos == std::string::npos) {
        return default_snippet;
    }

    size_t start = (match_pos > 60) ? (match_pos - 60) : 0;
    if (start > 0) {
        size_t space_pos = text.find(' ', start);
        if (space_pos != std::string::npos && space_pos < match_pos) {
            start = space_pos + 1;
        }
    }

    size_t length = std::min<size_t>(text.size() - start, 180);
    if (start + length < text.size()) {
        size_t last_space = text.rfind(' ', start + length);
        if (last_space != std::string::npos && last_space > match_pos) {
            length = last_space - start;
        }
    }

    std::string snippet = text.substr(start, length);
    std::replace(snippet.begin(), snippet.end(), '\n', ' ');
    std::replace(snippet.begin(), snippet.end(), '\r', ' ');

    if (start > 0) snippet = "..." + snippet;
    if (start + length < text.size()) snippet += "...";

    return snippet;
}

const std::vector<std::string>& InvertedIndex::get_stop_words() {
    // Відсортований масив стоп-слів для бінарного пошуку O(log N) без STL unordered_set
    static const std::vector<std::string> stop_words = {
        "a", "about", "above", "after", "again", "against", "all", "am", "an", "and", "any", "are",
        "as", "at", "be", "because", "been", "before", "being", "below", "between", "both", "but",
        "by", "can", "could", "did", "do", "does", "doing", "down", "during", "each", "few", "for",
        "from", "further", "had", "has", "have", "having", "he", "her", "here", "hers", "herself",
        "him", "himself", "his", "how", "i", "if", "in", "into", "is", "it", "its", "itself", "just",
        "me", "more", "most", "my", "myself", "no", "nor", "not", "now", "of", "off", "on", "once",
        "only", "or", "other", "our", "ours", "ourselves", "out", "over", "own", "same", "she",
        "should", "so", "some", "such", "than", "that", "the", "their", "theirs", "them", "themselves",
        "then", "there", "these", "they", "this", "those", "through", "to", "too", "under", "until",
        "up", "very", "was", "we", "were", "what", "when", "where", "which", "while", "who", "whom",
        "why", "with", "would", "you", "your", "yours", "yourself", "yourselves"
    };
    return stop_words;
}

bool InvertedIndex::is_stop_word(const std::string& word) {
    const auto& stop_words = get_stop_words();
    return std::binary_search(stop_words.begin(), stop_words.end(), word);
}

} // namespace search
