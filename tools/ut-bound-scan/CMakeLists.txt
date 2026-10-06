# ut-bound-scan -- UTA-0328. Lists every BSP node whose render bound misses the
# drawn geometry under it, one JSON line each, for UT_MonsterHunt's map checker
# (GAME-0205). UTA-0283 found the cause this hunts for: TheBoat's node 1305.
#
# A TOOL, so docs/design.md rule 2 permits it to link the package reader.
# Cli.cpp and Scan.cpp are compiled into the unit tests too.
add_executable(ut-bound-scan main.cpp Cli.cpp Scan.cpp)
target_link_libraries(ut-bound-scan PRIVATE uta_ubake uta_tools_common)
