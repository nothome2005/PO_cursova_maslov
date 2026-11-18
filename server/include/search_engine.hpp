#pragma once

#include "inverted_index.hpp"
#include "thread_pool.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <atomic>

namespace search {

struct IndexStats {
    size_t documents{0};
    uint64_t index_build_time_ms{0};
    size_t threads_used{0};
};

struct SearchResponse {
    std::vector<SearchResult> results;
    uint64_t time_ms{0};
    std::vector<std::string> expanded_terms;
    std::string original_query;
};

class SearchEngine {
public:
    explicit SearchEngine(std::string data_directory = "./data/documents");
    ~SearchEngine() = default;

    // Побудова індексу з заданою кількістю потоків
    IndexStats build_index(size_t threads = 4);

    // Звичайний пошук за ключовими словами (mode: "keyword")
    SearchResponse search_keyword(const std::string& query, size_t limit = 20) const;

    // Отримання поточної статистики
    IndexStats get_stats() const;

    void set_data_directory(std::string data_directory) {
        data_directory_ = std::move(data_directory);
    }

private:
    std::string data_directory_;
    InvertedIndex index_;

    std::atomic<size_t> last_indexed_docs_{0};
    std::atomic<uint64_t> last_build_time_ms_{0};
    std::atomic<size_t> last_threads_used_{0};

    // Обробка одного файлу в потоці
    void process_file(const std::filesystem::path& file_path);
};

} // namespace search
