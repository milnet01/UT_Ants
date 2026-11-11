# ugame -- the rules (docs/design.md § The parts, rule 8). Its first code is
# the weapon set and its tick, docs/specs/UTA-0019-weapon-set.md. Client and
# server compile it alike (rule 9), so it links the simulation and nothing
# that draws, plays or shows.

add_library(uta_ugame STATIC
    Fire.cpp
    Game.cpp
    WeaponSet.cpp)
target_link_libraries(uta_ugame PUBLIC uta_core uta_uworld)

# SS 4.2's shipped sets. There is no install step yet, so this is the
# repository's own weapons/ directory, as urecipe does for recipes/.
target_compile_definitions(uta_ugame PRIVATE
    UTA_SHIPPED_WEAPONS_DIR="${PROJECT_SOURCE_DIR}/weapons")

# Asserted at configure time, in the form every sibling uses: rule 2 keeps
# urender, uaudio and uui out of the server, and ugame is in its closure.
get_target_property(_uta_ugame_links uta_ugame LINK_LIBRARIES)
if(NOT _uta_ugame_links STREQUAL "uta_core;uta_uworld")
    message(FATAL_ERROR
        "uta_ugame links '${_uta_ugame_links}'. UTA-0019 allows uta_core and "
        "uta_uworld and nothing else; docs/design.md rule 2 keeps the "
        "server's closure free of urender, uaudio and uui.")
endif()
unset(_uta_ugame_links)
