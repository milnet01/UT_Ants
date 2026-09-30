# ut-bench -- UTA-0129. Bakes maps several times and says which step of the
# bake took the time, beside the machine and the build it was measured on
# (docs/specs/UTA-0129-benchmark-tool.md SS 4.3). Run by hand, never as a gate:
# a shared runner's timings are noise.
#
# A TOOL, so docs/design.md rule 2 permits it to link uta_ubake, as ut-bake
# does. Cli.cpp is the command line as a function; tests/CMakeLists.txt
# compiles it into the unit tests too.
add_executable(ut-bench main.cpp Cli.cpp)
target_link_libraries(ut-bench PRIVATE uta_ubake uta_tools_common)

# What compiled it rides beside every figure: a number with no build named
# cannot be compared with a later one. $<CONFIG> and not CMAKE_BUILD_TYPE, which
# a multi-configuration generator leaves empty.
target_compile_definitions(ut-bench PRIVATE
    "UTA_BENCH_COMPILER=\"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\""
    "UTA_BENCH_BUILD_TYPE=\"$<CONFIG>\""
    "UTA_BENCH_SANITIZER=\"${UTA_SANITIZE}\"")

# The commit, resolved on every build as the client's is (UTA-0200), by the
# client's own script and template.
set(_uta_commit_header "${CMAKE_CURRENT_BINARY_DIR}/BuildCommit.h")
set(_uta_commit_script "${PROJECT_SOURCE_DIR}/apps/ut-ants/WriteBuildCommit.cmake")
set(_uta_commit_args
    -D "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -D "TEMPLATE=${PROJECT_SOURCE_DIR}/apps/ut-ants/BuildCommit.h.in"
    -D "OUTPUT=${_uta_commit_header}")
execute_process(COMMAND "${CMAKE_COMMAND}" ${_uta_commit_args} -P "${_uta_commit_script}")
add_custom_target(ut-bench-commit
    COMMAND "${CMAKE_COMMAND}" ${_uta_commit_args} -P "${_uta_commit_script}"
    BYPRODUCTS "${_uta_commit_header}"
    COMMENT "Resolving the build's commit for ut-bench"
    VERBATIM)
add_dependencies(ut-bench ut-bench-commit)
target_include_directories(ut-bench PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
unset(_uta_commit_header)
unset(_uta_commit_script)
unset(_uta_commit_args)
