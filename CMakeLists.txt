cmake_minimum_required(VERSION 3.20)

project(epochkv LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
    epochkv_core
    src/storage/KeyValueStore.cpp
    src/storage/LogStore.cpp
    src/Database.cpp
)

target_include_directories(epochkv_core PUBLIC include)

target_compile_options(
    epochkv_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

include(CTest)

if(BUILD_TESTING)
    add_executable(
        key_value_store_tests
        tests/KeyValueStoreTests.cpp
    )

    target_link_libraries(key_value_store_tests PRIVATE epochkv_core)

    add_test(NAME KeyValueStore COMMAND key_value_store_tests)

    add_executable(
        log_store_tests
        tests/LogStoreTests.cpp
    )

    target_link_libraries(log_store_tests PRIVATE epochkv_core)

    add_test(NAME LogStore COMMAND log_store_tests)


    add_executable(
        database_tests
        tests/DatabaseTests.cpp
    )

    target_link_libraries(database_tests PRIVATE epochkv_core)

    add_test(NAME Database COMMAND database_tests)

endif()
