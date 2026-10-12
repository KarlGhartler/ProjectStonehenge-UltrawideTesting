cmake_minimum_required(VERSION 3.24)

project(AC04Port
    VERSION 0.1.0
    DESCRIPTION "Native PC port of Ace Combat 04 (SLUS_201.52)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The skeleton targets Windows first (ADR-005). Linux support is added after the port is complete.")
endif()

option(AC04_BUILD_TESTS "Build the doctest unit tests" ON)

# Build version shown in the session log: project version + git commit, if available.
find_package(Git QUIET)
set(AC04_GIT_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(AC04_GIT_DIRTY)
        string(APPEND AC04_GIT_COMMIT "-dirty")
    endif()
endif()
configure_file(src/common/build_info.h.in "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

# Common compile settings for our own targets (never applied to dependencies).
add_library(ac04_options INTERFACE)
target_compile_options(ac04_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP)
target_compile_definitions(ac04_options INTERFACE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    # Development build = every configuration except Release (CLAUDE.md, debug overlay default).
    AC04_DEV_BUILD=$<IF:$<CONFIG:Release>,0,1>)
target_include_directories(ac04_options INTERFACE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")

add_subdirectory(src)

if(AC04_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
