# uworld -- the simulation (docs/design.md § Runtime). A runtime library: both
# runtime targets may link it, and rule 4 keeps urender, uaudio and uui out of
# it. Its first code is the collision-tree query UTA-0158 moved out of ubake,
# so the client's camera can stop at walls. UTA-0457 adds a body's sweeps,
# which Jolt Physics does; no Jolt type appears in a uworld header, so it
# links privately. UTA-0017 adds the movement rules and the fixed tick.

add_library(uta_uworld STATIC
    BodyCollision.cpp
    CollisionMesh.cpp
    CollisionQuery.cpp
    FixedTick.cpp
    Movement.cpp)
target_link_libraries(uta_uworld PUBLIC uta_core uta_ubundle PRIVATE Jolt)

# Asserted at configure time, in the form every sibling uses. A link beyond
# these three is a decision to take deliberately, and rule 4 forbids three of
# the candidates outright.
get_target_property(_uta_uworld_links uta_uworld LINK_LIBRARIES)
if(NOT _uta_uworld_links STREQUAL "uta_core;uta_ubundle;Jolt")
    message(FATAL_ERROR
        "uta_uworld links '${_uta_uworld_links}'. UTA-0158 allows uta_core and "
        "uta_ubundle, and UTA-0457 Jolt, in that order, and nothing else; "
        "docs/design.md rule 4 forbids urender, uaudio and uui.")
endif()
unset(_uta_uworld_links)
