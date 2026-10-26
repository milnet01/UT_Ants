# uta_bench: ut-bench's command line and frame workload as functions, built
# once and linked by the tool, the unit tests and the device tests (UTA-0433).
# Both tools/ut-bench/CMakeLists.txt and tests/CMakeLists.txt include this
# file, because a tests-only build (UTA_BUILD_TOOLS=OFF) never reaches the
# first; whichever runs first defines the target.
if(NOT TARGET uta_bench)
    add_library(uta_bench STATIC "${CMAKE_CURRENT_LIST_DIR}/Cli.cpp" "${CMAKE_CURRENT_LIST_DIR}/Frame.cpp")
    # uta_urender for the frame workload, which draws on the surfaceless path
    # as ut-shot does. tools/ for tools/common/, which uta_tools_common carries
    # only in a build with the tools.
    target_link_libraries(uta_bench PUBLIC uta_ubake uta_urender uta_ubundle)
    target_include_directories(uta_bench PUBLIC "${PROJECT_SOURCE_DIR}/tools")
endif()
