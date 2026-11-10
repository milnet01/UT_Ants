# uinput -- devices to actions (docs/design.md § The parts, UTA-0018). A
# runtime library with no SDL in it: the client reads the devices and hands
# their state over, so the unit tests build on the leg that has no SDL.

add_library(uta_uinput STATIC Input.cpp)
target_include_directories(uta_uinput PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

# Asserted at configure time, in the form every sibling uses: uinput links
# nothing, and design rule 12 keeps uworld from linking it.
get_target_property(_uta_uinput_links uta_uinput LINK_LIBRARIES)
if(_uta_uinput_links)
    message(FATAL_ERROR
        "uta_uinput links '${_uta_uinput_links}'. UTA-0018 has it link nothing.")
endif()
unset(_uta_uinput_links)
