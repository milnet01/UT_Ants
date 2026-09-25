# ut-dump is a TOOL, not a runtime target, so docs/design.md rule 2 permits it
# to link the package reader. uta_unav_build is the bake-side library that
# already links uta_upkg; linking it is what gives this both graphs without a
# second path from a runtime target into upkg.
#
# Cli.cpp is the command line as a function; tests/CMakeLists.txt compiles it
# into the unit tests too, as it does ut-origin's.
add_executable(ut-dump main.cpp Cli.cpp)
target_link_libraries(ut-dump PRIVATE uta_ubake uta_unav_build uta_tools_common)
