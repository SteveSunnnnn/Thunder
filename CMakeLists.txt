cmake_minimum_required(VERSION 3.25)
project(Thunder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Keep every target (including tools) on the same UTF-8 source
    # encoding.  Applying this globally avoids headers being reparsed under
    # the host code page when they are included by an executable target.
    add_compile_options(/utf-8)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ThunderWarnings)

option(THUNDER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(THUNDER_BUILD_VULKAN "Build the SDL3/Vulkan rendering backend" OFF)
option(THUNDER_BUILD_TOOLS "Build engine command-line tools" ON)
option(THUNDER_BUILD_TESTS "Build Thunder test suites" ${PROJECT_IS_TOP_LEVEL})

find_package(Threads REQUIRED)

# Enforce the one-way product boundary at configure time. Engine code may
# expose reusable capabilities, but it must never depend on this repository's
# game client or authored content adapters.
file(GLOB_RECURSE THUNDER_BOUNDARY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thunder/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/thunder/*.hpp")
foreach(thunder_source IN LISTS THUNDER_BOUNDARY_SOURCES)
    file(READ "${thunder_source}" thunder_source_text)
    if(thunder_source_text MATCHES "#[ \t]*include[ \t]*[<\"]game/")
        message(FATAL_ERROR "Engine/game boundary violation: ${thunder_source} includes game code")
    endif()
    if(thunder_source_text MATCHES "set_world_map_layers|world_map(_ids|_terrain|_height)?\\.thunderimg")
        message(FATAL_ERROR "Legacy full-world atlas path is forbidden in Thunder: ${thunder_source}")
    endif()
endforeach()

# Floating-point determinism guard. Authoritative doubles are hashed by bit
# pattern, so fast-math family flags (reassociation, FMA contraction,
# flush-to-zero) silently break world-checksum determinism across builds.
# Reject them at configure time like the boundary guards above; see
# docs/DETERMINISM.md ("Floating point") and
# thunder/foundation/base/FpEnvironment.hpp for the runtime probe.
set(THUNDER_FAST_MATH_PATTERN
    "(^|[ \t;])(-ffast-math|-funsafe-math-optimizations|-ffinite-math-only|-ffp-contract=fast|-Ofast|/fp:fast|-fp-model=fast)")
foreach(thunder_flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
    if(DEFINED ${thunder_flag_var} AND "${${thunder_flag_var}}" MATCHES "${THUNDER_FAST_MATH_PATTERN}")
        message(FATAL_ERROR
            "Determinism violation: ${thunder_flag_var} contains a fast-math flag "
            "(${${thunder_flag_var}}); see docs/DETERMINISM.md")
    endif()
endforeach()

add_library(thunder_runtime
    src/thunder/foundation/jobs/JobSystem.cpp
    src/thunder/simulation/economy/EconomyDefinitions.cpp
    src/thunder/simulation/economy/EconomyPopulationPhases.cpp
    src/thunder/simulation/economy/EconomyMarketPhases.cpp
    src/thunder/simulation/economy/CountryStore.cpp
    src/thunder/simulation/economy/MarketStore.cpp
    src/thunder/simulation/economy/BuildingStore.cpp
    src/thunder/simulation/economy/PopStore.cpp
    src/thunder/simulation/economy/MarketEntityIndex.cpp
    src/thunder/simulation/economy/EconomySystem.cpp
    src/thunder/simulation/economy/CurrencyStore.cpp
    src/thunder/simulation/economy/BankStore.cpp
    src/thunder/simulation/economy/TradePolicyStore.cpp
    src/thunder/simulation/economy/ConstructionStore.cpp
    src/thunder/simulation/living/ProvinceEntityIndex.cpp
    src/thunder/simulation/living/LivingMapSystem.cpp
    src/thunder/simulation/living/LivingMapStreamingPlanner.cpp
    src/thunder/simulation/living/TransportNetwork.cpp
    src/thunder/simulation/kernel/GameClock.cpp
    src/thunder/simulation/kernel/World.cpp
    src/thunder/simulation/kernel/TickScheduler.cpp
    src/thunder/simulation/kernel/ModifierGraph.cpp
    src/thunder/simulation/kernel/HierarchicalModifierGraph.cpp
    src/thunder/simulation/kernel/CommandQueue.cpp
    src/thunder/scripting/ScriptRegistry.cpp
    src/thunder/scripting/SymbolTable.cpp
    src/thunder/scripting/ThunderScriptParser.cpp
    src/thunder/scripting/ScriptContext.cpp
    src/thunder/scripting/ScriptProgram.cpp
    src/thunder/scripting/ScriptProgramDatabase.cpp
    src/thunder/scripting/ScriptCompiler.cpp
    src/thunder/scripting/ScriptCompilerScoped.cpp
    src/thunder/scripting/ScriptProfiler.cpp
    src/thunder/scripting/ScriptVm.cpp
    src/thunder/scripting/ScriptVmValues.cpp
    src/thunder/scripting/ScriptBytecode.cpp
    src/thunder/scripting/ScriptMemoizer.cpp
    src/thunder/scripting/ScopeResolver.cpp
    src/thunder/scripting/DynamicVariables.cpp
    src/thunder/scripting/GlobalScriptStore.cpp
    src/thunder/simulation/gameplay/ScriptedGameplay.cpp

    src/thunder/simulation/ai/UtilityAi.cpp
    src/thunder/content/definition/ModManifest.cpp
    src/thunder/content/definition/ModVersion.cpp
    src/thunder/content/definition/VirtualFileSystem.cpp
    src/thunder/content/definition/DefinitionDatabase.cpp
    src/thunder/content/definition/WorldContentBinder.cpp
    src/thunder/content/definition/GameplayContent.cpp
    src/thunder/content/definition/ResearchContent.cpp
    src/thunder/content/definition/NotificationContent.cpp
    src/thunder/simulation/gameplay/NotificationRuntime.cpp
    src/thunder/content/definition/OnActionContent.cpp
    src/thunder/simulation/gameplay/OnActionRuntime.cpp
    src/thunder/simulation/gameplay/GameplaySignalRegistry.cpp
    src/thunder/content/definition/ContentLoader.cpp
    src/thunder/content/definition/LocalizationDatabase.cpp
    src/thunder/presentation/render/RenderSnapshotBuilder.cpp
    src/thunder/presentation/render/RenderGraph.cpp
    src/thunder/presentation/render/LivingMapRenderPlan.cpp
    src/thunder/presentation/render/GpuCapabilities.cpp
    src/thunder/presentation/render/StrategicCamera.cpp
    src/thunder/presentation/render/GpuDrivenPipeline.cpp
    src/thunder/presentation/render/BindlessMaterialSystem.cpp
    src/thunder/presentation/render/PhysicalLighting.cpp
    src/thunder/presentation/render/RenderQuality.cpp
    src/thunder/presentation/render/RenderSnapshot.cpp
    src/thunder/presentation/render/environment/VolumetricAtmosphereAndClouds.cpp
    src/thunder/presentation/render/map/BorderMesh3D.cpp
    src/thunder/presentation/render/terrain/TerrainMaterialShading.cpp
    src/thunder/presentation/render/vegetation/ForestCanopyInstancer.cpp
    src/thunder/presentation/render/vfx/LivingMapVfx3D.cpp
    src/thunder/presentation/render/water/RiverSplineFlowPass.cpp
    src/thunder/presentation/render/flag/DynamicFlag3D.cpp
    src/thunder/presentation/render/terrain/TerrainClipmap.cpp

    src/thunder/presentation/render/terrain/TerrainHeightPage.cpp
    src/thunder/presentation/render/terrain/TerrainPageCache.cpp
    src/thunder/presentation/render/terrain/TerrainStreamingPlanner.cpp
    src/thunder/presentation/render/terrain/TerrainRenderPlan.cpp
    src/thunder/presentation/render/map/ProvinceRasterPage.cpp
    src/thunder/presentation/render/map/CoastDistancePage.cpp
    src/thunder/presentation/render/map/PoliticalMapState.cpp
    src/thunder/presentation/render/map/PoliticalMapRenderPlan.cpp
    src/thunder/presentation/render/map/VectorMapPipeline.cpp
    src/thunder/presentation/render/water/PhysicalWaterPass.cpp
    src/thunder/presentation/render/vulkan/VulkanProbe.cpp
    src/thunder/presentation/render/map/PoliticalMapStreamingPlanner.cpp
    src/thunder/presentation/render/map/ProvincePickingCache.cpp
    src/thunder/presentation/render/map/PoliticalMapPageBundle.cpp
    src/thunder/presentation/render/map/WorldMapPageSource.cpp
    src/thunder/presentation/render/map/WorldMapPageStreamer.cpp
    src/thunder/presentation/render/map/WorldMapPicker.cpp
    src/thunder/presentation/render/map/WorldStaticLayerSource.cpp
    src/thunder/presentation/render/map/MapModeStore.cpp
    src/thunder/simulation/world/ProvinceAdjacencyGraph.cpp
    src/thunder/simulation/world/HierarchicalPathfinder.cpp
    src/thunder/simulation/world/SpatialPlacement.cpp
    src/thunder/simulation/world/StateRegionIndex.cpp
    src/thunder/simulation/world/GeographyStore.cpp
    src/thunder/simulation/world/WorldBootstrap.cpp
    src/thunder/simulation/world/WorldBootstrapWire.cpp
    src/thunder/simulation/world/WorldTopology.cpp
    src/thunder/simulation/world/WorldStaticLayers.cpp
    src/thunder/simulation/world/WorldMapHierarchy.cpp
    src/thunder/simulation/world/WorldMapHierarchyWire.cpp
    src/thunder/simulation/world/WorldMapLabels.cpp
    src/thunder/simulation/grand_strategy/GrandStrategyStore.cpp
    src/thunder/simulation/grand_strategy/GrandStrategyValidation.cpp
    src/thunder/simulation/grand_strategy/GrandStrategyWeekly.cpp
    src/thunder/simulation/research/ResearchSystem.cpp
    src/thunder/runtime/save/SaveGame.cpp
    src/thunder/runtime/save/SaveGameLegacy.cpp
    src/thunder/runtime/save/SaveGameRuntime.cpp
    src/thunder/runtime/save/SaveGameScriptSections.cpp
    src/thunder/runtime/save/SaveGameWorldSections.cpp
    src/thunder/runtime/save/AsyncSavePipeline.cpp
    src/thunder/runtime/save/ReplayJournal.cpp
    src/thunder/runtime/engine/ThunderEngine.cpp
    src/thunder/runtime/engine/GameContentRuntime.cpp
    src/thunder/runtime/engine/DebugConsole.cpp
    src/thunder/content/localization/LocalizationStore.cpp
    src/thunder/simulation/ai/StrategicAiPlanner.cpp
    src/thunder/presentation/render/map/VectorMapTypography.cpp
    src/thunder/presentation/render/map/MapDecorationRenderer.cpp
    src/thunder/simulation/warfare/LogisticsNetwork.cpp
    src/thunder/simulation/warfare/BattlePhaseSystem.cpp
    src/thunder/presentation/render/vfx/LivingMapVfxSystem.cpp
    src/thunder/content/assets/AssetPack.cpp
    src/thunder/content/assets/MeshData.cpp
    src/thunder/content/assets/GlTFImporter.cpp
    src/thunder/content/assets/ArchitectureKit.cpp
    src/thunder/content/assets/Material.cpp
    src/thunder/presentation/ui/StrategyUi.cpp
    src/thunder/presentation/ui/StrategyUiAnalytics.cpp
    src/thunder/presentation/ui/StrategyUiComponents.cpp
    src/thunder/presentation/ui/StrategyUiPrimitives.cpp
    src/thunder/presentation/ui/UiTheme.cpp
    src/thunder/presentation/ui/TooltipStack.cpp
    src/thunder/presentation/ui/ScriptedGui.cpp
    src/thunder/presentation/ui/ScriptedGuiCompiler.cpp
    src/thunder/presentation/ui/ScriptedGuiRuntime.cpp
    src/thunder/presentation/ui/ScriptedGuiValues.cpp
    src/thunder/presentation/ui/ScriptedGuiPainter.cpp
    src/thunder/presentation/ui/ScriptedGuiLayout.cpp
    src/thunder/presentation/ui/FontAtlas.cpp
    src/thunder/presentation/ui/LocalizationRichText.cpp
    src/thunder/runtime/editor/MapEditorSystem.cpp
    src/thunder/runtime/editor/GuiLayoutEditor.cpp
    src/thunder/content/worldpack/WorldPack.cpp
    src/thunder/content/worldpack/WorldPackMetadata.cpp
    src/thunder/foundation/io/RandomAccessFile.cpp
)

add_library(Thunder::Runtime ALIAS thunder_runtime)
target_include_directories(thunder_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_features(thunder_runtime PUBLIC cxx_std_23)
target_link_libraries(thunder_runtime PUBLIC Threads::Threads)

# Zstd is optional at source level, but strongly recommended for .thunderworld packs.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
endif()
if(TARGET PkgConfig::ZSTD)
    target_link_libraries(thunder_runtime PRIVATE PkgConfig::ZSTD)
    target_compile_definitions(thunder_runtime PUBLIC THUNDER_HAS_ZSTD=1)
endif()
if(PkgConfig_FOUND)
    pkg_check_modules(XXHASH QUIET IMPORTED_TARGET libxxhash)
endif()
if(TARGET PkgConfig::XXHASH)
    target_link_libraries(thunder_runtime PRIVATE PkgConfig::XXHASH)
    target_compile_definitions(thunder_runtime PUBLIC THUNDER_HAS_XXHASH=1)
endif()

thunder_apply_warnings(thunder_runtime)

# 引擎命令行工具：目标名与 src/apps/<目标名>.cpp 同名，一律只链接 thunder_runtime。
if(THUNDER_BUILD_TOOLS)
    thunder_add_tool(thunder_cli)
    thunder_add_tool(thunder_world_compiler)
    thunder_add_tool(thunder_world_inspect)
    thunder_add_tool(thunder_world_validate)
    thunder_add_tool(thunder_asset_cooker)
    thunder_add_tool(thunder_asset_inspect)
    thunder_add_tool(thunder_architecture_cooker)
    thunder_add_tool(thunder_material_cooker)
    thunder_add_tool(thunder_map_editor)
    thunder_add_tool(thunder_gui_editor)
endif()

if(THUNDER_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    include(FetchContent)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        URL "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/SDL3-3.4.14.zip"
        URL_HASH SHA256=b07ef7b5431cea0cb2ea9613e2d8410f9ef37f5627f82f1733086cb00aa2b4b4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL3)

    add_library(thunder_vulkan_backend STATIC
        src/thunder/presentation/render/vulkan/VulkanDesktopBackend.cpp
        src/thunder/presentation/render/vulkan/VulkanDevice.cpp
        src/thunder/presentation/render/vulkan/VulkanFrame.cpp
        src/thunder/presentation/render/vulkan/VulkanLivingMap.cpp
        src/thunder/presentation/render/vulkan/VulkanPostProcess.cpp
        src/thunder/presentation/render/vulkan/VulkanRuntimeRenderer.cpp
        src/thunder/presentation/render/vulkan/VulkanScene.cpp
        src/thunder/presentation/render/vulkan/VulkanUi.cpp
        src/thunder/presentation/render/vulkan/VulkanUiResources.cpp
        src/thunder/presentation/render/vulkan/VulkanUiStaging.cpp
        src/thunder/presentation/render/vulkan/VulkanWorldMap.cpp
    )
    target_include_directories(thunder_vulkan_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(thunder_vulkan_backend PUBLIC thunder_runtime SDL3::SDL3 Vulkan::Vulkan)
    thunder_apply_warnings(thunder_vulkan_backend)

    find_program(THUNDER_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
    find_program(THUNDER_SPIRV_VAL spirv-val HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
    file(GLOB THUNDER_SHADER_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp")
    set(THUNDER_SHADER_BINARIES)
    foreach(shader_source IN LISTS THUNDER_SHADER_SOURCES)
        get_filename_component(shader_name "${shader_source}" NAME)
        set(shader_binary "${CMAKE_BINARY_DIR}/thunder-shaders/${shader_name}.spv")
        add_custom_command(
            OUTPUT "${shader_binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/thunder-shaders"
            COMMAND "${THUNDER_GLSLC}" -O "${shader_source}" -o "${shader_binary}"
            COMMAND "${THUNDER_SPIRV_VAL}" "${shader_binary}"
            DEPENDS "${shader_source}"
            VERBATIM)
        list(APPEND THUNDER_SHADER_BINARIES "${shader_binary}")
    endforeach()
    add_custom_target(thunder_shaders ALL DEPENDS ${THUNDER_SHADER_BINARIES})

    add_dependencies(thunder_vulkan_backend thunder_shaders)
    add_library(Thunder::VulkanBackend ALIAS thunder_vulkan_backend)

    # Near-view material showcase (standalone SDL3+Vulkan demo for the P0
    # terrain material sets). Reads raw RGB8 textures converted from the pack
    # PNGs by tools/convert_grass_textures.py.
    add_executable(thunder_grass_showcase src/apps/thunder_grass_showcase.cpp)
    add_executable(thunder_world_bench src/apps/thunder_world_bench.cpp)
    target_link_libraries(thunder_world_bench PRIVATE thunder_vulkan_backend)
    thunder_apply_warnings(thunder_world_bench)
    add_dependencies(thunder_world_bench thunder_shaders)
    thunder_bundle_runtime_dlls(thunder_world_bench)
    add_executable(thunder_ui_gallery src/apps/thunder_ui_gallery.cpp)
    target_link_libraries(thunder_ui_gallery PRIVATE thunder_vulkan_backend)
    thunder_apply_warnings(thunder_ui_gallery)
    add_dependencies(thunder_ui_gallery thunder_shaders)
    thunder_bundle_runtime_dlls(thunder_ui_gallery)
    target_link_libraries(thunder_grass_showcase PRIVATE SDL3::SDL3 Vulkan::Vulkan)
    thunder_apply_warnings(thunder_grass_showcase)
    add_dependencies(thunder_grass_showcase thunder_shaders)
    thunder_bundle_runtime_dlls(thunder_grass_showcase)

endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(thunder_runtime PRIVATE dl)
endif()

if(THUNDER_BUILD_TESTS)
    enable_testing()
    if(MSVC)
        add_compile_options(/UNDEBUG)
    else()
        add_compile_options(-UNDEBUG)
    endif()

    function(thunder_add_test target_name source_file)
        add_executable(${target_name} ${source_file})
        target_link_libraries(${target_name} PRIVATE thunder_runtime)
        target_compile_definitions(${target_name} PRIVATE THUNDER_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        thunder_apply_warnings(${target_name})
        thunder_bundle_runtime_dlls(${target_name})
        add_test(NAME ${target_name} COMMAND ${target_name})
    endfunction()

    thunder_add_test(thunder_tests tests/thunder_tests.cpp)
    thunder_add_test(thunder_economy_tests tests/economy_tests.cpp)
    thunder_add_test(thunder_living_tests tests/living_map_tests.cpp)
    thunder_add_test(thunder_engine_tests tests/engine_tests.cpp)
    thunder_add_test(thunder_asset_ui_tests tests/asset_ui_tests.cpp)
    thunder_add_test(thunder_scripted_gui_tests tests/scripted_gui_tests.cpp)
    thunder_add_test(thunder_logic_layer_tests tests/logic_layer_tests.cpp)
    thunder_add_test(thunder_script_runtime_tests tests/thunderscript_runtime_tests.cpp)
    thunder_add_test(thunder_world_bootstrap_tests tests/world_bootstrap_tests.cpp)
    thunder_add_test(thunder_strategy_system_tests tests/strategy_system_tests.cpp)
    thunder_add_test(thunder_runtime_save_tests tests/runtime_save_tests.cpp)
    thunder_add_test(thunder_replay_journal_tests tests/replay_journal_tests.cpp)
    thunder_add_test(thunder_research_tests tests/research_tests.cpp)
    thunder_add_test(thunder_state_contract_tests tests/state_contract_tests.cpp)
    thunder_add_test(thunder_mod_pipeline_tests tests/mod_pipeline_tests.cpp)
    thunder_add_test(thunder_notification_tests tests/notification_tests.cpp)
    thunder_add_test(thunder_hierarchical_systems_tests tests/hierarchical_systems_tests.cpp)
    thunder_add_test(thunder_memory_save_opt_tests tests/memory_save_opt_tests.cpp)
    thunder_add_test(thunder_script_vm_opt_tests tests/script_vm_opt_tests.cpp)
    thunder_add_test(thunder_gpu_driven_opt_tests tests/gpu_driven_opt_tests.cpp)
    thunder_add_test(thunder_vector_map_and_ui_tests tests/vector_map_and_ui_tests.cpp)
    thunder_add_test(thunder_grand_strategy_advanced_tests tests/grand_strategy_advanced_tests.cpp)
    thunder_add_test(thunder_tooltip_stack_tests tests/tooltip_stack_tests.cpp)
    thunder_add_test(thunder_ui_theme_tests tests/ui_theme_tests.cpp)
    thunder_add_test(thunder_slot_pool_tests tests/slot_pool_tests.cpp)
    thunder_add_test(thunder_mesh_pipeline_tests tests/mesh_pipeline_tests.cpp)
    thunder_add_test(thunder_map_editor_tests tests/map_editor_tests.cpp)
    thunder_add_test(thunder_job_stress tests/job_system_stress.cpp)

    add_executable(thunder_bench bench/thunder_bench.cpp)
    target_link_libraries(thunder_bench PRIVATE thunder_runtime)
    thunder_apply_warnings(thunder_bench)
    thunder_bundle_runtime_dlls(thunder_bench)

    add_executable(thunder_economy_bench bench/economy_bench.cpp)
    target_link_libraries(thunder_economy_bench PRIVATE thunder_runtime)
    thunder_apply_warnings(thunder_economy_bench)
    thunder_bundle_runtime_dlls(thunder_economy_bench)

    add_executable(thunder_living_map_bench bench/living_map_bench.cpp)
    target_link_libraries(thunder_living_map_bench PRIVATE thunder_runtime)
    thunder_apply_warnings(thunder_living_map_bench)
    thunder_bundle_runtime_dlls(thunder_living_map_bench)
endif()
