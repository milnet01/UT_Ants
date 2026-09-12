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
    unit/BakeCollisionTest.cpp
    unit/BakeFixture.cpp
    unit/BakeGeometryTest.cpp
    unit/BakeGoldenTest.cpp
    unit/BakeInstallTest.cpp
    unit/BakeLightModelTest.cpp
    unit/BakeLightProbesTest.cpp
    unit/BakeMoversTest.cpp
    unit/BakeTest.cpp
    unit/BundleActorsTest.cpp
    unit/BundleCollisionTest.cpp
    unit/BundleMoversTest.cpp
    unit/BundleFormatTest.cpp
    unit/BundleGeometryTest.cpp
    unit/BundleLightProbesTest.cpp
    unit/BundleMalformedTest.cpp
    unit/BundleMaterialTest.cpp
    unit/BundleTextureTest.cpp
    unit/CompactIndexTest.cpp
    unit/CoreErrorTest.cpp
    unit/CoreFileSystemTest.cpp
    unit/CoreJobsTest.cpp
    unit/CoreLogTest.cpp
    unit/CoreMd5Test.cpp
    unit/CoreSha256Test.cpp
    unit/LightFixture.cpp
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
    unit/PathFixture.cpp
    unit/PathSeedsTest.cpp
    unit/PathTraceTest.cpp
    unit/PathWalkableTest.cpp
    unit/RenderCameraTest.cpp
    unit/RenderClusterTest.cpp
    unit/RenderDeviceTest.cpp
    unit/RenderLightsTest.cpp
    unit/RenderMoverPlacementTest.cpp
    unit/RenderShadowsTest.cpp
    unit/RoomBuildTest.cpp
    unit/RoomMapTest.cpp
    unit/WiringGraphTest.cpp)
target_link_libraries(uta_unit_tests PRIVATE uta_core uta_upkg uta_unav uta_unav_build
                                            uta_umap uta_umap_build uta_ubundle uta_umat
                                            uta_ubake uta_urender uta_test_support
                                            Catch2::Catch2WithMain
                                            # INV-3's synthetic devices are Vulkan's own feature structs.
                                            Vulkan::Vulkan)

