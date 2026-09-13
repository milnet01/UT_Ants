# ut-origin -- whether a bundle's header marks it authored. UTA-0013's
# quarantine guard runs it over every tracked .utab outside content/, so the
# header is read by ubundle::readHeader, the one reader
# docs/specs/UTA-0008-bundle-container-and-origin.md SS 4.5 names, and not by a
# second copy of the header rules in shell.
#
# Cli.cpp is the command line as a function; tests/CMakeLists.txt compiles it
# into the unit tests too, as it does ut-bake's.
add_executable(ut-origin main.cpp Cli.cpp)
target_link_libraries(ut-origin PRIVATE uta_ubundle)
