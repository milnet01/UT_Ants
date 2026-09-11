# ut-paths is a TOOL, not a runtime target, so docs/design.md rule 2 permits it
# to link the package reader through uta_ubake --
# docs/specs/UTA-0121-bot-path-seeds.md SS 4.1.
#
# Everything but main.cpp is compiled into the unit tests too, as ut-bake's
# Cli.cpp is (tests/CMakeLists.txt).
add_executable(ut-paths main.cpp Cli.cpp Trace.cpp Walkable.cpp Seeds.cpp)
target_link_libraries(ut-paths PRIVATE uta_ubake uta_tools_common)
