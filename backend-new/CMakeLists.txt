cmake_minimum_required(VERSION 3.14)
project(LocalArchVault CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Disable dependency test suites to speed up compilation
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

# 1. Fetch doctest (Testing Framework) - Bumped to latest modern release
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
)
FetchContent_MakeAvailable(doctest)

# 2. Fetch nlohmann/json (JSON Configuration Parser)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# 3. Define the test executable
add_executable(unit_tests 
    src/config/config.cpp
    src/config/config.h
    src/parser/canvas-parser.h
    src/parser/canvas-parser.cpp
    src/parser/markdown-parser.cpp
    src/parser/markdown-parser.h
    src/parser/parser-factory.h
    src/parser/parser.h
    tests/main_test.cpp
    tests/config_test.cpp
    tests/parser_test.cpp
)

# 4. Link dependencies to the test executable
target_link_libraries(unit_tests PRIVATE 
    doctest::doctest 
    nlohmann_json::nlohmann_json
)

add_custom_target(run_tests
    COMMAND unit_tests
    DEPENDS unit_tests
    COMMENT "Running unit tests..."
)
