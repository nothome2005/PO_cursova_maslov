#pragma once

#include <string>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>

namespace search {

/**
 * Власна реалізація хеш-таблиці поверх динамічних масивів (std::vector)
 * з вирішенням колізій методом ланцюжків (Separate Chaining) та хешем FNV-1a.
 * Повністю замінює STL std::unordered_map відповідно до вимог курсової роботи.
 */
template <typename V>
class StringHashMap {
public:
    struct Entry {
        std::string key;
        V value;
    };

    explicit StringHashMap(size_t initial_buckets = 256)
        : buckets_(initial_buckets > 0 ? initial_buckets : 16), size_(0) {}

    // 64-бітна хеш-функція FNV-1a
    static uint64_t fnv1a_hash(const std::string& key) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : key) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    V* find(const std::string& key) {
        if (buckets_.empty()) return nullptr;
        size_t idx = fnv1a_hash(key) % buckets_.size();
        for (auto& entry : buckets_[idx]) {
            if (entry.key == key) return &entry.value;
        }
        return nullptr;
    }

    const V* find(const std::string& key) const {
        if (buckets_.empty()) return nullptr;
        size_t idx = fnv1a_hash(key) % buckets_.size();
        for (const auto& entry : buckets_[idx]) {
            if (entry.key == key) return &entry.value;
        }
        return nullptr;
    }

    V& get_or_insert(const std::string& key) {
        // Автоматичне рехешування при коефіцієнті заповнення > 0.75
        if ((size_ + 1) * 4 > buckets_.size() * 3) {
            rehash(buckets_.size() * 2);
        }
        size_t idx = fnv1a_hash(key) % buckets_.size();
        for (auto& entry : buckets_[idx]) {
            if (entry.key == key) return entry.value;
        }
        buckets_[idx].push_back(Entry{key, V{}});
        ++size_;
        return buckets_[idx].back().value;
    }

    void clear() {
        for (auto& bucket : buckets_) {
            bucket.clear();
        }
        size_ = 0;
    }

    template <typename Callback>
    void for_each(Callback&& cb) const {
        for (const auto& bucket : buckets_) {
            for (const auto& entry : bucket) {
                cb(entry.key, entry.value);
            }
        }
    }

    size_t size() const { return size_; }

private:
    std::vector<std::vector<Entry>> buckets_;
    size_t size_{0};

    void rehash(size_t new_bucket_count) {
        std::vector<std::vector<Entry>> new_buckets(new_bucket_count);
        for (auto& bucket : buckets_) {
            for (auto& entry : bucket) {
                size_t idx = fnv1a_hash(entry.key) % new_bucket_count;
                new_buckets[idx].push_back(std::move(entry));
            }
        }
        buckets_ = std::move(new_buckets);
    }
};

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
    void add_document_terms(uint32_t doc_id, const StringHashMap<uint32_t>& term_counts);

    // Пошук за списком термінів з ранжуванням за алгоритмом BM25
    std::vector<SearchResult> search(const std::vector<std::string>& query_terms, size_t limit = 20) const;

    // Отримання кількості проіндексованих документів
    size_t get_document_count() const;

    // Отримання середньої довжини документа
    double get_avg_doc_length() const;

    // Токенізація тексту на список слів (очищення, нижній регістр, фільтрація стоп-слів)
    static std::vector<std::string> tokenize(const std::string& text);

    // Швидкий підрахунок частот термінів у власну хеш-таблицю без створення проміжного вектора токенів
    static void count_tokens(const std::string& text,
                             StringHashMap<uint32_t>& term_counts,
                             uint32_t& total_words);

    // Витяг релевантного сніпету тексту навколо знайдених слів
    std::string generate_snippet(uint32_t doc_id, const std::vector<std::string>& query_terms) const;

private:
    // Шард індексу з власним Read-Write м'ютексом і власною хеш-таблицею поверх масивів
    struct IndexShard {
        mutable std::shared_mutex mutex;
        StringHashMap<std::vector<Posting>> table{256};
    };

    // Шардована хеш-таблиця для інвертованого індексу
    std::vector<IndexShard> shards_;

    // Метадані документів
    mutable std::shared_mutex docs_mutex_;
    std::vector<DocumentInfo> documents_;
    std::atomic<uint64_t> total_words_all_docs_{0};

    // Отримання індексу шарду за власним хешем слова
    size_t get_shard_index(const std::string& term) const {
        return StringHashMap<uint32_t>::fnv1a_hash(term) % NUM_SHARDS;
    }

    // Відсортований масив стоп-слів (пошук через binary_search без std::unordered_set)
    static const std::vector<std::string>& get_stop_words();
    static bool is_stop_word(const std::string& word);
};

} // namespace search
