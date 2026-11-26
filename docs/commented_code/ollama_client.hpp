#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex> // Для синхронізованої черги запитів до локальної LLM Ollama

namespace search {

// Результат розширення запиту через LLM
struct QueryExpansionResult {
    std::string original_query;             // Початковий запит користувача
    std::vector<std::string> expanded_terms; // Базові слова + згенеровані синоніми
    bool ollama_used{false};                // true, якщо LLM успішно відповіла
};

/**
 * HTTP-клієнт для взаємодії з контейнером Ollama (порт 11434).
 */
class OllamaClient {
public:
    explicit OllamaClient(std::string host = "ollama", int port = 11434, std::string model = "llama3.2:1b");
    ~OllamaClient() = default;

    // Перевірка стану Ollama через GET /api/tags (використовується в /api/health)
    bool is_healthy() const;

    // Генерація синонімів через POST /api/generate під захистом черги request_mutex_
    QueryExpansionResult expand_query(const std::string& query) const;

    void set_connection(std::string host, int port, std::string model);

private:
    std::string host_;
    int port_;
    std::string model_;

    // М'ютекс гарантує, що Ollama обробляє один запит на інференс за раз (черга із синхронізацією)
    mutable std::mutex request_mutex_;

    std::string http_post(const std::string& endpoint, const std::string& json_body, int timeout_seconds = 8) const;
    int http_get_status(const std::string& endpoint, int timeout_seconds = 2) const;
};

} // namespace search
