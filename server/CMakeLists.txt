# C++ Search Engine Server Executable
add_executable(search_server
    src/main.cpp
    src/thread_pool.cpp
    src/inverted_index.cpp
    src/ollama_client.cpp
    src/search_engine.cpp
)

target_include_directories(search_server PRIVATE
    include
)

target_link_libraries(search_server PRIVATE
    Crow::Crow
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads
)
