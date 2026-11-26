#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>
#include <atomic>

namespace search {

// Запис у списку індексу для конкретного терміну
struct Posting {
    uint32_t doc_id;
    uint32_t term_frequency; // Кількість входжень терміну в документі
};

// Метадані проіндексованого документа
struct DocumentInfo {
    uint32_t id;
    std::string filename;
    std::string path;
    uint32_t total_words;
    std::string snippet; // Збережений початковий фрагмент для швидкого прев'ю
};

// Результат пошуку для API
struct SearchResult {
    std::string doc_id;
    double score;
    std::string snippet;
};

class InvertedIndex {
public:
    static constexpr size_t NUM_SHARDS = 64; // Кількість шардингів для мінімізації блокувань

    InvertedIndex();
    ~InvertedIndex() = default;

    // Очищення індексу перед новою індексацією
    void clear();

    // Реєстрація документа (потокобезпечна)
    uint32_t register_document(const std::string& filename, const std::string& filepath, 
                               uint32_t total_words, const std::string& full_text);

    // Додавання розпарсених термінів документа до індексу (потокобезпечно)
    void add_document_terms(uint32_t doc_id, const std::unordered_map<std::string, uint32_t>& term_counts);

    // Пошук за списком термінів з ранжуванням за алгоритмом BM25
    std::vector<SearchResult> search(const std::vector<std::string>& query_terms, size_t limit = 20) const;

    // Отримання кількості проіндексованих документів
    size_t get_document_count() const;

    // Отримання середньої довжини документа
    double get_avg_doc_length() const;

    // Токенізація тексту на список слів (очищення, нижній регістр, фільтрація стоп-слів)
    static std::vector<std::string> tokenize(const std::string& text);

    // Швидкий підрахунок частот термінів без створення проміжного вектора токенів
    static void count_tokens(const std::string& text,
                             std::unordered_map<std::string, uint32_t>& term_counts,
                             uint32_t& total_words);

    // Витяг релевантного сніпету тексту навколо знайдених слів
    std::string generate_snippet(uint32_t doc_id, const std::vector<std::string>& query_terms) const;

private:
    // Шард індексу з власним Read-Write м'ютексом
    struct IndexShard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, std::vector<Posting>> table;
    };

    // Шардована хеш-таблиця для інвертованого індексу
    std::vector<IndexShard> shards_;

    // Шардинг для метаданих документів
    mutable std::shared_mutex docs_mutex_;
    std::vector<DocumentInfo> documents_;
    std::atomic<uint64_t> total_words_all_docs_{0};


    // Отримання індексу шарду за хешем слова
    size_t get_shard_index(const std::string& term) const {
        return std::hash<std::string>{}(term) % NUM_SHARDS;
    }

    static const std::unordered_set<std::string>& get_stop_words();
};

} // namespace search
