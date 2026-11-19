# uaudio -- sound playback and mixing (docs/design.md § The parts,
# docs/specs/UTA-0056-audio-mixer.md). A runtime library with no SDL in it,
# as uinput: the client opens the device and hands the mixer a buffer to
# fill, so the unit tests build on the leg that has no SDL.

add_library(uta_uaudio STATIC Mixer.cpp SoundBank.cpp Spatial.cpp Wav.cpp)
target_include_directories(uta_uaudio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(uta_uaudio PUBLIC uta_core)

# INV-11, asserted at configure time in the form every sibling uses: uaudio
# links core and nothing else -- no SDL.
get_target_property(_uta_uaudio_links uta_uaudio LINK_LIBRARIES)
if(NOT _uta_uaudio_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_uaudio links '${_uta_uaudio_links}'. UTA-0056 INV-11 allows uta_core "
        "and nothing else; the client owns the SDL device.")
endif()
unset(_uta_uaudio_links)
