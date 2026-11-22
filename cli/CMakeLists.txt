# C++ Search Engine CLI Client Executable
add_executable(search_cli
    src/main.cpp
)

target_link_libraries(search_cli PRIVATE
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads
)

if(WIN32)
    target_link_libraries(search_cli PRIVATE ws2_32 wsock32)
endif()

# Створення псевдоніма для зручного запуску
set_target_properties(search_cli PROPERTIES OUTPUT_NAME "search")
