# uta_paths: everything of ut-paths but main.cpp, built once and linked by the
# tool, the unit tests and the real-asset tests (UTA-0433). Both
# tools/ut-paths/CMakeLists.txt and tests/CMakeLists.txt include this file,
# because a tests-only build (UTA_BUILD_TOOLS=OFF, CI's real-asset build)
# never reaches the first; whichever runs first defines the target.
if(NOT TARGET uta_paths)
    add_library(uta_paths STATIC "${CMAKE_CURRENT_LIST_DIR}/Cli.cpp" "${CMAKE_CURRENT_LIST_DIR}/Walkable.cpp"
                                 "${CMAKE_CURRENT_LIST_DIR}/Seeds.cpp")
    # tools/ for tools/common/, which uta_tools_common carries only in a build
    # with the tools.
    target_link_libraries(uta_paths PUBLIC uta_ubake)
    target_include_directories(uta_paths PUBLIC "${PROJECT_SOURCE_DIR}/tools")
endif()
