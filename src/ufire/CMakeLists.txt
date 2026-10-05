# ufire -- UT99's FireTexture animation, shared by the bake's still and the
# renderer's replay. docs/specs/UTA-0286-replayed-fire-textures.md SS 4.1.
#
# A runtime target links it through urender, so it reaches nothing but
# uta_core: docs/design.md rule 2 keeps the package reader out of the game.

add_library(uta_ufire STATIC Fire.cpp)
target_link_libraries(uta_ufire PUBLIC uta_core)

# SS 4.1, asserted at configure time in the form every sibling uses.
get_target_property(_uta_ufire_links uta_ufire LINK_LIBRARIES)
if(NOT _uta_ufire_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_ufire links '${_uta_ufire_links}'. SS 4.1 of "
        "docs/specs/UTA-0286-replayed-fire-textures.md allows uta_core and "
        "nothing else: urender links it into both runtime targets.")
endif()
unset(_uta_ufire_links)
