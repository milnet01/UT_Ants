# UTA-0200: resolve the build's commit and write BuildCommit.h.
#
# Run with cmake -P, both at configure time and again on every build, so the
# value names the commit that COMPILED rather than the one the build directory
# was last configured at. UTA-0191 resolved it once at configure time and the
# first capture it ever took already named the wrong build.
#
# configure_file rewrites the header only when the value actually differs, so
# main.cpp recompiles on a new commit and not otherwise -- which is what keeps
# a per-build git call from costing a rebuild every time.
#
# Needs SOURCE_DIR, TEMPLATE and OUTPUT on the command line.

execute_process(
    COMMAND git -C "${SOURCE_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE UTA_BUILD_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _status)

# A source tarball has no git history, and "unknown" is the honest answer there
# -- the capture's bundle hash and baker version still say what was drawn.
if(NOT _status EQUAL 0 OR UTA_BUILD_COMMIT STREQUAL "")
    set(UTA_BUILD_COMMIT "unknown")
endif()

configure_file("${TEMPLATE}" "${OUTPUT}" @ONLY)
