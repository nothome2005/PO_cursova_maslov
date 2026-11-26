#pragma once

#include "inverted_index.hpp"
#include "thread_pool.hpp"
#include "ollama_client.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <atomic>
#include <mutex>

namespace search {

// Метрики стану індексу для GET /api/stats і POST /api/index
struct IndexStats {
    size_t documents{0};
    uint64_t index_build_time_ms{0};
    size_t threads_used{0};
};

// Уніфікована відповідь пошуку (для REST API та WebSocket)
struct SearchResponse {
    std::vector<SearchResult> results;
    uint64_t time_ms{0};
    std::vector<std::string> expanded_terms;
    std::string original_query;
};

/**
 * Головний фасад пошукової системи.
 * Поєднує пул потоків ThreadPool, шардований InvertedIndex та клієнт OllamaClient.
 */
class SearchEngine {
public:
    SearchEngine(std::string data_directory = "./data/documents",
                 std::string ollama_host = "ollama",
                 int ollama_port = 11434,
                 std::string ollama_model = "llama3.2:1b");
    ~SearchEngine() = default;

    // Паралельна побудова індексу на `threads` потоках (захищена index_build_mutex_)
    IndexStats build_index(size_t threads = 4);

    // Пошук без LLM (mode: "keyword")
    SearchResponse search_keyword(const std::string& query, size_t limit = 20) const;

    // Пошук з автоматичним розширенням запиту через Ollama (mode: "llm")
    SearchResponse search_with_expansion(const std::string& query, size_t limit = 20) const;

    // Пошук за вже готовим списком розширених термів (використовується у WebSocket,
    // щоб не викликати Ollama двічі на один і той самий запит!)
    SearchResponse search_terms(const std::string& original_query,
                                const std::vector<std::string>& terms,
                                size_t limit = 20) const;

    // Повертає статистику останньої побудови індексу
    IndexStats get_stats() const;

    // Перевірка доступності сервісу Ollama
    bool is_ollama_healthy() const;

    const OllamaClient& get_ollama_client() const { return ollama_client_; }

    void set_data_directory(std::string data_directory) {
        data_directory_ = std::move(data_directory);
    }

private:
    std::string data_directory_;
    InvertedIndex index_;
    OllamaClient ollama_client_;

    // Атомарні лічильники для безпечного читання статистики під час запитів
    std::atomic<size_t> last_indexed_docs_{0};
    std::atomic<uint64_t> last_build_time_ms_{0};
    std::atomic<size_t> last_threads_used_{0};

    // Гарантує, що дві перебудови індексу (наприклад, фоновий старт + POST /api/index)
    // не будуть виконуватись одночасно і не пошкодять індекс
    mutable std::mutex index_build_mutex_;

    // Читання й токенізація одного файла всередині робочого потоку ThreadPool
    void process_file(const std::filesystem::path& file_path);
};

} // namespace search
