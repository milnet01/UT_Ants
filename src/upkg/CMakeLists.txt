# upkg -- reads Unreal Engine 1 packages. Data in, structures out.

add_library(uta_upkg STATIC
    ByteReader.cpp
    Class.cpp
    Geometry.cpp
    Package.cpp
    Properties.cpp
    Script.cpp
    Sound.cpp
    Texture.cpp)

target_link_libraries(uta_upkg PUBLIC uta_core)

# INV-13, and the reason it is a configure-time assertion rather than a note:
# section 1 says uta_upkg links uta_core and nothing else, and a convenience
# dependency added later is exactly how design rule 2's boundary stops being
# checkable. Naming the one permitted entry, as src/core/CMakeLists.txt does,
# rather than forbidding all -- an empty assertion would fail the moment the
# line above is needed.
#
# This says nothing about design rule 2 itself, which constrains what the
# RUNTIME targets link. ut-ants and ut-ants-server do not exist yet, so the
# link-closure test that rule needs belongs to whichever item first builds one.
get_target_property(_uta_upkg_links uta_upkg LINK_LIBRARIES)
if(NOT _uta_upkg_links STREQUAL "uta_core")
    message(FATAL_ERROR
        "uta_upkg links '${_uta_upkg_links}'. INV-13 in "
        "docs/specs/UTA-0003-package-container.md allows uta_core and nothing "
        "else.")
endif()
unset(_uta_upkg_links)
