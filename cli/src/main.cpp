#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

void print_help(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options] <search query>\n"
              << "Options:\n"
              << "  -h, --help               Show this help message\n"
              << "  -s, --server <host:port> Server address (default: localhost:8088)\n"
              << "  -m, --mode <mode>        Search mode: 'keyword' or 'llm' (default: keyword)\n"
              << "  -i, --index <threads>    Trigger index rebuild with N threads\n"
              << "  --stats                  Show server index statistics\n"
              << "Example:\n"
              << "  " << prog_name << " \"space exploration\"\n"
              << "  " << prog_name << " -m llm \"artificial intelligence\"\n"
              << "  " << prog_name << " -i 8\n";
}

int main(int argc, char** argv) {
    std::string server_host = "localhost";
    int server_port = 8088;

    std::string mode = "keyword";
    std::string query = "";
    int index_threads = -1;
    bool show_stats = false;

    if (argc < 2) {
        print_help(argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                std::string server_str = argv[++i];
                auto colon = server_str.find(':');
                if (colon != std::string::npos) {
                    server_host = server_str.substr(0, colon);
                    server_port = std::stoi(server_str.substr(colon + 1));
                } else {
                    server_host = server_str;
                }
            }
        } else if (arg == "-m" || arg == "--mode") {
            if (i + 1 < argc) {
                mode = argv[++i];
            }
        } else if (arg == "-i" || arg == "--index") {
            if (i + 1 < argc) {
                index_threads = std::stoi(argv[++i]);
            } else {
                index_threads = 4;
            }
        } else if (arg == "--stats") {
            show_stats = true;
        } else {
            if (!query.empty()) query += " ";
            query += arg;
        }
    }

    httplib::Client cli(server_host, server_port);
    cli.set_connection_timeout(5, 0);
    cli.set_read_timeout(30, 0);

    // 1. Команда перебудови індексу
    if (index_threads > 0) {
        std::cout << "\033[1;36m[CLI] Triggering index rebuild with " << index_threads << " threads...\033[0m\n";
        json req_body = {{"threads", index_threads}};
        auto res = cli.Post("/api/index", req_body.dump(), "application/json");
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            std::cout << "\033[1;32m✓ Index built successfully!\033[0m\n";
            std::cout << "  Indexed documents: " << j.value("documents_indexed", 0) << "\n";
            std::cout << "  Time elapsed:      " << j.value("time_ms", 0) << " ms\n";
        } else {
            std::cerr << "\033[1;31m✗ Failed to build index. Server error or unreachable.\033[0m\n";
            return 1;
        }
        return 0;
    }

    // 2. Команда перегляду статистики
    if (show_stats) {
        auto res = cli.Get("/api/stats");
        if (res && res->status == 200) {
            auto j = json::parse(res->body);
            std::cout << "\033[1;34m=== Server Index Statistics ===\033[0m\n";
            std::cout << "  Documents:       " << j.value("documents", 0) << "\n";
            std::cout << "  Build Time:      " << j.value("index_build_time_ms", 0) << " ms\n";
            std::cout << "  Threads Used:    " << j.value("threads_used", 0) << "\n";
        } else {
            std::cerr << "\033[1;31m✗ Failed to get statistics.\033[0m\n";
            return 1;
        }
        return 0;
    }

    // 3. Пошуковий запит
    if (query.empty()) {
        std::cerr << "\033[1;31mError: Empty search query provided.\033[0m\n";
        print_help(argv[0]);
        return 1;
    }

    std::cout << "\033[1;36m[CLI] Searching for: \"" << query << "\" (mode: " << mode << ")...\033[0m\n";
    json req_body = {
        {"query", query},
        {"mode", mode}
    };

    auto res = cli.Post("/api/search", req_body.dump(), "application/json");
    if (!res || res->status != 200) {
        std::cerr << "\033[1;31m✗ Search failed. Make sure the search server is running on " 
                  << server_host << ":" << server_port << "\033[0m\n";
        return 1;
    }

    auto j = json::parse(res->body);
    auto results = j.value("results", json::array());
    uint64_t time_ms = j.value("time_ms", 0);

    std::cout << "\033[1;32m✓ Found " << results.size() << " results in " << time_ms << " ms\033[0m\n\n";

    if (results.empty()) {
        std::cout << "  No documents matched your query.\n";
        return 0;
    }

    int rank = 1;
    for (const auto& item : results) {
        std::string doc_id = item.value("doc_id", "unknown");
        double score = item.value("score", 0.0);
        std::string snippet = item.value("snippet", "");

        std::cout << "\033[1;33m[" << rank++ << "] " << doc_id << "\033[0m"
                  << "  \033[1;35m(Score: " << std::fixed << std::setprecision(2) << score << ")\033[0m\n";
        std::cout << "    \033[0;37m" << snippet << "\033[0m\n\n";
    }

    return 0;
}
