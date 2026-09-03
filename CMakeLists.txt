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

# src/ holds no sources yet -- UTA-0002 onward add them. Adding an empty
# subdirectory here would be a build rule for nothing.

if(UTA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
