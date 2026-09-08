# umap -- the level map: rooms from a level's own BSP zones, and the rule for
# which room a point is in. Reads no package bytes of its own.
#
# TWO libraries from ONE directory, and the split is forced by docs/design.md
# rather than chosen here, exactly as src/unav/CMakeLists.txt is. Rule 2 says
# neither runtime target links upkg, while rule 17 gives umap the room types
# and ubundle their bytes -- so a single library linking uta_upkg would pull
# the package reader into ut-ants through this item. UTA-0007 SS 4.1.
#
# One directory rather than two, because two would make the split a
# build-layout fact the root build file and every test target binds to, where
# it is a link-closure fact.

add_library(uta_umap STATIC Rooms.cpp)
target_link_libraries(uta_umap PUBLIC uta_core)

add_library(uta_umap_build STATIC Build.cpp)
target_link_libraries(uta_umap_build PUBLIC uta_umap uta_upkg)

# INV-5, asserted at configure time in the form src/unav/CMakeLists.txt uses
# for UTA-0006's INV-6. BOTH closures are checked, because which target may
# reach upkg is the whole point of the split: uta_umap is what a runtime target
# links, and a convenience dependency added to it later is exactly how design
# rule 2's boundary stops being checkable, with nothing else in the build
# failing. Each assertion names the permitted entries rather than forbidding
# all, so it does not fail the moment a line above is legitimately needed.
get_target_property(_uta_umap_links uta_umap LINK_LIBRARIES)
if(NOT _uta_umap_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_umap links '${_uta_umap_links}'. INV-5 in "
        "docs/specs/UTA-0007-room-partition-and-lookup.md allows uta_core and "
        "nothing else: it is the library a runtime target links, and "
        "docs/design.md rule 2 keeps the package reader out of the game.")
endif()
unset(_uta_umap_links)

get_target_property(_uta_umap_build_links uta_umap_build LINK_LIBRARIES)
if(NOT _uta_umap_build_links STREQUAL "uta_umap;uta_upkg")
    message(FATAL_ERROR
        "uta_umap_build links '${_uta_umap_build_links}'. INV-5 in "
        "docs/specs/UTA-0007-room-partition-and-lookup.md allows uta_umap and "
        "uta_upkg, in that order, and nothing else. It is bake-side only: no "
        "runtime target may link it.")
endif()
unset(_uta_umap_build_links)
