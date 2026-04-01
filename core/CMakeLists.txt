cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(QODELOC_VERSION_FILE "${CMAKE_CURRENT_LIST_DIR}/../VERSION")
if(NOT EXISTS "${QODELOC_VERSION_FILE}")
    message(FATAL_ERROR "Missing QodeLoc version file: ${QODELOC_VERSION_FILE}")
endif()

file(READ "${QODELOC_VERSION_FILE}" QODELOC_CORE_VERSION_RAW)
string(STRIP "${QODELOC_CORE_VERSION_RAW}" QODELOC_CORE_VERSION_RAW)

if(NOT QODELOC_CORE_VERSION_RAW MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+([.-][0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$")
    message(FATAL_ERROR "Invalid QodeLoc version in ${QODELOC_VERSION_FILE}: ${QODELOC_CORE_VERSION_RAW}")
endif()

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _qodeloc_version_match "${QODELOC_CORE_VERSION_RAW}")
set(QODELOC_CORE_PROJECT_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

project(QodeLocCore VERSION ${QODELOC_CORE_PROJECT_VERSION} LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(spdlog CONFIG REQUIRED)
find_package(duckdb CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tree-sitter CONFIG REQUIRED)
find_package(tree-sitter-cpp CONFIG REQUIRED)

set(QODELOC_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(QODELOC_CORE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")

function(qodeloc_core_apply_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(qodeloc_core_add_module module_name)
    set(target_name "qodeloc_${module_name}")
    add_library(${target_name} STATIC ${ARGN})
    set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${module_name}")
    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_include_directories(${target_name} PUBLIC "${QODELOC_CORE_INCLUDE_DIR}")
    qodeloc_core_apply_warnings(${target_name})
    add_library(qodeloc::${module_name} ALIAS ${target_name})
endfunction()

qodeloc_core_add_module(api "${QODELOC_CORE_SOURCE_DIR}/api.cpp")
qodeloc_core_add_module(config "${QODELOC_CORE_SOURCE_DIR}/config.cpp")
qodeloc_core_add_module(embedder "${QODELOC_CORE_SOURCE_DIR}/embedder.cpp")
qodeloc_core_add_module(indexer "${QODELOC_CORE_SOURCE_DIR}/indexer.cpp")
target_sources(qodeloc_indexer PRIVATE "${QODELOC_CORE_SOURCE_DIR}/git_watcher.cpp")
target_sources(qodeloc_indexer PRIVATE "${QODELOC_CORE_SOURCE_DIR}/hierarchy.cpp")
qodeloc_core_add_module(llm
    "${QODELOC_CORE_SOURCE_DIR}/llm.cpp"
    "${QODELOC_CORE_SOURCE_DIR}/prompt_builder.cpp"
)
qodeloc_core_add_module(parser "${QODELOC_CORE_SOURCE_DIR}/parser.cpp")
qodeloc_core_add_module(retriever "${QODELOC_CORE_SOURCE_DIR}/retriever.cpp")
qodeloc_core_add_module(storage
    "${QODELOC_CORE_SOURCE_DIR}/storage.cpp"
)

target_link_libraries(qodeloc_parser PUBLIC
    tree-sitter::tree-sitter
    tree-sitter-cpp::tree-sitter-cpp
)

target_link_libraries(qodeloc_config PUBLIC
    nlohmann_json::nlohmann_json
)

target_link_libraries(qodeloc_embedder PUBLIC
    qodeloc::config
    Boost::system
    nlohmann_json::nlohmann_json
)

target_link_libraries(qodeloc_llm PUBLIC
    qodeloc::config
    Boost::system
    nlohmann_json::nlohmann_json
)

target_link_libraries(qodeloc_indexer PUBLIC
    qodeloc::config
    qodeloc::embedder
    qodeloc::parser
    qodeloc::storage
    spdlog::spdlog_header_only
)

target_link_libraries(qodeloc_retriever PUBLIC
    qodeloc::config
    qodeloc::embedder
    qodeloc::indexer
    qodeloc::storage
)

target_link_libraries(qodeloc_storage PUBLIC qodeloc::config duckdb::duckdb)

add_executable(qodeloc-core
    src/main.cpp
)

target_compile_features(qodeloc-core PRIVATE cxx_std_23)
target_compile_definitions(qodeloc-core PRIVATE
    QODELOC_CORE_VERSION="${QODELOC_CORE_VERSION_RAW}"
    QODELOC_CORE_PROJECT_VERSION="${QODELOC_CORE_PROJECT_VERSION}"
    QODELOC_CORE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
target_include_directories(qodeloc-core PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
target_link_libraries(qodeloc-core PRIVATE
    qodeloc::api
    qodeloc::config
    qodeloc::embedder
    qodeloc::indexer
    qodeloc::llm
    qodeloc::parser
    qodeloc::retriever
    qodeloc::storage
    spdlog::spdlog_header_only
)

qodeloc_core_apply_warnings(qodeloc-core)

if(BUILD_TESTING)
    enable_testing()
    add_test(NAME qodeloc-core-smoke COMMAND qodeloc-core)

    find_package(GTest CONFIG REQUIRED)

    add_executable(qodeloc-parser-tests
        tests/parser_test.cpp
    )

    target_compile_features(qodeloc-parser-tests PRIVATE cxx_std_23)
    target_compile_definitions(qodeloc-parser-tests PRIVATE
        QODELOC_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testdata"
    )
    target_include_directories(qodeloc-parser-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-parser-tests PRIVATE
        qodeloc::parser
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-parser-tests)

    add_test(NAME qodeloc-parser COMMAND qodeloc-parser-tests)

    add_executable(qodeloc-storage-tests
        tests/storage_test.cpp
    )

    target_compile_features(qodeloc-storage-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-storage-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-storage-tests PRIVATE
        qodeloc::storage
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-storage-tests)

    add_test(NAME qodeloc-storage COMMAND qodeloc-storage-tests)

    add_executable(qodeloc-embedder-tests
        tests/embedder_test.cpp
    )

    target_compile_features(qodeloc-embedder-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-embedder-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-embedder-tests PRIVATE
        qodeloc::embedder
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-embedder-tests)

    add_test(NAME qodeloc-embedder COMMAND qodeloc-embedder-tests)

    add_executable(qodeloc-llm-tests
        tests/llm_test.cpp
    )

    target_compile_features(qodeloc-llm-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-llm-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-llm-tests PRIVATE
        qodeloc::llm
        Boost::system
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-llm-tests)

    add_test(NAME qodeloc-llm COMMAND qodeloc-llm-tests)

    add_executable(qodeloc-prompt-builder-tests
        tests/prompt_builder_test.cpp
    )

    target_compile_features(qodeloc-prompt-builder-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-prompt-builder-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-prompt-builder-tests PRIVATE
        qodeloc::llm
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-prompt-builder-tests)

    add_test(NAME qodeloc-prompt-builder COMMAND qodeloc-prompt-builder-tests)

    add_executable(qodeloc-indexer-tests
        tests/indexer_test.cpp
    )

    target_compile_features(qodeloc-indexer-tests PRIVATE cxx_std_23)
    target_compile_definitions(qodeloc-indexer-tests PRIVATE
        QODELOC_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testdata"
    )
    target_include_directories(qodeloc-indexer-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-indexer-tests PRIVATE
        qodeloc::indexer
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-indexer-tests)

    add_test(NAME qodeloc-indexer COMMAND qodeloc-indexer-tests)

    add_executable(qodeloc-retriever-tests
        tests/retriever_test.cpp
    )

    target_compile_features(qodeloc-retriever-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-retriever-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-retriever-tests PRIVATE
        qodeloc::retriever
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-retriever-tests)

    add_test(NAME qodeloc-retriever COMMAND qodeloc-retriever-tests)

    add_executable(qodeloc-git-watcher-tests
        tests/git_watcher_test.cpp
    )

    target_compile_features(qodeloc-git-watcher-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-git-watcher-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-git-watcher-tests PRIVATE
        qodeloc::indexer
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-git-watcher-tests)

    add_test(NAME qodeloc-git-watcher COMMAND qodeloc-git-watcher-tests)

    add_executable(qodeloc-indexer-update-tests
        tests/indexer_update_test.cpp
    )

    target_compile_features(qodeloc-indexer-update-tests PRIVATE cxx_std_23)
    target_compile_definitions(qodeloc-indexer-update-tests PRIVATE
        QODELOC_TEST_REPOS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testdata/repos"
    )
    target_include_directories(qodeloc-indexer-update-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-indexer-update-tests PRIVATE
        qodeloc::indexer
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-indexer-update-tests)

    add_test(NAME qodeloc-indexer-update COMMAND qodeloc-indexer-update-tests)

    add_executable(qodeloc-hierarchy-tests
        tests/hierarchy_test.cpp
    )

    target_compile_features(qodeloc-hierarchy-tests PRIVATE cxx_std_23)
    target_include_directories(qodeloc-hierarchy-tests PRIVATE "${QODELOC_CORE_INCLUDE_DIR}")
    target_link_libraries(qodeloc-hierarchy-tests PRIVATE
        qodeloc::indexer
        GTest::gmock_main
    )

    qodeloc_core_apply_warnings(qodeloc-hierarchy-tests)

    add_test(NAME qodeloc-hierarchy COMMAND qodeloc-hierarchy-tests)
endif()
