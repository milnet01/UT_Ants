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
#
# uta_umap_build is NOT here yet: buildRoomMap is the other half of UTA-0007
# and lands with SS 4.4's sampling. Its closure assertion arrives with it. The
# assertion below is INV-5's first half, which is the half that guards design
# rule 2 -- uta_umap is the library a runtime target links.

add_library(uta_umap STATIC Rooms.cpp)
target_link_libraries(uta_umap PUBLIC uta_core)

# INV-5's first half, asserted at configure time in the form
# src/unav/CMakeLists.txt uses for UTA-0006's INV-6. The assertion names the
# permitted entries rather than forbidding all, so it does not fail the moment
# a line above is legitimately needed. A convenience dependency added to
# uta_umap later is exactly how design rule 2's boundary stops being
# checkable, and nothing else in the build would fail.
get_target_property(_uta_umap_links uta_umap LINK_LIBRARIES)
if(NOT _uta_umap_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_umap links '${_uta_umap_links}'. INV-5 in "
        "docs/specs/UTA-0007-room-partition-and-lookup.md allows uta_core and "
        "nothing else: it is the library a runtime target links, and "
        "docs/design.md rule 2 keeps the package reader out of the game.")
endif()
unset(_uta_umap_links)
