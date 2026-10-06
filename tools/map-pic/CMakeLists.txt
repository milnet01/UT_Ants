# map-pic -- UTA-0316. Saves the picture a map ships with as a PNG, for
# UT_MonsterHunt's server launcher. A tool, so docs/design.md rule 2 lets it
# link the package reader through uta_ubake.
#
# Png.cpp is the viewer's PNG encoder, compiled in rather than linked: it is
# the one translation unit instantiating the vendored stb_image_write, and it
# sits outside core by design.md rule 1 (apps/ut-ants/Png.h says why).
#
# Cli.cpp is the command line as a function; tests/CMakeLists.txt compiles it
# into the unit tests too, as it does ut-dump's.
add_executable(map-pic main.cpp Cli.cpp "${PROJECT_SOURCE_DIR}/apps/ut-ants/Png.cpp")
target_include_directories(map-pic PRIVATE "${PROJECT_SOURCE_DIR}/apps"
                                           "${PROJECT_SOURCE_DIR}/third_party/stb")
target_link_libraries(map-pic PRIVATE uta_ubake uta_core)
