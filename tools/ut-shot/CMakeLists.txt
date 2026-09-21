# ut-shot -- UTA-0156. Draws a baked map headlessly from given cameras and
# writes each frame as a PPM, so brightness is measured against the original
# game's frames rather than judged by eye. Run by hand; it needs a Vulkan
# device, as the device tier does.
#
# Cli.cpp is the command line as a function; tests/CMakeLists.txt compiles it
# into the unit tests too, as it does ut-bake's and ut-origin's. The options
# are then graded without a Vulkan device, which no CI leg can rely on.
add_executable(ut-shot main.cpp Cli.cpp)
target_link_libraries(ut-shot PRIVATE uta_urender uta_ubundle uta_core)
