# Tools, in docs/design.md's sense: programs that are not the client or the
# server, and so may link the package reader under rule 2.

# What more than one tool shares, header-only: tools/common/Json.h
# (docs/specs/UTA-0121-bot-path-seeds.md SS 4.8).
add_library(uta_tools_common INTERFACE)
target_include_directories(uta_tools_common INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(ut-dump)
add_subdirectory(ut-bake)
add_subdirectory(ut-paths)
