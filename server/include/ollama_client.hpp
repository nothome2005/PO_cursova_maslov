#pragma once

#include <string>
#include <vector>
#include <memory>

namespace search {

struct QueryExpansionResult {
    std::string original_query;
    std::vector<std::string> expanded_terms;
    bool ollama_used{false};
};

class OllamaClient {
public:
    explicit OllamaClient(std::string host = "ollama", int port = 11434, std::string model = "llama3.2:1b");
    ~OllamaClient() = default;

    // Перевірка доступності сервера Ollama
    bool is_healthy() const;

    // Розширення пошукового запиту синонімами та ключовими словами через LLM
    QueryExpansionResult expand_query(const std::string& query) const;

    // Зміна хосту/порту/моделі
    void set_connection(std::string host, int port, std::string model);

private:
    std::string host_;
    int port_;
    std::string model_;

    // Внутрішній метод для виконання HTTP POST запиту
    std::string http_post(const std::string& endpoint, const std::string& json_body, int timeout_seconds = 8) const;
    
    // Внутрішній метод для виконання HTTP GET запиту
    int http_get_status(const std::string& endpoint, int timeout_seconds = 2) const;
};

} // namespace search
