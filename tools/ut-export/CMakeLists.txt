# ut-export -- UTA-0476. Writes a baked map as ExperiRays' glTF scene and
# camera path (its EXRAY-0001). Run by hand. tests/CMakeLists.txt compiles
# Scene.cpp and Path.cpp into the unit tests too.
add_executable(ut-export main.cpp Scene.cpp Path.cpp)
target_link_libraries(ut-export PRIVATE uta_ubake uta_ubundle uta_unav uta_core uta_tools_common)
