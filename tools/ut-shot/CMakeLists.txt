# ut-shot -- UTA-0156. Draws a baked map headlessly from given cameras and
# writes each frame as a PPM, so brightness is measured against the original
# game's frames rather than judged by eye. Run by hand; it needs a Vulkan
# device, as the device tier does.
add_executable(ut-shot main.cpp)
target_link_libraries(ut-shot PRIVATE uta_urender uta_ubundle uta_core)
