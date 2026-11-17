# ut-mix -- UTA-0056 SS 4.9. Renders the flak-announcer scenario to a WAV
# file and prints SS 4.7's figures. Run by hand.
include("${CMAKE_CURRENT_SOURCE_DIR}/Library.cmake")
add_executable(ut-mix main.cpp)
target_link_libraries(ut-mix PRIVATE uta_mix uta_tools_common)
