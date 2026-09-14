# ut-ants, the client -- UTA-0016. A RUNTIME target (docs/design.md § Programs).
#
# Cli.cpp and FlyCamera.cpp need no window, and tests/CMakeLists.txt compiles
# them into the unit tests. main.cpp is the SDL half, run by hand: no CI leg
# has a display.
add_executable(ut-ants main.cpp Cli.cpp FlyCamera.cpp)
target_link_libraries(ut-ants PRIVATE uta_urender uta_ubundle uta_core SDL3::SDL3)

# Rule 16 has ut-ants run ut-bake, and it looks beside itself, so a build puts
# one there. A target of its own rather than a POST_BUILD step on ut-ants, which
# would leave a stale copy whenever ut-bake is rebuilt and ut-ants is not.
add_custom_target(ut-ants-baker ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:ut-bake>" "$<TARGET_FILE_DIR:ut-ants>"
    COMMENT "Putting ut-bake beside ut-ants"
    VERBATIM)
add_dependencies(ut-ants-baker ut-bake ut-ants)

# docs/design.md rule 2: the link closure of a runtime target contains no
# uta_upkg, uta_umat or uta_ubake. This is the test that rule names; UTA-0011
# and UTA-0052 left it to the first item that built a runtime target.
#
# Configure time, as src/upkg/CMakeLists.txt asserts INV-13, and every target
# is defined by now: the root CMakeLists.txt adds apps/ after src/ and tools/.
# The walk follows LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES through aliases
# and $<LINK_ONLY:>; any other generator expression names no target here.
function(uta_link_closure out target)
    set(seen "")
    set(pending "${target}")
    while(pending)
        list(POP_FRONT pending item)
        if(item MATCHES "^\\$<LINK_ONLY:(.+)>$")
            set(item "${CMAKE_MATCH_1}")
        endif()
        if(NOT TARGET "${item}")
            continue()
        endif()
        get_target_property(aliased "${item}" ALIASED_TARGET)
        if(aliased)
            set(item "${aliased}")
        endif()
        if(item IN_LIST seen)
            continue()
        endif()
        list(APPEND seen "${item}")
        foreach(property IN ITEMS LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(links "${item}" ${property})
            if(links)
                list(APPEND pending ${links})
            endif()
        endforeach()
    endwhile()
    set(${out} "${seen}" PARENT_SCOPE)
endfunction()

uta_link_closure(_uta_client_closure ut-ants)
# The walk must reach what ut-ants does link, or finding nothing forbidden
# proves nothing: uta_umap arrives only through uta_ubundle.
foreach(_uta_expected IN ITEMS uta_urender uta_umap)
    if(NOT _uta_expected IN_LIST _uta_client_closure)
        message(FATAL_ERROR
            "The link-closure walk over ut-ants did not reach ${_uta_expected}, so it "
            "cannot be trusted to find what docs/design.md rule 2 forbids. "
            "Closure: ${_uta_client_closure}")
    endif()
endforeach()
foreach(_uta_forbidden IN ITEMS uta_upkg uta_umat uta_ubake)
    if(_uta_forbidden IN_LIST _uta_client_closure)
        message(FATAL_ERROR
            "ut-ants links ${_uta_forbidden}. docs/design.md rule 2 keeps upkg, umat "
            "and ubake out of every runtime target; ut-ants runs ut-bake instead "
            "(rule 16). Closure: ${_uta_client_closure}")
    endif()
endforeach()
unset(_uta_client_closure)
