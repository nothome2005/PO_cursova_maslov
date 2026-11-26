#pragma once

#include <string>
#include <vector>
#include <shared_mutex>  // Дозволяє багатьом потокам читати одночасно (shared_lock) або одному писати (unique_lock)
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>         // Для std::log та std::round у формулі BM25
#include <atomic>
#include <cstdint>

namespace search {

/**
 * Власна реалізація хеш-таблиці поверх динамічних масивів (std::vector)
 * з вирішенням колізій методом ланцюжків (Separate Chaining) та хешем FNV-1a.
 * Головне для захисту: ми НЕ використовуємо STL std::unordered_map або std::map —
 * уся структура побудована повністю самостійно поверх масивів і списків!
 */
template <typename V>
class StringHashMap {
public:
    // Елемент бакету: пара (ключ-слово, значення)
    struct Entry {
        std::string key;
        V value;
    };

    // Створюємо масив із initial_buckets порожніх кошиків (бакетів)
    explicit StringHashMap(size_t initial_buckets = 256)
        : buckets_(initial_buckets > 0 ? initial_buckets : 16), size_(0) {}

    // 64-бітна хеш-функція FNV-1a (швидка, рівномірно розподіляє рядки по бакетах)
    static uint64_t fnv1a_hash(const std::string& key) {
        uint64_t hash = 14695981039346656037ULL;
        for (unsigned char c : key) {
            hash ^= c;
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    // Пошук за ключем (немодифікуючий, повертає вказівник або nullptr) — середня складність O(1)
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

    // Повертає посилання на значення або створює новий запис при відсутності ключа
    V& get_or_insert(const std::string& key) {
        // Якщо коефіцієнт заповнення (Load Factor = size / buckets) > 0.75 — подвоюємо масив бакетів
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

    // Обхід усіх пар (ключ, значення) для пакетного перенесення у шарди індексу
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
    std::vector<std::vector<Entry>> buckets_; // Масив бакетів, кожен бакет — ланцюжок записів
    size_t size_{0};                          // Поточна кількість унікальних ключів

    // Розширення таблиці вдвічі та перерозподіл усіх елементів за новими індексами
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

// Окремий запис у posting-списку для конкретного слова
struct Posting {
    uint32_t doc_id;
    uint32_t term_frequency;
};

// Метадані проіндексованого документа (зберігаються в пам'яті)
struct DocumentInfo {
    uint32_t id;
    std::string filename;  // Назва файлу (повертається в API як doc_id)
    std::string path;      // Повний шлях на диску
    uint32_t total_words;  // Довжина документа в токенах (потрібна для нормалізації BM25)
    std::string snippet;   // Кешований у пам'яті початок тексту
};

// Структура результату пошуку для REST API та WebSocket
struct SearchResult {
    std::string doc_id;
    double score;
    std::string snippet;
};

/**
 * Повністю самостійна потокобезпечна структура даних для інвертованого індексу.
 * Поєднує Lock Striping (64 шарди з std::shared_mutex) та власну хеш-таблицю StringHashMap.
 */
class InvertedIndex {
public:
    static constexpr size_t NUM_SHARDS = 64;

    InvertedIndex();
    ~InvertedIndex() = default;

    void clear();

    uint32_t register_document(const std::string& filename, const std::string& filepath,
                               uint32_t total_words, const std::string& full_text);

    // Пакетне додавання частот слів із власної таблиці StringHashMap у шарди
    void add_document_terms(uint32_t doc_id, const StringHashMap<uint32_t>& term_counts);

    // Пошук за списком термів з ранжуванням BM25 (shared_lock + плаский масив оцінок)
    std::vector<SearchResult> search(const std::vector<std::string>& query_terms, size_t limit = 20) const;

    size_t get_document_count() const;
    double get_avg_doc_length() const;

    static std::vector<std::string> tokenize(const std::string& text);

    // Прямий підрахунок частот слів у власну хеш-таблицю StringHashMap
    static void count_tokens(const std::string& text,
                             StringHashMap<uint32_t>& term_counts,
                             uint32_t& total_words);

    std::string generate_snippet(uint32_t doc_id, const std::vector<std::string>& query_terms) const;

private:
    // Один шард = окремий Read-Write м'ютекс + власна хеш-таблиця StringHashMap
    struct IndexShard {
        mutable std::shared_mutex mutex;
        StringHashMap<std::vector<Posting>> table{256};
    };

    std::vector<IndexShard> shards_;

    mutable std::shared_mutex docs_mutex_;
    std::vector<DocumentInfo> documents_;
    std::atomic<uint64_t> total_words_all_docs_{0};

    // Детермінований вибір шарду через нашу власну хеш-функцію FNV-1a
    size_t get_shard_index(const std::string& term) const {
        return StringHashMap<uint32_t>::fnv1a_hash(term) % NUM_SHARDS;
    }

    // Відсортований масив стоп-слів + бінарний пошук (без std::unordered_set)
    static const std::vector<std::string>& get_stop_words();
    static bool is_stop_word(const std::string& word);
};

} // namespace search
