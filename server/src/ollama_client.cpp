#include "ollama_client.hpp"
#include "inverted_index.hpp"
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <iostream>
#include <algorithm>

namespace search {

using json = nlohmann::json;

OllamaClient::OllamaClient(std::string host, int port, std::string model)
    : host_(std::move(host)), port_(port), model_(std::move(model)) {}

void OllamaClient::set_connection(std::string host, int port, std::string model) {
    host_ = std::move(host);
    port_ = port;
    model_ = std::move(model);
}

int OllamaClient::http_get_status(const std::string& endpoint, int timeout_seconds) const {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(timeout_seconds, 0);
        cli.set_read_timeout(timeout_seconds, 0);
        auto res = cli.Get(endpoint);
        if (res) {
            return res->status;
        }
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] Health check failed: " << e.what() << std::endl;
    }
    return -1;
}

std::string OllamaClient::http_post(const std::string& endpoint, const std::string& json_body, int timeout_seconds) const {
    try {
        httplib::Client cli(host_, port_);
        cli.set_connection_timeout(timeout_seconds, 0);
        cli.set_read_timeout(timeout_seconds, 0);
        auto res = cli.Post(endpoint, json_body, "application/json");
        if (res && res->status == 200) {
            return res->body;
        }
    } catch (const std::exception& e) {
        std::cerr << "[OllamaClient] HTTP POST failed: " << e.what() << std::endl;
    }
    return "";
}

bool OllamaClient::is_healthy() const {
    int status = http_get_status("/api/tags", 2);
    return (status == 200);
}

QueryExpansionResult OllamaClient::expand_query(const std::string& query) const {
    QueryExpansionResult result;
    result.original_query = query;

    // Спочатку токенізуємо початковий запит
    std::vector<std::string> base_tokens = InvertedIndex::tokenize(query);
    for (const auto& tok : base_tokens) {
        result.expanded_terms.push_back(tok);
    }

    if (query.empty()) {
        return result;
    }

    // Формуємо промпт для LLM: просимо видати лише синоніми та споріднені ключові слова у форматі JSON
    std::string prompt = "You are a search engine query expansion assistant. Given the user search query: \"" + 
                         query + 
                         "\", output ONLY a JSON object with a key 'keywords' containing 3-5 relevant synonyms or related search keywords (single words, lowercase, English). Do not include explanations. Example format: {\"keywords\": [\"term1\", \"term2\", \"term3\"]}";

    json request_payload = {
        {"model", model_},
        {"prompt", prompt},
        {"stream", false},
        {"format", "json"},
        {"options", {
            {"temperature", 0.3},
            {"num_predict", 150}
        }}
    };

    std::string response_str;
    {
        // Ollama обробляє один запит за раз — синхронізована черга звернень до LLM
        std::lock_guard<std::mutex> lock(request_mutex_);
        response_str = http_post("/api/generate", request_payload.dump(), 60);
    }

    if (!response_str.empty()) {
        try {
            json res_json = json::parse(response_str);
            if (res_json.contains("response")) {
                std::string response_content = res_json["response"].get<std::string>();

                // Очищення відповіді від markdown-блоків (```json ... ```) та зайвого тексту від LLM
                size_t first_brace = response_content.find('{');
                size_t first_bracket = response_content.find('[');
                size_t start_pos = std::string::npos;
                if (first_brace != std::string::npos && first_bracket != std::string::npos) {
                    start_pos = std::min(first_brace, first_bracket);
                } else if (first_brace != std::string::npos) {
                    start_pos = first_brace;
                } else {
                    start_pos = first_bracket;
                }

                size_t last_brace = response_content.rfind('}');
                size_t last_bracket = response_content.rfind(']');
                size_t end_pos = std::string::npos;
                if (last_brace != std::string::npos && last_bracket != std::string::npos) {
                    end_pos = std::max(last_brace, last_bracket);
                } else if (last_brace != std::string::npos) {
                    end_pos = last_brace;
                } else {
                    end_pos = last_bracket;
                }

                if (start_pos != std::string::npos && end_pos != std::string::npos && end_pos >= start_pos) {
                    response_content = response_content.substr(start_pos, end_pos - start_pos + 1);
                }

                json parsed_keywords = json::parse(response_content);

                std::vector<std::string> extracted_terms;
                if (parsed_keywords.is_array()) {
                    for (const auto& item : parsed_keywords) {
                        if (item.is_string()) {
                            extracted_terms.push_back(item.get<std::string>());
                        }
                    }
                } else if (parsed_keywords.is_object() && parsed_keywords.contains("keywords")) {
                    for (const auto& item : parsed_keywords["keywords"]) {
                        if (item.is_string()) {
                            extracted_terms.push_back(item.get<std::string>());
                        }
                    }
                }

                for (const auto& raw_kw : extracted_terms) {
                    auto tokens = InvertedIndex::tokenize(raw_kw);
                    for (const auto& tok : tokens) {
                        if (std::find(result.expanded_terms.begin(), result.expanded_terms.end(), tok) == result.expanded_terms.end()) {
                            result.expanded_terms.push_back(tok);
                        }
                    }
                }

                result.ollama_used = true;
                return result;
            }
        } catch (const std::exception& e) {
            std::cerr << "[OllamaClient] Failed to parse Ollama JSON response: " << e.what() << std::endl;
        }
    }

    // Якщо Ollama не відповіла, додаємо базове розширення
    result.ollama_used = false;
    return result;
}

} // namespace search
