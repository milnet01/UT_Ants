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
    unit/BakeActorsTest.cpp
    unit/BakeCliTest.cpp
    unit/BakeFixture.cpp
    unit/BakeGeometryTest.cpp
    unit/BakeGoldenTest.cpp
    unit/BakeInstallTest.cpp
    unit/BakeTest.cpp
    unit/BundleActorsTest.cpp
    unit/BundleFormatTest.cpp
    unit/BundleGeometryTest.cpp
    unit/BundleMalformedTest.cpp
    unit/BundleMaterialTest.cpp
    unit/BundleTextureTest.cpp
    unit/CompactIndexTest.cpp
    unit/CoreErrorTest.cpp
    unit/CoreFileSystemTest.cpp
    unit/CoreJobsTest.cpp
    unit/CoreLogTest.cpp
    unit/CoreSha256Test.cpp
    unit/MaterialCompressTest.cpp
    unit/MaterialGenerateTest.cpp
    unit/MaterialLibraryTest.cpp
    unit/NavGraphTest.cpp
    unit/NumericContractTest.cpp
    unit/PackageAncestryTest.cpp
    unit/PackageBuilderTest.cpp
    unit/PackageClassTest.cpp
    unit/PackageContentTest.cpp
    unit/PackageMalformedTest.cpp
    unit/PackagePropertiesTest.cpp
    unit/PackageReaderTest.cpp
    unit/PackageScriptTest.cpp
    unit/RoomBuildTest.cpp
    unit/RoomMapTest.cpp
    unit/WiringGraphTest.cpp)
target_link_libraries(uta_unit_tests PRIVATE uta_core uta_upkg uta_unav uta_unav_build
                                            uta_umap uta_umap_build uta_ubundle uta_umat
                                            uta_ubake uta_test_support Catch2::Catch2WithMain)

# The ut-bake command line, compiled in so INV-15 of
# docs/specs/UTA-0011-map-baker.md is tested without starting a process.
target_sources(uta_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools/ut-bake/Cli.cpp")
target_include_directories(uta_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools")

# TIMEOUT is load-bearing rather than tidy: INV-10's breaking case is a
# deadlock, and without a bound it hangs the run instead of failing it.
catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast" TIMEOUT 30)

if(UTA_REAL_ASSET_TESTS)
    add_executable(uta_real_asset_tests real/RealInstallTest.cpp real/RealGeometryTest.cpp
                                        real/RealActorsTest.cpp)
    target_link_libraries(uta_real_asset_tests PRIVATE uta_upkg uta_umap uta_umap_build uta_unav_build
                                                       uta_umat uta_ubake Catch2::Catch2WithMain)
    target_compile_definitions(uta_real_asset_tests
        PRIVATE UTA_UT_INSTALL_DIR="${UTA_UT_INSTALL_DIR}")
    catch_discover_tests(uta_real_asset_tests PROPERTIES LABELS "real-assets;local-only")
endif()
