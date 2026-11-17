# uui -- the menus and the HUD (docs/design.md § The parts). Its first code is
# the weapon wheel's pointing and ring, docs/specs/UTA-0020-weapon-wheel.md
# SS 4.6. Then the landing marker and the take-off pitch,
# docs/specs/UTA-0021-landing-marker.md SS 4.3 and 4.4.

add_library(uta_uui STATIC Wheel.cpp Platforming.cpp)
target_include_directories(uta_uui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(uta_uui PUBLIC uta_core uta_uinput uta_urender)

# Asserted at configure time, in the form every sibling uses. uui draws, so
# design rules 2 and 4 keep it out of uworld and ugame, whose own assertions
# refuse it; this one keeps uui from reaching into the simulation.
get_target_property(_uta_uui_links uta_uui LINK_LIBRARIES)
if(NOT _uta_uui_links STREQUAL "uta_core;uta_uinput;uta_urender")
    message(FATAL_ERROR
        "uta_uui links '${_uta_uui_links}'. UTA-0020 allows uta_core, "
        "uta_uinput and uta_urender, in that order, and nothing else.")
endif()
unset(_uta_uui_links)
