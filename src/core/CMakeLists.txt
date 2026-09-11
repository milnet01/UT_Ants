# core -- docs/design.md rule 1: depends on nothing beyond the C++ standard
# library. That rule is what every later part inherits its independence from,
# so it is asserted below rather than trusted.

find_package(Threads REQUIRED)

add_library(uta_core STATIC
    Error.cpp
    Log.cpp
    FileSystem.cpp
    Jobs.cpp
    Md5.cpp
    Sha256.cpp)

target_include_directories(uta_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Threads::Threads is how CMake spells the standard library's own threading
# support -- what std::thread needs on GCC and Clang. It is not an exception
# to rule 1.
target_link_libraries(uta_core PUBLIC Threads::Threads)

# INV-14. An empty assertion would fail the moment the line above is needed,
# so the check names the one permitted entry instead of forbidding all.
get_target_property(_uta_core_links uta_core LINK_LIBRARIES)
if(NOT _uta_core_links STREQUAL "Threads::Threads")
    message(FATAL_ERROR
        "uta_core links '${_uta_core_links}'. docs/design.md rule 1 allows "
        "Threads::Threads and nothing else -- see INV-14 in "
        "docs/specs/UTA-0002-core-foundations.md.")
endif()
unset(_uta_core_links)
