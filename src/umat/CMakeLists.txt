# umat -- block-compress a texture, cap its upscale, and measure a map's
# texture working set. Build-time only: docs/design.md rule 2 keeps it out of
# every runtime target.
#
# ONE library, unlike umap and unav. Those split because a builder half must
# reach uta_upkg while a runtime half must not; umat has no runtime half.
#
# The vendored encoders are compiled INTO this library rather than into one of
# their own, so nothing else in the build can reach them --
# docs/specs/UTA-0052-texture-memory-budget.md SS 4.1.

set(_uta_bc7enc_dir "${PROJECT_SOURCE_DIR}/third_party/bc7enc")

add_library(uta_umat STATIC Material.cpp "${_uta_bc7enc_dir}/bc7enc.c")
target_link_libraries(uta_umat PUBLIC uta_core uta_ubundle)
target_include_directories(uta_umat PRIVATE "${_uta_bc7enc_dir}")

# bc7enc.c is C, compiled as C++. The root project stays LANGUAGES CXX: the
# numeric-contract guard there scans the CMAKE_CXX_FLAGS family only, so a C
# translation unit would sit outside the one check that holds INV-6.
set_source_files_properties("${_uta_bc7enc_dir}/bc7enc.c" PROPERTIES LANGUAGE CXX)

# INV-12, in the form src/ubundle/CMakeLists.txt uses for UTA-0008's INV-10.
# Not uta_upkg: nothing here reads a package, and UTA-0009 adds that link and
# amends INV-12 in the same change. A convenience dependency on a bake-side
# library is how docs/design.md rule 2's boundary stops being checkable.
get_target_property(_uta_umat_links uta_umat LINK_LIBRARIES)
if(NOT _uta_umat_links STREQUAL "uta_core;uta_ubundle")
    message(FATAL_ERROR
        "uta_umat links '${_uta_umat_links}'. INV-12 in "
        "docs/specs/UTA-0052-texture-memory-budget.md allows uta_core and "
        "uta_ubundle, in that order, and nothing else.")
endif()
unset(_uta_umat_links)

# INV-8. The root guard sees CMAKE_CXX_FLAGS and nothing else, so a flag added
# to this target or to one of its sources configures green under it. This
# reads the root guard's own pattern rather than a copy of it.
#
# The pattern names the ENABLING spellings only. COMPILE_OPTIONS starts from
# the directory property add_compile_options sets, so on a correct tree it
# already carries -ffp-contract=off -fno-fast-math, or /fp:precise -- a wider
# pattern would refuse every correct build.
#
# Placed last, so it sees every option added above it.
get_target_property(_uta_umat_options uta_umat COMPILE_OPTIONS)
if(_uta_umat_options MATCHES "${_uta_unsafe_fp_pattern}")
    message(FATAL_ERROR
        "uta_umat's COMPILE_OPTIONS ('${_uta_umat_options}') enable fast-math "
        "or contraction. INV-8 in docs/specs/UTA-0052-texture-memory-budget.md "
        "forbids it: a multiply-add folded on one compiler and not another "
        "changes INV-6's golden bytes on that leg alone.")
endif()
get_target_property(_uta_umat_sources uta_umat SOURCES)
foreach(_uta_umat_source IN LISTS _uta_umat_sources)
    get_source_file_property(_uta_umat_source_options "${_uta_umat_source}" COMPILE_OPTIONS)
    get_source_file_property(_uta_umat_source_flags "${_uta_umat_source}" COMPILE_FLAGS)
    if("${_uta_umat_source_options};${_uta_umat_source_flags}" MATCHES "${_uta_unsafe_fp_pattern}")
        message(FATAL_ERROR
            "${_uta_umat_source} carries a fast-math or contraction-enabling "
            "flag. INV-8 in docs/specs/UTA-0052-texture-memory-budget.md "
            "forbids it for every translation unit uta_umat compiles, the "
            "vendored sources included.")
    endif()
endforeach()
unset(_uta_umat_options)
unset(_uta_umat_sources)
unset(_uta_umat_source)
unset(_uta_umat_source_options)
unset(_uta_umat_source_flags)
unset(_uta_bc7enc_dir)
