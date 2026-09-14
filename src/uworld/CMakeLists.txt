# uworld -- the simulation (docs/design.md § Runtime). A runtime library: both
# runtime targets may link it, and rule 4 keeps urender, uaudio and uui out of
# it. Its first code is the collision-tree query UTA-0158 moved out of ubake,
# so the client's camera can stop at walls.

add_library(uta_uworld STATIC
    CollisionQuery.cpp)
target_link_libraries(uta_uworld PUBLIC uta_core uta_ubundle)

# Asserted at configure time, in the form every sibling uses. A link beyond
# these two is a decision to take deliberately, and rule 4 forbids three of
# the candidates outright.
get_target_property(_uta_uworld_links uta_uworld LINK_LIBRARIES)
if(NOT _uta_uworld_links STREQUAL "uta_core;uta_ubundle")
    message(FATAL_ERROR
        "uta_uworld links '${_uta_uworld_links}'. UTA-0158 allows uta_core and "
        "uta_ubundle, in that order, and nothing else; docs/design.md rule 4 "
        "forbids urender, uaudio and uui.")
endif()
unset(_uta_uworld_links)
