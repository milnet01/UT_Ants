cmake_minimum_required(VERSION 3.28)

project(UT_Ants
    VERSION 0.0.1
    DESCRIPTION "A from-scratch engine that bakes Unreal Tournament maps into modernised versions of themselves"
    LANGUAGES CXX)

# C++23 rather than cpp.md's C++20 floor. The reason is std::expected, which
# docs/design.md makes the error model across every module boundary; without
# it the floor would buy a hand-rolled equivalent in core and nothing else.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# docs/design.md § The stack rules out anything older. Checked here rather
# than left to a confusing template error four thousand lines into a build.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "GCC 14 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION}); see docs/design.md")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "Clang 18 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION}); see docs/design.md")
elseif(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.40)
    message(FATAL_ERROR "MSVC 19.40 / Visual Studio 2022 17.10 or newer is required (found ${CMAKE_CXX_COMPILER_VERSION}); see docs/design.md")
endif()

# Windows is a first-class target (docs/design.md § The stack), so MSVC's two
# defaults that differ from every other compiler here are set once, globally.
if(MSVC)
    # Without /utf-8 MSVC reads sources in the machine's ANSI code page and the
    # non-ASCII in this project's own comments becomes a warning storm or a
    # mojibake string literal. It is not a style choice: the other two
    # compilers assume UTF-8 and this one has to be told.
    add_compile_options(/utf-8)
    # The traditional preprocessor is not conforming and breaks variadic macros
    # the same way for every library that uses them, Catch2 included.
    add_compile_options(/Zc:preprocessor)
endif()

# The job system is the first threaded code in this project, and ordinary
# test runs do not see data races. This is what INV-13 of
# docs/specs/UTA-0002-core-foundations.md is measured with.
#
# `thread` and nothing else. AddressSanitizer is not offered here because MSVC
# spells it /fsanitize=address and takes no link flag, so a second value would
# need a second spelling for a build nothing in this repository runs.
set(UTA_SANITIZE "" CACHE STRING "Sanitizer to build with: empty or thread")
set_property(CACHE UTA_SANITIZE PROPERTY STRINGS "" "thread")

if(UTA_SANITIZE STREQUAL "thread")
    if(MSVC)
        message(FATAL_ERROR
            "UTA_SANITIZE=thread is not available on MSVC, which has no "
            "ThreadSanitizer. Refusing rather than building something that "
            "does not check what it claims.")
    endif()
    # Applied at the TOP LEVEL on purpose: uta_core, the test executable and
    # the Catch2 the build fetches must all be instrumented. Instrumenting the
    # library alone yields a leg that reports nothing about the job tests,
    # which is what INV-13 is actually about.
    add_compile_options(-fsanitize=thread -g -O1)
    add_link_options(-fsanitize=thread)
elseif(NOT UTA_SANITIZE STREQUAL "")
    message(FATAL_ERROR
        "UTA_SANITIZE is '${UTA_SANITIZE}'; the accepted values are '' and 'thread'.")
endif()

option(UTA_BUILD_TESTS "Build the test suite" ON)

# The second test tier. OFF by default so a clone with no Unreal Tournament
# builds and tests clean, which is what S7 is measured on. Turning it on
# without a path is a configure error rather than a silently empty run.
option(UTA_REAL_ASSET_TESTS "Also run tests against a real Unreal Tournament install" OFF)
set(UTA_UT_INSTALL_DIR "" CACHE PATH "Unreal Tournament install to run UTA_REAL_ASSET_TESTS against")

if(UTA_REAL_ASSET_TESTS AND NOT UTA_UT_INSTALL_DIR)
    message(FATAL_ERROR
        "UTA_REAL_ASSET_TESTS is ON but UTA_UT_INSTALL_DIR is empty. "
        "Point it at an Unreal Tournament install, or leave both at their defaults.")
endif()

add_subdirectory(src)

if(UTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
