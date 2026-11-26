#include "search_engine.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>

namespace search {

SearchEngine::SearchEngine(std::string data_directory, 
                           std::string ollama_host, 
                           int ollama_port, 
                           std::string ollama_model)
    : data_directory_(std::move(data_directory)),
      ollama_client_(std::move(ollama_host), ollama_port, std::move(ollama_model)) {}

IndexStats SearchEngine::build_index(size_t threads) {
    if (threads == 0) threads = 1;

    auto start_time = std::chrono::high_resolution_clock::now();
    index_.clear();

    std::vector<std::filesystem::path> files;
    try {
        if (std::filesystem::exists(data_directory_) && std::filesystem::is_directory(data_directory_)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(data_directory_)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path());
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "[SearchEngine] Filesystem scanning error: " << e.what() << std::endl;
    }

    if (!files.empty()) {
        ThreadPool pool(threads);
        
        // Розподіляємо файли пакетами для зменшення накладних витрат на синхронізацію
        const size_t batch_size = std::max<size_t>(1, files.size() / (threads * 8));
        for (size_t i = 0; i < files.size(); i += batch_size) {
            size_t end = std::min(files.size(), i + batch_size);
            pool.enqueue([this, &files, i, end]() {
                for (size_t j = i; j < end; ++j) {
                    this->process_file(files[j]);
                }
            });
        }

        pool.wait_all();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    size_t doc_count = index_.get_document_count();
    last_indexed_docs_.store(doc_count);
    last_build_time_ms_.store(duration_ms);
    last_threads_used_.store(threads);

    std::cout << "[SearchEngine] Indexed " << doc_count << " documents in " 
              << duration_ms << " ms using " << threads << " threads." << std::endl;

    return IndexStats{doc_count, static_cast<uint64_t>(duration_ms), threads};
}

void SearchEngine::process_file(const std::filesystem::path& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return;

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    if (content.empty()) return;

    std::vector<std::string> tokens = InvertedIndex::tokenize(content);
    if (tokens.empty()) return;

    std::unordered_map<std::string, uint32_t> term_counts;
    for (const auto& token : tokens) {
        term_counts[token]++;
    }

    std::string filename = file_path.filename().string();
    uint32_t doc_id = index_.register_document(filename, file_path.string(), 
                                               static_cast<uint32_t>(tokens.size()), content);
    index_.add_document_terms(doc_id, term_counts);
}

SearchResponse SearchEngine::search_keyword(const std::string& query, size_t limit) const {
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<std::string> tokens = InvertedIndex::tokenize(query);
    std::vector<SearchResult> results = index_.search(tokens, limit);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    SearchResponse response;
    response.results = std::move(results);
    response.time_ms = static_cast<uint64_t>(duration_ms);
    response.expanded_terms = tokens;
    response.original_query = query;
    return response;
}

SearchResponse SearchEngine::search_terms(const std::string& original_query,
                                          const std::vector<std::string>& terms,
                                          size_t limit) const {
    auto start_time = std::chrono::high_resolution_clock::now();

    std::vector<SearchResult> results = index_.search(terms, limit);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    SearchResponse response;
    response.results = std::move(results);
    response.time_ms = static_cast<uint64_t>(duration_ms);
    response.expanded_terms = terms;
    response.original_query = original_query;
    return response;
}

SearchResponse SearchEngine::search_with_expansion(const std::string& query, size_t limit) const {
    QueryExpansionResult expansion = ollama_client_.expand_query(query);
    return search_terms(query, expansion.expanded_terms, limit);
}

IndexStats SearchEngine::get_stats() const {
    return IndexStats{
        index_.get_document_count(),
        last_build_time_ms_.load(),
        last_threads_used_.load()
    };
}

bool SearchEngine::is_ollama_healthy() const {
    return ollama_client_.is_healthy();
}

} // namespace search