# The ut-bake command line, compiled in so INV-15 of
# docs/specs/UTA-0011-map-baker.md is tested without starting a process.
target_sources(uta_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools/ut-bake/Cli.cpp")
# And ut-paths', but for main.cpp -- docs/specs/UTA-0121-bot-path-seeds.md SS 4.1.
target_sources(uta_unit_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tools/ut-paths/Cli.cpp"
    "${PROJECT_SOURCE_DIR}/tools/ut-paths/Walkable.cpp"
    "${PROJECT_SOURCE_DIR}/tools/ut-paths/Seeds.cpp")
target_include_directories(uta_unit_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools")

# TIMEOUT is load-bearing rather than tidy: INV-10's breaking case is a
# deadlock, and without a bound it hangs the run instead of failing it.
catch_discover_tests(uta_unit_tests PROPERTIES LABELS "unit;fast" TIMEOUT 30)

# UTA-0014 INV-2, in a target of its own that links uta_urender and NOT
# Vulkan::Vulkan. In uta_unit_tests it could never fail: that binary has the
# Vulkan include path for INV-3.
add_executable(uta_render_header_isolation_test unit/RenderHeaderIsolationTest.cpp)
target_link_libraries(uta_render_header_isolation_test PRIVATE uta_urender Catch2::Catch2WithMain)
catch_discover_tests(uta_render_header_isolation_test PROPERTIES LABELS "unit;fast" TIMEOUT 30)

# UTA-0014's device tier -- docs/specs/UTA-0014-vulkan-draw-path.md SS 7.
#
# REGISTERED UNCONDITIONALLY, wherever uta_urender builds, and that is the
# point. A guard on finding a loader would mean a leg that lost it registered
# no device test and reported green over a renderer nothing ran. The loader and
# glslc are REQUIRED at configure time instead; only a missing DRIVER gets as
# far as a test, and INV-5 is what reds the leg for it. scripts/ci.sh chooses
# which labels a leg runs.
add_executable(uta_device_tests
    device/ComputeFixture.cpp
    device/DeviceFixture.cpp
    device/RenderColourTransferTest.cpp
    device/RenderDeviceAbsentTest.cpp
    device/RenderLightParityTest.cpp
    device/RenderLightingTest.cpp
    device/RenderMoverTest.cpp
    device/RenderOffscreenTest.cpp
    device/RenderProbeTest.cpp
    device/RenderShadowTest.cpp
    device/RenderSurfaceFlagsTest.cpp)
# uta_ubake beside uta_urender: INV-6 holds the shader's light to ubake's
# lightAt, which UTA-0112 SS 4.9 says a test of this item may link.
target_link_libraries(uta_device_tests PRIVATE uta_urender uta_ubundle uta_ubake Vulkan::Vulkan
                                               Catch2::Catch2WithMain)
target_include_directories(uta_device_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
# The kernels include urender's own GLSL -- the files the shading pass does.
uta_embed_shaders(uta_device_tests device/shaders/light_parity.comp device/shaders/probe_eval.comp)
# Two registrations of one binary, split by tag. The TIMEOUT is for Mesa's CPU
# driver, which compiles every pipeline on first use.
catch_discover_tests(uta_device_tests TEST_SPEC "[device]" PROPERTIES LABELS "device" TIMEOUT 300)
catch_discover_tests(uta_device_tests TEST_SPEC "[device-absent]" PROPERTIES LABELS "device-absent" TIMEOUT 60)
# INV-5's own grader. RenderDeviceAbsentTest grades the refusal; this grades the
# rule, running a `device` test with no driver. It passes only on
# requireRenderer's failure line -- the compact reporter puts the verdict and the
# message on one line, and a SKIP carrying the same message says `skipped:` --
# so a fixture that skipped, or a name that matched no test, prints nothing it
# accepts.
set(uta_no_driver "uta-no-such-vulkan-driver.json")
set(uta_inv5_rule "INV-5: a device test with no driver fails rather than skips")
add_test(NAME "${uta_inv5_rule}"
         COMMAND uta_device_tests "INV-4: the surfaceless path draws and reads back with no display"
                 --reporter compact)
set_tests_properties("${uta_inv5_rule}" PROPERTIES
    LABELS "device-absent" TIMEOUT 60
    ENVIRONMENT "VK_DRIVER_FILES=${uta_no_driver};VK_ICD_FILENAMES=${uta_no_driver}"
    ENVIRONMENT_MODIFICATION "VK_ADD_DRIVER_FILES=unset:"
    PASS_REGULAR_EXPRESSION "failed: explicitly with 1 message: 'no renderer, so this device test cannot pass")

if(UTA_REAL_ASSET_TESTS)
    add_executable(uta_real_asset_tests real/RealInstallTest.cpp real/RealGeometryTest.cpp
                                        real/RealActorsTest.cpp real/RealMoversTest.cpp
                                        real/RealCollisionTest.cpp real/RealPathSeedsTest.cpp
                                        real/RealLightProbesTest.cpp)
    target_link_libraries(uta_real_asset_tests PRIVATE uta_upkg uta_umap uta_umap_build uta_unav_build
                                                       uta_umat uta_ubake Catch2::Catch2WithMain)
    # tests/support/FCoordsPort.h, header-only -- UTA-0119 SS 7.
    target_include_directories(uta_real_asset_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    # ut-paths, but for its command line -- UTA-0121 SS 7.
    target_sources(uta_real_asset_tests PRIVATE
            "${PROJECT_SOURCE_DIR}/tools/ut-paths/Walkable.cpp"
        "${PROJECT_SOURCE_DIR}/tools/ut-paths/Seeds.cpp")
    target_include_directories(uta_real_asset_tests PRIVATE "${PROJECT_SOURCE_DIR}/tools")
    target_compile_definitions(uta_real_asset_tests
        PRIVATE UTA_UT_INSTALL_DIR="${UTA_UT_INSTALL_DIR}")
    catch_discover_tests(uta_real_asset_tests PROPERTIES LABELS "real-assets;local-only")
endif()
