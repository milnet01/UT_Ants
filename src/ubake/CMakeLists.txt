# ubake -- bake a map into a .utab, and check an install. Build-time only:
# docs/design.md rule 2 keeps it out of both runtime targets, which run
# `ut-bake` instead (rule 16).
#
# ONE library, as umat is: it has no runtime half to split off.
# docs/specs/UTA-0011-map-baker.md SS 4.1.

add_library(uta_ubake STATIC
    Install.cpp
    Name.cpp
    Bake.cpp)
target_link_libraries(uta_ubake PUBLIC uta_core uta_upkg uta_umat uta_unav_build uta_umap_build
                                       uta_ubundle)

# SS 4.1's list, asserted at configure time in the form every sibling uses. The
# _build libraries are the bake-side halves of unav and umap, which hold the
# graph and room builders. Keeping this library out of the runtime targets is a
# link-closure test on those targets, and belongs to the first item that
# builds one (UTA-0016, SS 9).
get_target_property(_uta_ubake_links uta_ubake LINK_LIBRARIES)
if(NOT _uta_ubake_links STREQUAL
   "uta_core;uta_upkg;uta_umat;uta_unav_build;uta_umap_build;uta_ubundle")
    message(FATAL_ERROR
        "uta_ubake links '${_uta_ubake_links}'. SS 4.1 of "
        "docs/specs/UTA-0011-map-baker.md allows uta_core, uta_upkg, uta_umat, "
        "uta_unav_build, uta_umap_build and uta_ubundle, in that order, and "
        "nothing else.")
endif()
unset(_uta_ubake_links)
