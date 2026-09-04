include(FetchContent)

# Fetched rather than found. A stranger cloning this repository has no Catch2
# installed and must still be able to run the suite, which is S7.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0        # current release, resolved 2026-09-03
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)

# The synthetic fixtures. Test-only by design: docs/design.md rule 2 keeps the
# package reader out of every runtime target, and this is the writer that
# feeds it.
add_library(uta_test_support STATIC support/UnrealPackageBuilder.cpp)
target_include_directories(uta_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(uta_unit_tests
    unit/CompactIndexTest.cpp
    unit/CoreErrorTest.cpp
    unit/PackageBuilderTest.cpp)
target_link_libraries(uta_unit_tests PRIVATE uta_core uta_test_support Catch2::Catch2WithMain)

# TIMEOUT is load-bearing rather than tidy: INV-10's breaking case is a
# deadlock, and without a bound it hangs the run instead of failing it.
catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast" TIMEOUT 30)

if(UTA_REAL_ASSET_TESTS)
    add_executable(uta_real_asset_tests real/RealInstallTest.cpp)
    target_link_libraries(uta_real_asset_tests PRIVATE Catch2::Catch2WithMain)
    target_compile_definitions(uta_real_asset_tests
        PRIVATE UTA_UT_INSTALL_DIR="${UTA_UT_INSTALL_DIR}")
    catch_discover_tests(uta_real_asset_tests PROPERTIES LABELS "real-assets;local-only")
endif()
