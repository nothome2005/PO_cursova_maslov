#include "search_engine.hpp"
#include <crow.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstdlib>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

using json = nlohmann::json;

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    std::string data_dir = "./data/documents";
    std::string ollama_host = "ollama";
    int ollama_port = 11434;
    std::string ollama_model = "llama3.2:1b";
    int server_port = 8088;

    // Зчитування параметрів з середовища (Environment variables)
    if (const char* env_dir = std::getenv("DATA_DIR")) data_dir = env_dir;
    if (const char* env_host = std::getenv("OLLAMA_HOST")) ollama_host = env_host;
    if (const char* env_port = std::getenv("OLLAMA_PORT")) ollama_port = std::stoi(env_port);
    if (const char* env_model = std::getenv("OLLAMA_MODEL")) ollama_model = env_model;
    if (const char* env_sport = std::getenv("PORT")) server_port = std::stoi(env_sport);

    std::cout << "==================================================" << std::endl;
    std::cout << "  Parallel Search Engine & LLM Query Expansion" << std::endl;
    std::cout << "  Port: " << server_port << std::endl;
    std::cout << "  Data directory: " << data_dir << std::endl;
    std::cout << "  Ollama: http://" << ollama_host << ":" << ollama_port << " (" << ollama_model << ")" << std::endl;
    std::cout << "==================================================" << std::endl;

    auto engine = std::make_shared<search::SearchEngine>(data_dir, ollama_host, ollama_port, ollama_model);

    // Початкова індексація за замовчуванням при старті
    std::cout << "[Init] Starting initial dataset indexing with 4 threads..." << std::endl;
    engine->build_index(4);


    crow::SimpleApp app;

    // CORS Headers Helper
    auto add_cors_headers = [](crow::response& res) {
        res.add_header("Access-Control-Allow-Origin", "*");
        res.add_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res.add_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
    };

    // OPTIONS маршрути для CORS preflight
    CROW_ROUTE(app, "/api/<path>")
    .methods(crow::HTTPMethod::OPTIONS)
    ([&add_cors_headers](const std::string&) {
        crow::response res(204);
        add_cors_headers(res);
        return res;
    });

    // 1. GET /api/health
    CROW_ROUTE(app, "/api/health")
    .methods(crow::HTTPMethod::GET)
    ([&engine, &add_cors_headers]() {
        bool ollama_ok = engine->is_ollama_healthy();
        json res_json = {
            {"status", "ok"},
            {"ollama", ollama_ok}
        };
        crow::response res(200, res_json.dump());
        res.set_header("Content-Type", "application/json");
        add_cors_headers(res);
        return res;
    });

    // 2. POST /api/index
    CROW_ROUTE(app, "/api/index")
    .methods(crow::HTTPMethod::POST)
    ([&engine, &add_cors_headers](const crow::request& req) {
        size_t threads = 4;
        try {
            auto body_json = json::parse(req.body);
            if (body_json.contains("threads") && body_json["threads"].is_number()) {
                threads = body_json["threads"].get<size_t>();
            }
        } catch (...) {
            // За замовчуванням 4 потоки
        }

        auto stats = engine->build_index(threads);
        json res_json = {
            {"status", "ok"},
            {"documents_indexed", stats.documents},
            {"time_ms", stats.index_build_time_ms}
        };

        crow::response res(200, res_json.dump());
        res.set_header("Content-Type", "application/json");
        add_cors_headers(res);
        return res;
    });

    // 3. POST /api/search
    CROW_ROUTE(app, "/api/search")
    .methods(crow::HTTPMethod::POST)
    ([&engine, &add_cors_headers](const crow::request& req) {
        std::string query;
        std::string mode = "keyword";

        try {
            auto body_json = json::parse(req.body);
            if (body_json.contains("query") && body_json["query"].is_string()) {
                query = body_json["query"].get<std::string>();
            }
            if (body_json.contains("mode") && body_json["mode"].is_string()) {
                mode = body_json["mode"].get<std::string>();
            }
        } catch (const std::exception& e) {
            crow::response res(400, json({{"error", "Invalid JSON"}}).dump());
            add_cors_headers(res);
            return res;
        }

        search::SearchResponse search_res;
        if (mode == "keyword") {
            search_res = engine->search_keyword(query);
        } else {
            search_res = engine->search_with_expansion(query);
        }

        json results_array = json::array();
        for (const auto& r : search_res.results) {
            results_array.push_back({
                {"doc_id", r.doc_id},
                {"score", r.score},
                {"snippet", r.snippet}
            });
        }

        json res_json = {
            {"results", results_array},
            {"time_ms", search_res.time_ms},
            {"expanded_terms", search_res.expanded_terms}
        };

        crow::response res(200, res_json.dump());
        res.set_header("Content-Type", "application/json");
        add_cors_headers(res);
        return res;
    });

    // 4. GET /api/stats
    CROW_ROUTE(app, "/api/stats")
    .methods(crow::HTTPMethod::GET)
    ([&engine, &add_cors_headers]() {
        auto stats = engine->get_stats();
        json res_json = {
            {"documents", stats.documents},
            {"index_build_time_ms", stats.index_build_time_ms},
            {"threads_used", stats.threads_used}
        };

        crow::response res(200, res_json.dump());
        res.set_header("Content-Type", "application/json");
        add_cors_headers(res);
        return res;
    });

    // 5. WebSocket /ws/search
    CROW_WEBSOCKET_ROUTE(app, "/ws/search")
    .onopen([&](crow::websocket::connection& conn) {
        std::cout << "[WS] Client connected: " << &conn << std::endl;
    })
    .onclose([&](crow::websocket::connection& conn, const std::string& reason) {
        std::cout << "[WS] Client disconnected: " << &conn << " (" << reason << ")" << std::endl;
    })
    .onmessage([&engine](crow::websocket::connection& conn, const std::string& data, bool is_binary) {
        if (is_binary) return;
        std::string query;
        try {
            auto req_json = json::parse(data);
            if (req_json.contains("query") && req_json["query"].is_string()) {
                query = req_json["query"].get<std::string>();
            }
        } catch (...) {
            query = data;
        }

        if (query.empty()) return;

        // Етап 1: Query Expansion (Ollama)
        auto expansion = engine->get_ollama_client().expand_query(query);
        json step1 = {
            {"stage", "query_expansion"},
            {"data", {
                {"original", query},
                {"expanded_terms", expansion.expanded_terms}
            }}
        };
        conn.send_text(step1.dump());

        // Етап 2: Keyword Search (Inverted Index)
        auto start_t = std::chrono::high_resolution_clock::now();
        auto search_res = engine->search_with_expansion(query);
        auto end_t = std::chrono::high_resolution_clock::now();
        auto search_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_t - start_t).count();

        json results_arr = json::array();
        for (const auto& r : search_res.results) {
            results_arr.push_back({
                {"doc_id", r.doc_id},
                {"score", r.score},
                {"snippet", r.snippet}
            });
        }

        json step2 = {
            {"stage", "keyword_search"},
            {"data", {
                {"results", results_arr},
                {"time_ms", search_time_ms}
            }}
        };
        conn.send_text(step2.dump());

        // Етап 3: Done
        json step3 = {
            {"stage", "done"}
        };
        conn.send_text(step3.dump());
    });

    // 6. Роздача статичних файлів Web UI
    // Автоматичне визначення шляху до папки public для запуску як з кореня проекту, так і з білд-директорії (build/bin)
    std::filesystem::path public_dir = "public";
    if (!std::filesystem::exists(public_dir)) {
        if (std::filesystem::exists("../public")) {
            public_dir = "../public";
        } else if (std::filesystem::exists("../../public")) {
            public_dir = "../../public";
        }
    }
    std::cout << "[Server] Web UI static root: " << std::filesystem::absolute(public_dir) << std::endl;

    CROW_ROUTE(app, "/")
    ([public_dir]() {
        crow::response res;
        auto index_path = public_dir / "index.html";
        if (std::filesystem::exists(index_path)) {
            res.set_static_file_info(index_path.string());
        } else {
            res.code = 404;
            res.write("Web UI index.html not found");
        }
        return res;
    });

    CROW_ROUTE(app, "/assets/<path>")
    ([public_dir](const std::string& path) {
        crow::response res;
        auto full_path = public_dir / "assets" / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            res.set_static_file_info(full_path.string());
        } else {
            res.code = 404;
            res.write("Asset not found");
        }
        return res;
    });

    CROW_ROUTE(app, "/<path>")
    ([public_dir](const std::string& path) {
        crow::response res;
        auto full_path = public_dir / path;
        if (std::filesystem::exists(full_path) && std::filesystem::is_regular_file(full_path)) {
            res.set_static_file_info(full_path.string());
        } else {
            auto index_path = public_dir / "index.html";
            if (std::filesystem::exists(index_path)) {
                res.set_static_file_info(index_path.string());
            } else {
                res.code = 404;
                res.write("Not found");
            }
        }
        return res;
    });

    app.port(server_port).multithreaded().run();
    return 0;
}
