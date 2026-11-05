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
    src/network/MessageIO.cpp
    src/network/ReplicationIO.cpp
    src/replication/BackupStore.cpp
    src/replication/BackupClient.cpp
)

target_include_directories(epochkv_core PUBLIC include)

target_compile_options(
    epochkv_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

add_executable(epochkv
    src/main.cpp
)

target_link_libraries(epochkv PRIVATE epochkv_core)

include(CTest)

add_executable(
    epochkv_server
    src/network/ServerMain.cpp
)

target_link_libraries(epochkv_server PRIVATE epochkv_core)

add_executable(
    epochkv_client
    src/network/ClientMain.cpp
)

target_link_libraries(epochkv_client PRIVATE epochkv_core)

add_executable(
    epochkv_backup
    src/network/BackupMain.cpp
)

target_link_libraries(epochkv_backup PRIVATE epochkv_core)

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


    add_test(
        NAME CommandLine
        COMMAND /bin/sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/CliTests.sh"
        "$<TARGET_FILE:epochkv>"
    )


    add_executable(
        message_io_tests
        tests/MessageIOTests.cpp
    )

    target_link_libraries(message_io_tests PRIVATE epochkv_core)

    add_test(NAME MessageIO COMMAND message_io_tests)

    add_test(
        NAME NetworkCommands
        COMMAND /bin/sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/NetworkTests.sh"
        "$<TARGET_FILE:epochkv_server>"
        "$<TARGET_FILE:epochkv_client>"
    )

    add_executable(replication_io_tests
        tests/ReplicationIOTests.cpp
    )

    target_link_libraries(replication_io_tests PRIVATE epochkv_core)

    add_test(NAME ReplicationIO COMMAND replication_io_tests)


    add_executable(
        backup_store_tests
        tests/BackupStoreTests.cpp
    )

    target_link_libraries(backup_store_tests PRIVATE epochkv_core)

    add_test(NAME BackupStore COMMAND backup_store_tests)


    add_executable(
        backup_protocol_client
        tests/BackupProtocolClient.cpp
    )

    target_link_libraries(backup_protocol_client PRIVATE epochkv_core)

    add_test(
        NAME BackupNetwork
        COMMAND /bin/sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BackupNetworkTests.sh"
        "$<TARGET_FILE:epochkv_backup>"
        "$<TARGET_FILE:backup_protocol_client>"
    )


    add_executable(
        backup_client_tests
        tests/BackupClientTests.cpp
    )

    target_link_libraries(backup_client_tests PRIVATE epochkv_core)

    add_test(
        NAME BackupClient
        COMMAND /bin/sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BackupClientTests.sh"
        "$<TARGET_FILE:epochkv_backup>"
        "$<TARGET_FILE:backup_client_tests>"
        "$<TARGET_FILE:backup_protocol_client>"
    )

endif()
