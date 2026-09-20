# ut-ants, the client -- UTA-0016. A RUNTIME target (docs/design.md § Programs).
#
# Cli.cpp, FlyCamera.cpp, MapList.cpp, Png.cpp and Capture.cpp need no window,
# and tests/CMakeLists.txt compiles them into the unit tests. main.cpp and
# Launcher.cpp are the SDL half, run by hand: no CI leg has a display.
add_executable(ut-ants main.cpp Cli.cpp FlyCamera.cpp Launcher.cpp MapList.cpp Png.cpp Capture.cpp)
target_link_libraries(ut-ants PRIVATE uta_urender uta_uworld uta_ubundle uta_core SDL3::SDL3)

# UTA-0191's capture folder writes PNG, and Png.cpp is the one translation unit
# that instantiates the vendored encoder. third_party/stb/README.md records the
# copy; docs/standards/dependency-acquisition.md § 2 question 4 is why it is
# vendored. PRIVATE, so the header reaches no other target by accident -- and
# deliberately NOT on uta_core, whose rule-1 independence it would breach.
target_include_directories(ut-ants PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")

# UTA-0191: a capture folder records which build drew the frame. UTA-0200:
# resolved on every BUILD rather than once at configure, or the field names the
# commit the build directory was last configured at -- which goes stale on any
# commit made without re-running cmake, and did so on this feature's own first
# capture.
#
# WriteBuildCommit.cmake is run twice by design. Once here, so the header
# exists for the first compile and for an editor's index; then again by the
# target below on every build. configure_file rewrites it only when the commit
# actually differs, so main.cpp recompiles on a new commit and not otherwise.
set(_uta_commit_template "${CMAKE_CURRENT_SOURCE_DIR}/BuildCommit.h.in")
set(_uta_commit_header "${CMAKE_CURRENT_BINARY_DIR}/BuildCommit.h")
set(_uta_commit_script "${CMAKE_CURRENT_SOURCE_DIR}/WriteBuildCommit.cmake")
set(_uta_commit_args
    -D "SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -D "TEMPLATE=${_uta_commit_template}"
    -D "OUTPUT=${_uta_commit_header}")

execute_process(COMMAND "${CMAKE_COMMAND}" ${_uta_commit_args} -P "${_uta_commit_script}")

add_custom_target(ut-ants-commit
    COMMAND "${CMAKE_COMMAND}" ${_uta_commit_args} -P "${_uta_commit_script}"
    BYPRODUCTS "${_uta_commit_header}"
    COMMENT "Resolving the build's commit"
    VERBATIM)
add_dependencies(ut-ants ut-ants-commit)
target_include_directories(ut-ants PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

unset(_uta_commit_template)
unset(_uta_commit_header)
unset(_uta_commit_script)
unset(_uta_commit_args)

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
