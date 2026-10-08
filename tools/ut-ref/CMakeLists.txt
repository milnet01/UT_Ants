# ut-ref -- UTA-0292. Traces exact light in UTA-0112's model for a list of
# views and scores ut-shot --light-terms against it
# (docs/specs/UTA-0292-reference-path-tracer.md). Run by hand.
#
# Reference.cpp is the tracer and the scorer; tests/CMakeLists.txt compiles it
# into the unit tests too, as it does the other tools' Cli.cpp.
add_executable(ut-ref main.cpp Reference.cpp)
target_link_libraries(ut-ref PRIVATE uta_ubake uta_urender uta_ubundle uta_core)
