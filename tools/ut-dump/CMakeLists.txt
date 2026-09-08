# ut-dump is a TOOL, not a runtime target, so docs/design.md rule 2 permits it
# to link the package reader. uta_unav_build is the bake-side library that
# already links uta_upkg; linking it is what gives this both graphs without a
# second path from a runtime target into upkg.
add_executable(ut-dump main.cpp)
target_link_libraries(ut-dump PRIVATE uta_unav_build)
