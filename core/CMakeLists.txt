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

add_executable(qodeloc-core
    src/main.cpp
)

target_compile_features(qodeloc-core PRIVATE cxx_std_23)
target_compile_definitions(qodeloc-core PRIVATE
    QODELOC_CORE_VERSION="${QODELOC_CORE_VERSION_RAW}"
    QODELOC_CORE_PROJECT_VERSION="${QODELOC_CORE_PROJECT_VERSION}"
    QODELOC_CORE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
)
target_link_libraries(qodeloc-core PRIVATE spdlog::spdlog_header_only)

if(MSVC)
    target_compile_options(qodeloc-core PRIVATE /W4 /permissive-)
else()
    target_compile_options(qodeloc-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_test(NAME qodeloc-core-smoke COMMAND qodeloc-core)
endif()
