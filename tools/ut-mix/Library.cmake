# uta_mix: ut-mix's scenario, built once and linked by the tool and the
# real-asset tests (UTA-0056 SS 4.1). Both tools/ut-mix/CMakeLists.txt and
# tests/CMakeLists.txt include this file, because a tests-only build never
# reaches the first; whichever runs first defines the target.
if(NOT TARGET uta_mix)
    add_library(uta_mix STATIC "${CMAKE_CURRENT_LIST_DIR}/Scenario.cpp")
    # A tool's library, so rule 2 lets it read packages.
    target_link_libraries(uta_mix PUBLIC uta_uaudio uta_upkg uta_core)
    target_include_directories(uta_mix PUBLIC "${PROJECT_SOURCE_DIR}/tools")
endif()
