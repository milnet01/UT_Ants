# ut-bake is a TOOL, not a runtime target, so docs/design.md rule 2 permits it
# to link the package reader through uta_ubake. Both runtime targets run it
# rather than linking uta_ubake themselves (rule 16).
#
# Cli.cpp is the command line as a function. tests/CMakeLists.txt compiles it
# into the unit tests too, so the command line is tested without a process
# (docs/specs/UTA-0011-map-baker.md SS 4.1).
add_executable(ut-bake main.cpp Cli.cpp)
target_link_libraries(ut-bake PRIVATE uta_ubake)
