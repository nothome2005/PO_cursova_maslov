cmake_minimum_required(VERSION 3.20)
project(ParallelSearchEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Оптимізація та попередження
if(MSVC)
    add_compile_options(/W4 /EHsc /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -O3 -pthread)
endif()

# Менеджер залежностей FetchContent
include(FetchContent)

# 1. nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 2. cpp-httplib (для клієнтських HTTP-запитів до Ollama та REST API)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# 3. Crow C++ (для веб-сервера та WebSocket)
FetchContent_Declare(
    Crow
    GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
    GIT_TAG v1.2.0
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Crow)

# Знаходження системних потоків
find_package(Threads REQUIRED)

# Додавання піддиректорій
add_subdirectory(server)
