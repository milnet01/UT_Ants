# urender -- draws a baked bundle with Vulkan 1.3.
# docs/specs/UTA-0014-vulkan-draw-path.md.

# SS 4.2: ADR-0007's route 3 -- found, never fetched, on both platforms.
# REQUIRED, so a leg without the headers, the loader or glslc fails HERE, at
# configure, rather than registering no device test and reporting green over a
# renderer nothing ran (SS 7). The driver is not found: it is the machine's.
#
# GLOBAL, so the one find serves tests/ too: an imported target is otherwise
# visible only in this directory, and a second find would be a second place
# the version floor could drift.
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc GLOBAL)

add_library(uta_urender STATIC
    Device.cpp
    Resources.cpp
    Placement.cpp
    Pipelines.cpp
    Materials.cpp
    Geometry.cpp
    Lights.cpp
    Clusters.cpp
    Frame.cpp)
# Vulkan and glm are PRIVATE: Renderer.h names neither, so a consumer compiles
# with no Vulkan headers on its path (INV-2).
target_link_libraries(uta_urender PUBLIC uta_core uta_ubundle PRIVATE Vulkan::Vulkan glm::glm)

# -- Shaders ---------------------------------------------------------------
#
# SS 4.2: GLSL is compiled to SPIR-V at build time, never committed, and
# embedded into the binary as a generated header, so nothing loads a shader
# file at run time and there is no search path for packaging to bind to.
#
# glslc writes a dependency file (-MD), so an edit to an #included file
# rebuilds every shader that includes it without the edges written out here.

set(UTA_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders" CACHE INTERNAL "urender's shared GLSL")
set(UTA_EMBED_SPIRV "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedSpirv.cmake" CACHE INTERNAL "SPIR-V to header")

# uta_embed_shaders(<target> <source>...)
#
# Compiles each source, relative to the calling directory, and adds
# `<name>.spv.h` -- declaring `<name as an identifier>_spv` -- to <target>.
# urender's shared GLSL (types.glsl, light.glsl and the rest) is on the
# include path, which is how a test's compute shader includes the very file
# the shading pass does.
function(uta_embed_shaders target)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY "${out_dir}")
    foreach(source IN LISTS ARGN)
        get_filename_component(name "${source}" NAME)
        get_filename_component(path "${source}" ABSOLUTE)
        string(MAKE_C_IDENTIFIER "${name}_spv" identifier)
        set(spirv "${out_dir}/${name}.spv")
        set(header "${out_dir}/${name}.spv.h")
        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND Vulkan::glslc --target-env=vulkan1.3 -O
                    -I "${UTA_SHADER_DIR}"
                    -MD -MF "${spirv}.d" -MT "${spirv}"
                    -o "${spirv}" "${path}"
            DEPENDS "${path}"
            DEPFILE "${spirv}.d"
            COMMENT "glslc ${name}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${CMAKE_COMMAND}" -DINPUT=${spirv} -DOUTPUT=${header} -DNAME=${identifier}
                    -P "${UTA_EMBED_SPIRV}"
            DEPENDS "${spirv}" "${UTA_EMBED_SPIRV}"
            COMMENT "embed ${name}"
            VERBATIM)
        target_sources(${target} PRIVATE "${header}")
    endforeach()
    target_include_directories(${target} PRIVATE "${out_dir}")
endfunction()

uta_embed_shaders(uta_urender
    shaders/cluster.comp
    shaders/scene.vert
    shaders/scene.frag
    shaders/post.vert
    shaders/post.frag)

# INV-1, asserted at configure time in the mechanism src/ubundle/CMakeLists.txt
# uses for UTA-0008's INV-10 -- and the only thing that grades it: a compiled
# test cannot see a link set, and uta_unit_tests links uta_upkg, uta_umat and
# uta_ubake itself, so a check inside it would pass whether or not this
# library had an edge to any of them.
#
# The mechanism, not the string: this library links two external targets and
# ubundle links none, so the permitted list names both. uta_upkg, uta_umat and
# uta_ubake are build-time only (docs/design.md rule 2), and ut-ants will link
# this library.
get_target_property(_uta_urender_links uta_urender LINK_LIBRARIES)
if(NOT _uta_urender_links STREQUAL "uta_core;uta_ubundle;Vulkan::Vulkan;glm::glm")
    message(FATAL_ERROR
        "uta_urender links '${_uta_urender_links}'. INV-1 in "
        "docs/specs/UTA-0014-vulkan-draw-path.md allows uta_core, uta_ubundle, "
        "Vulkan::Vulkan and glm::glm, in that order, and nothing else: the "
        "renderer is linked by the game, and docs/design.md rule 2 keeps the "
        "package reader, the material maker and the baker out of it.")
endif()
unset(_uta_urender_links)
