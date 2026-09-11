# ubundle -- the .utab container: the header, the section table and the origin
# field. Owns the file layout and its version, never the meaning of a
# section's contents.
#
# ONE library, unlike umap and unav. Those two split because a builder half
# must reach uta_upkg; nothing here reads a package byte, so there is no
# bake-side half to separate.

# Bundle.cpp is the framing; each section's codec is a file of its own, because
# each changes for its own reason (UTA-0091). Codec.h and Sections.h are
# internal to this library.
add_library(uta_ubundle STATIC
    Bundle.cpp
    RoomSection.cpp
    NavSection.cpp
    WiringSection.cpp
    TextureSection.cpp
    MaterialSection.cpp
    GeometrySection.cpp
    PlacementSection.cpp
    LightSection.cpp
    MoverSection.cpp)
target_link_libraries(uta_ubundle PUBLIC uta_core uta_umap uta_unav)

# INV-10, asserted at configure time in the form src/umap/CMakeLists.txt uses
# for UTA-0007's INV-5. ut-ants links this library in order to load a bundle
# (UTA-0016), so a convenience dependency added here is exactly how
# docs/design.md rule 2's boundary stops being checkable -- the package reader
# reaches the game and nothing else in the build fails.
#
# uta_umap and uta_unav rather than uta_umap_build and uta_unav_build: those
# two are the bake-side libraries, and their own CMakeLists each already
# assert that they reach uta_upkg.
#
# The assertion names the permitted entries rather than forbidding all, so it
# does not fail the moment a line above is legitimately needed.
get_target_property(_uta_ubundle_links uta_ubundle LINK_LIBRARIES)
if(NOT _uta_ubundle_links STREQUAL "uta_core;uta_umap;uta_unav")
    message(FATAL_ERROR
        "uta_ubundle links '${_uta_ubundle_links}'. INV-10 in "
        "docs/specs/UTA-0008-bundle-container-and-origin.md allows uta_core, "
        "uta_umap and uta_unav, in that order, and nothing else: it is a "
        "library a runtime target links, and docs/design.md rule 2 keeps the "
        "package reader out of the game.")
endif()
unset(_uta_ubundle_links)
