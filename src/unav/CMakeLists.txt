# unav -- the navigation graph and the event-wiring graph. Resolves, joins and
# validates; reads no package bytes of its own.
#
# TWO libraries from ONE directory, and the split is forced by docs/design.md
# rather than chosen here. Rule 2 says neither runtime target links upkg, while
# rule 6 makes uai depend on unav -- so a single library linking uta_upkg would
# pull the package reader into ut-ants, breaching rule 2 through this item.
# UTA-0006 SS 4.1.
#
# One directory rather than two, because two would make the split a build-layout
# fact the root build file and every test target binds to, where it is a
# link-closure fact.

add_library(uta_unav STATIC Graphs.cpp)
target_link_libraries(uta_unav PUBLIC uta_core)

add_library(uta_unav_build STATIC Build.cpp)
target_link_libraries(uta_unav_build PUBLIC uta_unav uta_upkg)

# INV-6's first half, asserted at configure time in the form
# src/upkg/CMakeLists.txt uses for UTA-0003's INV-13. BOTH closures are checked,
# because which target may reach upkg is the whole point of the split: uta_unav
# is what a runtime target links, and a convenience dependency added to it later
# is exactly how design rule 2's boundary stops being checkable. Each assertion
# names the permitted entries rather than forbidding all, so it does not fail the
# moment a line above is needed.
get_target_property(_uta_unav_links uta_unav LINK_LIBRARIES)
if(NOT _uta_unav_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_unav links '${_uta_unav_links}'. INV-6 in "
        "docs/specs/UTA-0006-navigation-and-wiring-graphs.md allows uta_core and "
        "nothing else: it is the library a runtime target links, and "
        "docs/design.md rule 2 keeps the package reader out of the game.")
endif()
unset(_uta_unav_links)

get_target_property(_uta_unav_build_links uta_unav_build LINK_LIBRARIES)
if(NOT _uta_unav_build_links STREQUAL "uta_unav;uta_upkg")
    message(FATAL_ERROR
        "uta_unav_build links '${_uta_unav_build_links}'. UTA-0006 SS 4.1 allows "
        "uta_unav and uta_upkg, in that order, and nothing else. It is bake-side "
        "only: no runtime target may link it.")
endif()
unset(_uta_unav_build_links)

# As src/upkg/CMakeLists.txt: neither assertion above says anything about design
# rule 2 itself, which constrains what the RUNTIME targets link. ut-ants does not
# exist yet, so the link-closure test that rule names belongs to whichever item
# first builds one -- UTA-0006 SS 10 keeps the row.
