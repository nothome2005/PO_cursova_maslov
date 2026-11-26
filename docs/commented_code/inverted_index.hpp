#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>  // Дозволяє багатьом потокам читати одночасно (shared_lock) або одному писати (unique_lock)
#include <mutex>
#include <memory>
#include <algorithm>
#include <cmath>         // Для std::log та std::round у формулі BM25
#include <atomic>

namespace search {

// Окремий запис у posting-списку для конкретного слова:
// зберігає ID документа і те, скільки разів слово зустрілося в ньому (TF)
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
    std::string snippet;   // Кешований у пам'яті початок тексту (щоб не читати диск при кожному пошуку!)
};

// Структура результату, яку індекс повертає на сервер для JSON-відповіді
struct SearchResult {
    std::string doc_id;
    double score;
    std::string snippet;
};

/**
 * Власна потокобезпечна структура даних для інвертованого індексу.
 * Ключова архітектурна ідея — Lock Striping (шардування на 64 незалежні шарди).
 */
class InvertedIndex {
public:
    // 64 незалежні таблиці зі своїми м'ютексами -> потоки майже не блокують один одного
    static constexpr size_t NUM_SHARDS = 64;

    InvertedIndex();
    ~InvertedIndex() = default;

    // Повне очищення індексу перед новою індексацією
    void clear();

    // Потокобезпечна реєстрація документа в таблиці метаданих documents_ (повертає унікальний id)
    uint32_t register_document(const std::string& filename, const std::string& filepath,
                               uint32_t total_words, const std::string& full_text);

    // Пакетне додавання частот слів документа в шарди (1 блокування шарду на документ!)
    void add_document_terms(uint32_t doc_id, const std::unordered_map<std::string, uint32_t>& term_counts);

    // Пошук за списком термів з ранжуванням BM25 (використовує shared_lock — не блокує інших читачів)
    std::vector<SearchResult> search(const std::vector<std::string>& query_terms, size_t limit = 20) const;

    // Повертає кількість проіндексованих документів
    size_t get_document_count() const;

    // Середня довжина документа avgdl по всій колекції (потрібна для BM25)
    double get_avg_doc_length() const;

    // Токенізація коротких рядків (запиту користувача або слів від LLM)
    static std::vector<std::string> tokenize(const std::string& text);

    // Оптимізований підрахунок частот слів у великих файлах БЕЗ створення вектора всіх слів у пам'яті
    static void count_tokens(const std::string& text,
                             std::unordered_map<std::string, uint32_t>& term_counts,
                             uint32_t& total_words);

    // Метод пошуку фрагмента навколо знайдених слів на диску (залишений для сумісності)
    std::string generate_snippet(uint32_t doc_id, const std::vector<std::string>& query_terms) const;

private:
    // Один шард індексу = окрема хеш-таблиця + окремий Read-Write м'ютекс
    struct IndexShard {
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, std::vector<Posting>> table;
    };

    // Масив із 64 незалежних шардів
    std::vector<IndexShard> shards_;

    // Захист метаданих документів (вектор documents_ та атомарна сума всіх слів)
    mutable std::shared_mutex docs_mutex_;
    std::vector<DocumentInfo> documents_;
    std::atomic<uint64_t> total_words_all_docs_{0};

    // Детермінований розподіл слова по шардах за його хешем
    size_t get_shard_index(const std::string& term) const {
        return std::hash<std::string>{}(term) % NUM_SHARDS;
    }

    // Статичний словник англійських стоп-слів ("the", "and", "is", ...)
    static const std::unordered_set<std::string>& get_stop_words();
};

} // namespace search
