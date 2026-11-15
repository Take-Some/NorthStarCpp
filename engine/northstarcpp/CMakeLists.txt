function(northstarcpp_add_bullet)
    set(BULLET_PHYSICS_SOURCE_DIR "${NORTHSTARCPP_VENDOR_SOURCE_DIR}/bullet-3.25")
    file(STRINGS "${BULLET_PHYSICS_SOURCE_DIR}/VERSION" BULLET_VERSION LIMIT_COUNT 1)
    set(BUILD_SHARED_LIBS OFF)
    set(INSTALL_LIBS OFF)
    foreach(component IN ITEMS LinearMath BulletCollision BulletDynamics)
        add_subdirectory("${BULLET_PHYSICS_SOURCE_DIR}/src/${component}"
            "bullet/${component}" EXCLUDE_FROM_ALL)
    endforeach()
    target_include_directories(BulletDynamics SYSTEM INTERFACE "${BULLET_PHYSICS_SOURCE_DIR}/src")
    target_link_libraries(BulletCollision PUBLIC LinearMath)
    target_link_libraries(BulletDynamics PUBLIC BulletCollision)
endfunction()

northstarcpp_add_bullet()

function(northstarcpp_add_jolt)
    # Vendored, version-pinned Jolt 5.6.0 (MIT); compiled static into the provider DLL.
    set(JOLT_PHYSICS_SOURCE_DIR "${NORTHSTARCPP_VENDOR_SOURCE_DIR}/JoltPhysics")
    if(NOT EXISTS "${JOLT_PHYSICS_SOURCE_DIR}/Build/CMakeLists.txt")
        message(FATAL_ERROR "Missing Jolt Physics source tree: ${JOLT_PHYSICS_SOURCE_DIR}")
    endif()
    set(JPH_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
    set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
    set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
    set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${JOLT_PHYSICS_SOURCE_DIR}/Build" "jolt" EXCLUDE_FROM_ALL)
    # Avoid oversized GCC precompiled headers on MinGW: they can fail to write.
    if(MINGW)
        set_property(TARGET Jolt PROPERTY DISABLE_PRECOMPILE_HEADERS ON)
    endif()
endfunction()

northstarcpp_add_jolt()



function(northstarcpp_module target capability provider source)
    # Enforce a single source tree for provider implementations.
    if(NOT "${source}" MATCHES "^modules/${capability}/${provider}/[^/]+[.]cpp$")
        message(FATAL_ERROR "Plugin ${target} must live under modules/${capability}/${provider}/")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        message(FATAL_ERROR "Plugin ${target} source missing: ${source}")
    endif()
    add_library(${target} SHARED ${source})
    target_compile_definitions(${target} PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(${target} PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(${target} PRIVATE -static)
    set(selection DEFAULT)
    if(capability STREQUAL "physics" OR (capability STREQUAL "render" AND provider STREQUAL "d3d12"))
        unset(selection)
    endif()
    northstarcpp_register_plugin(TARGET ${target} CAPABILITY ${capability}
        PROVIDER ${provider} ${selection})
endfunction()

northstarcpp_module(northstarcpp_ecs_flecs ecs flecs modules/ecs/flecs/flecs.cpp)
target_compile_definitions(northstarcpp_ecs_flecs PRIVATE NORTHSTARCPP_ECS_BUILD)
target_link_libraries(northstarcpp_ecs_flecs PRIVATE flecs_static)

northstarcpp_module(northstarcpp_physics_bullet physics bullet modules/physics/bullet/bullet.cpp)
target_link_libraries(northstarcpp_physics_bullet PRIVATE BulletDynamics)

northstarcpp_module(northstarcpp_physics_jolt physics jolt modules/physics/jolt/jolt.cpp)
target_link_libraries(northstarcpp_physics_jolt PRIVATE Jolt)

# First-party UI geometry is shared by the D3D11 and D3D12 backends.
add_library(northstarcpp_ui INTERFACE)
target_include_directories(northstarcpp_ui INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/ui>"
    "$<INSTALL_INTERFACE:sdk/include/northstarcpp/ui>")
northstarcpp_module(northstarcpp_render_d3d11 render d3d11 modules/render/d3d11/d3d11.cpp)
target_sources(northstarcpp_render_d3d11 PRIVATE modules/render/d3d11/shader_compiler.cpp)
target_sources(northstarcpp_render_d3d11 PRIVATE modules/render/d3d11/renderer_geometry.cpp)
target_sources(northstarcpp_render_d3d11 PRIVATE
    modules/render/d3d11/renderer_device.cpp
    modules/render/d3d11/renderer_resources.cpp
    modules/render/d3d11/renderer_hud.cpp
    modules/render/d3d11/renderer_sky.cpp
    modules/render/d3d11/renderer_characters.cpp
    modules/render/d3d11/renderer_materials.cpp
    modules/render/d3d11/renderer_surfaces.cpp
    modules/render/d3d11/renderer_frame.cpp
)
target_link_libraries(northstarcpp_render_d3d11 PRIVATE northstarcpp_ui northstarcpp_math d3d11 dxgi d3dcompiler_47 dxguid)
target_include_directories(northstarcpp_render_d3d11 PRIVATE "${northstarcpp_engine_sources}")
target_compile_definitions(northstarcpp_render_d3d11 PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)

northstarcpp_module(northstarcpp_render_d3d12 render d3d12 modules/render/d3d12/d3d12.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE modules/render/d3d12/shader_compiler.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE modules/render/d3d12/renderer_geometry.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE
    modules/render/d3d12/renderer_device.cpp
    modules/render/d3d12/renderer_resources.cpp
    modules/render/d3d12/renderer_materials.cpp
    modules/render/d3d12/renderer_scene.cpp
    modules/render/d3d12/renderer_diagnostics.cpp
    modules/render/d3d12/renderer_frame.cpp
    modules/render/d3d12/renderer_resource_states.cpp
    modules/render/d3d12/renderer_transient_alias.cpp
    modules/render/d3d12/renderer_texture_pool.cpp
    modules/render/d3d12/renderer_pass_barriers.cpp
    modules/render/d3d12/renderer_pass_shadow.cpp
    modules/render/d3d12/renderer_pass_gbuffer.cpp
    modules/render/d3d12/renderer_pass_deferred.cpp
    modules/render/d3d12/renderer_pass_postfx.cpp
    modules/render/d3d12/renderer_pass_present.cpp
)
target_link_libraries(northstarcpp_render_d3d12 PRIVATE northstarcpp_ui northstarcpp_math d3d12 dxgi d3dcompiler_47 dxguid)
target_include_directories(northstarcpp_render_d3d12 PRIVATE "${northstarcpp_engine_sources}")
target_compile_definitions(northstarcpp_render_d3d12 PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)

if(BUILD_TESTING)
    add_executable(northstarcpp_d3d11_material_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d11_material_smoke.cpp")
    target_include_directories(northstarcpp_d3d11_material_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_d3d11_material_smoke PRIVATE -static)
    # One acceptance executable tests both plug-in render providers.
    add_dependencies(northstarcpp_d3d11_material_smoke
        northstarcpp_render_d3d11 northstarcpp_render_d3d12)
    add_test(NAME northstarcpp_d3d11_material_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d11_material_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d11>")
    set_tests_properties(northstarcpp_d3d11_material_warp PROPERTIES TIMEOUT 40)
    add_test(NAME northstarcpp_d3d12_material_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d11_material_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_material_warp PROPERTIES TIMEOUT 45)
    add_test(NAME northstarcpp_d3d12_material_hardware
        COMMAND "$<TARGET_FILE:northstarcpp_d3d11_material_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_material_hardware PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 55)
    add_test(NAME northstarcpp_d3d11_material_hardware
        COMMAND "$<TARGET_FILE:northstarcpp_d3d11_material_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d11>" --hardware)
    set_tests_properties(northstarcpp_d3d11_material_hardware PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 55)


    add_executable(northstarcpp_d3d12_enhanced_barriers_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_enhanced_barriers_smoke.cpp")
    target_compile_features(northstarcpp_d3d12_enhanced_barriers_smoke PRIVATE cxx_std_17)
    target_link_libraries(northstarcpp_d3d12_enhanced_barriers_smoke PRIVATE d3d12 dxgi)
    add_test(NAME northstarcpp_d3d12_enhanced_barriers_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_enhanced_barriers_smoke>")
    set_tests_properties(northstarcpp_d3d12_enhanced_barriers_warp
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(northstarcpp_d3d12_transient_alias_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_transient_alias_smoke.cpp"
        modules/render/d3d12/renderer_transient_alias.cpp
        modules/render/d3d12/renderer_resource_states.cpp)
    target_compile_features(northstarcpp_d3d12_transient_alias_smoke PRIVATE cxx_std_17)
    target_link_libraries(northstarcpp_d3d12_transient_alias_smoke PRIVATE d3d12 dxgi)
    add_test(NAME northstarcpp_d3d12_transient_alias_placed
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_transient_alias_smoke>")
    set_tests_properties(northstarcpp_d3d12_transient_alias_placed
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(northstarcpp_d3d12_texture_pool_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_texture_pool_smoke.cpp"
        modules/render/d3d12/renderer_texture_pool.cpp
        modules/render/d3d12/renderer_resource_states.cpp)
    target_compile_features(northstarcpp_d3d12_texture_pool_smoke PRIVATE cxx_std_17)
    target_link_libraries(northstarcpp_d3d12_texture_pool_smoke PRIVATE d3d12 dxgi)
    add_test(NAME northstarcpp_d3d12_texture_pool
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_texture_pool_smoke>")
    set_tests_properties(northstarcpp_d3d12_texture_pool PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(northstarcpp_d3d12_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_smoke.cpp")
    target_include_directories(northstarcpp_d3d12_smoke PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_d3d12_smoke PRIVATE -static)
    add_dependencies(northstarcpp_d3d12_smoke northstarcpp_render_d3d12)
    add_test(NAME northstarcpp_d3d12_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_warp PROPERTIES TIMEOUT 45)
    add_test(NAME northstarcpp_d3d12_hardware
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_hardware PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_test(NAME northstarcpp_d3d12_warp_stress
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>" --stress)
    set_tests_properties(northstarcpp_d3d12_warp_stress PROPERTIES TIMEOUT 120)

    # Optional visual effects must be validated with real GPU command
    # execution, not only HLSL compilation or CPU scene checks.
    add_test(NAME northstarcpp_d3d12_postfx_shadow_warp
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_postfx_shadow_warp
        PROPERTIES TIMEOUT 120)
    add_test(NAME northstarcpp_d3d12_postfx_shadow_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_postfx_shadow_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)

    add_test(NAME northstarcpp_d3d12_csm_hdr_warp
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_csm_hdr_warp
        PROPERTIES TIMEOUT 140)
    add_test(NAME northstarcpp_d3d12_csm_hdr_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_csm_hdr_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)

    # Physically aliased G-buffer Normal / Bloom, validated with native GPU
    # execution, readback and resize rather than CPU lifetime tests alone.
    add_test(NAME northstarcpp_d3d12_transient_alias_warp
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_transient_alias_warp
        PROPERTIES TIMEOUT 140)
    add_test(NAME northstarcpp_d3d12_transient_alias_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_transient_alias_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)
    add_test(NAME northstarcpp_d3d12_transient_alias_stress_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware --stress)
    set_tests_properties(northstarcpp_d3d12_transient_alias_stress_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    add_test(NAME northstarcpp_d3d12_transient_alias_ldr_warp
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_DISABLE_FXAA=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_transient_alias_ldr_warp
        PROPERTIES TIMEOUT 140)

    # Resource-state acceptance for PostFX composition permutations.
    add_test(NAME northstarcpp_d3d12_hdr_no_fxaa_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_HDR=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            NORTHSTARCPP_DISABLE_FXAA=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_hdr_no_fxaa_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)
    add_test(NAME northstarcpp_d3d12_bloom_no_fxaa_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_HDR=0
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_DISABLE_FXAA=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_bloom_no_fxaa_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)
    add_test(NAME northstarcpp_d3d12_no_postfx_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_HDR=0
            NORTHSTARCPP_ENABLE_BLOOM=0
            NORTHSTARCPP_ENABLE_SHADOWS=0
            NORTHSTARCPP_DISABLE_FXAA=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>" --hardware)
    set_tests_properties(northstarcpp_d3d12_no_postfx_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 100)
endif()

# Backend-independent scheduling contract and smoke coverage.
if(BUILD_TESTING)
    add_executable(northstarcpp_render_graph_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_graph_smoke.cpp")
    target_compile_features(northstarcpp_render_graph_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_graph
        COMMAND "$<TARGET_FILE:northstarcpp_render_graph_smoke>")
    # Vulkan synchronization2 CPU contract is optional: the active runtime
    # has D3D11/D3D12 providers but no Vulkan engine.render implementation.
    find_path(NORTHSTARCPP_VULKAN_INCLUDE_DIR vulkan/vulkan.h
        PATHS "$ENV{VULKAN_SDK}/Include" "$ENV{VULKAN_SDK}/include"
              "C:/msys64/mingw64/include" "C:/VulkanSDK/Include")
    if(NORTHSTARCPP_VULKAN_INCLUDE_DIR)
        add_executable(northstarcpp_render_graph_vulkan_sync_smoke
            "${PROJECT_SOURCE_DIR}/tests/render_graph_vulkan_sync_smoke.cpp")
        target_include_directories(northstarcpp_render_graph_vulkan_sync_smoke PRIVATE
            "${NORTHSTARCPP_VULKAN_INCLUDE_DIR}")
        target_compile_features(northstarcpp_render_graph_vulkan_sync_smoke PRIVATE cxx_std_17)
        add_test(NAME northstarcpp_render_graph_vulkan_sync
            COMMAND "$<TARGET_FILE:northstarcpp_render_graph_vulkan_sync_smoke>")
        add_executable(northstarcpp_render_graph_vulkan_executor_smoke
            "${PROJECT_SOURCE_DIR}/tests/render_graph_vulkan_executor_smoke.cpp")
        target_include_directories(northstarcpp_render_graph_vulkan_executor_smoke PRIVATE
            "${NORTHSTARCPP_VULKAN_INCLUDE_DIR}")
        target_compile_features(northstarcpp_render_graph_vulkan_executor_smoke PRIVATE cxx_std_17)
        add_test(NAME northstarcpp_render_graph_vulkan_executor
            COMMAND "$<TARGET_FILE:northstarcpp_render_graph_vulkan_executor_smoke>")

    endif()
    add_executable(northstarcpp_render_transient_resource_planner_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_transient_resource_planner_smoke.cpp")
    target_compile_features(northstarcpp_render_transient_resource_planner_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_transient_resource_planner
        COMMAND "$<TARGET_FILE:northstarcpp_render_transient_resource_planner_smoke>")

    add_executable(northstarcpp_d3d12_pass_graph_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_pass_graph_smoke.cpp")
    target_compile_features(northstarcpp_d3d12_pass_graph_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_d3d12_pass_graph
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_pass_graph_smoke>")

    add_executable(northstarcpp_deferred_math_smoke
        "${PROJECT_SOURCE_DIR}/tests/deferred_math_smoke.cpp")
    target_compile_features(northstarcpp_deferred_math_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_deferred_math
        COMMAND "$<TARGET_FILE:northstarcpp_deferred_math_smoke>")
    add_executable(northstarcpp_render_tiled_lights_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_tiled_lights_smoke.cpp")
    target_compile_features(northstarcpp_render_tiled_lights_smoke PRIVATE cxx_std_17)
    target_include_directories(northstarcpp_render_tiled_lights_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_render_tiled_lights_smoke PRIVATE
        northstarcpp_math)
    add_test(NAME northstarcpp_render_tiled_lights
        COMMAND "$<TARGET_FILE:northstarcpp_render_tiled_lights_smoke>")
    add_executable(northstarcpp_render_cascaded_shadows_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_cascaded_shadows_smoke.cpp")
    target_compile_features(northstarcpp_render_cascaded_shadows_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_cascaded_shadows
        COMMAND "$<TARGET_FILE:northstarcpp_render_cascaded_shadows_smoke>")
endif()

northstarcpp_module(northstarcpp_audio_winmm audio winmm modules/audio/winmm/winmm.cpp)
target_link_libraries(northstarcpp_audio_winmm PRIVATE winmm)

# Replaceable logger: UTF-8 structured .ulog, plain .log, console, debugger.
northstarcpp_module(northstarcpp_logger_multisink logger multisink
    modules/logger/multisink/multisink.cpp)

# Perlin is linked into the engine. This optional DLL exercises the same ABI
# and can be shipped independently as an override.
if(BUILD_TESTING)
    add_library(northstarcpp_perlin_improved SHARED modules/perlin/improved/improved.cpp)
    target_compile_definitions(northstarcpp_perlin_improved PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_perlin_improved PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_perlin_improved PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_perlin_improved)
    set_target_properties(northstarcpp_perlin_improved PROPERTIES
        PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
endif()

# First native JavaScript scripting provider. Compiled only with a toolchain-
# compatible V8 monolith SDK; the stable host API is always available.
set(NORTHSTARCPP_V8_ROOT "" CACHE PATH "Root of an installed V8 monolith SDK")
set(NORTHSTARCPP_HAS_V8 FALSE)
if(NORTHSTARCPP_V8_ROOT)
    # The main MINGW64/MSVCRT build must never link against UCRT64 V8 C++.
    # An MSYS2 V8 SDK ships dynamic import libraries as well as monolith.a.
    if(EXISTS "${NORTHSTARCPP_V8_ROOT}/lib/libv8.dll.a" OR
       EXISTS "${NORTHSTARCPP_V8_ROOT}/bin/libv8.dll")
        message(FATAL_ERROR
            "UCRT64/dynamic V8 SDK detected: use tools/v8_provider with UCRT64 GCC, not the main engine toolchain")
    endif()
    find_path(NORTHSTARCPP_V8_INCLUDE_DIR v8.h
        PATHS "${NORTHSTARCPP_V8_ROOT}/include" NO_DEFAULT_PATH)
    find_library(NORTHSTARCPP_V8_MONOLITH_LIBRARY NAMES v8_monolith
        PATHS "${NORTHSTARCPP_V8_ROOT}/lib"
              "${NORTHSTARCPP_V8_ROOT}/lib/Release"
              "${NORTHSTARCPP_V8_ROOT}/out.gn/x64.release/obj"
        NO_DEFAULT_PATH)
    if(NOT NORTHSTARCPP_V8_INCLUDE_DIR OR NOT NORTHSTARCPP_V8_MONOLITH_LIBRARY)
        message(FATAL_ERROR "NORTHSTARCPP_V8_ROOT must contain include/v8.h and a compatible libv8_monolith")
    endif()
    northstarcpp_module(northstarcpp_scripting_v8 scripting v8
        modules/scripting/v8/v8.cpp)
    target_compile_features(northstarcpp_scripting_v8 PRIVATE cxx_std_20)
    target_include_directories(northstarcpp_scripting_v8 SYSTEM PRIVATE
        "${NORTHSTARCPP_V8_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_scripting_v8 PRIVATE
        "${NORTHSTARCPP_V8_MONOLITH_LIBRARY}"
        winmm dbghelp shlwapi ws2_32 version)
    set(NORTHSTARCPP_HAS_V8 TRUE)
else()
    option(NORTHSTARCPP_V8_SIDECAR "Bundle pinned V8 for local Windows development and QA" ON)
    if(NORTHSTARCPP_V8_SIDECAR)
        include("${PROJECT_SOURCE_DIR}/tools/v8_provider_sidecar/runtime.cmake")
        northstarcpp_module(northstarcpp_scripting_v8_sidecar scripting v8_sidecar
            modules/scripting/v8_sidecar/v8_sidecar.cpp)
        target_compile_features(northstarcpp_scripting_v8_sidecar PRIVATE cxx_std_20)
        add_dependencies(northstarcpp_scripting_v8_sidecar northstarcpp_v8_sidecar_runtime)
    endif()
endif()

# Replaceable input capability: Win32 keyboard/mouse and dynamically loaded XInput.
northstarcpp_module(northstarcpp_input_win32 input win32 modules/input/win32/win32_xinput.cpp)

northstarcpp_module(northstarcpp_assets_directory assets directory modules/assets/directory/directory.cpp)
target_sources(northstarcpp_assets_directory PRIVATE modules/assets/directory/fat_archive.cpp)
target_include_directories(northstarcpp_assets_directory SYSTEM PRIVATE "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")

# The app depends on every registered module, including modules added in future.
get_property(_registered_plugins GLOBAL PROPERTY NORTHSTARCPP_MODULE_REGISTRY)
set(northstarcpp_default_modules "")
foreach(_entry IN LISTS _registered_plugins)
    string(REPLACE "|" ";" _fields "${_entry}")
    list(GET _fields 2 _target)
    list(APPEND northstarcpp_default_modules "${_target}")
endforeach()

# First-class model and animation libraries shared by Player and Qt Editor.
# Retain the already working resource formats and playback implementation.
add_library(northstarcpp_models STATIC modules/models/models.cpp)
northstarcpp_unprefix_module_output(northstarcpp_models)
target_include_directories(northstarcpp_models PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:sdk/include>")
target_compile_features(northstarcpp_models PUBLIC cxx_std_17)

add_library(northstarcpp_animation STATIC
    modules/animation/animation.cpp
    modules/animation/camera_track.cpp
    modules/animation/skeletal_animation.cpp
    modules/animation/character_facing.cpp)
northstarcpp_unprefix_module_output(northstarcpp_animation)
target_include_directories(northstarcpp_animation PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:sdk/include>")
target_include_directories(northstarcpp_animation PRIVATE "${northstarcpp_engine_sources}")
target_include_directories(northstarcpp_animation SYSTEM PRIVATE
    "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
target_compile_features(northstarcpp_animation PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_skeletal_animation_smoke
        "${PROJECT_SOURCE_DIR}/tests/skeletal_animation_smoke.cpp")
    target_link_libraries(northstarcpp_skeletal_animation_smoke PRIVATE
        northstarcpp_animation northstarcpp_math)
    target_link_options(northstarcpp_skeletal_animation_smoke PRIVATE -static)
    add_test(NAME northstarcpp_skeletal_animation
        COMMAND "$<TARGET_FILE:northstarcpp_skeletal_animation_smoke>")
    set_tests_properties(northstarcpp_skeletal_animation PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_character_facing_smoke
        "${PROJECT_SOURCE_DIR}/tests/character_facing_smoke.cpp")
    target_link_libraries(northstarcpp_character_facing_smoke PRIVATE
        northstarcpp_animation northstarcpp_math)
    target_link_options(northstarcpp_character_facing_smoke PRIVATE -static)
    add_test(NAME northstarcpp_character_facing
        COMMAND "$<TARGET_FILE:northstarcpp_character_facing_smoke>")
    set_tests_properties(northstarcpp_character_facing PROPERTIES TIMEOUT 20)

    add_executable(northstarcpp_material_functions_smoke
        "${PROJECT_SOURCE_DIR}/tests/material_functions_smoke.cpp")
    target_link_libraries(northstarcpp_material_functions_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_material_functions_smoke PRIVATE -static)
    add_dependencies(northstarcpp_material_functions_smoke
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_material_functions
        COMMAND "${CMAKE_COMMAND}"
        -E env
            "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
            "$<TARGET_FILE:northstarcpp_material_functions_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    set_tests_properties(northstarcpp_material_functions PROPERTIES TIMEOUT 30)

    add_executable(northstarcpp_material_graph_typed_smoke
        "${PROJECT_SOURCE_DIR}/tests/material_graph_typed_smoke.cpp")
    target_link_libraries(northstarcpp_material_graph_typed_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_material_graph_typed_smoke PRIVATE -static)
    add_test(NAME northstarcpp_material_graph_typed
        COMMAND "$<TARGET_FILE:northstarcpp_material_graph_typed_smoke>")
    set_tests_properties(northstarcpp_material_graph_typed PROPERTIES TIMEOUT 20)
    if(WIN32)
        add_executable(northstarcpp_material_graph_hlsl_smoke
            "${PROJECT_SOURCE_DIR}/tests/material_graph_hlsl_smoke.cpp")
        target_include_directories(northstarcpp_material_graph_hlsl_smoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/render/shared")
        target_link_libraries(northstarcpp_material_graph_hlsl_smoke PRIVATE
            northstarcpp_runtime d3dcompiler_47)
        target_link_options(northstarcpp_material_graph_hlsl_smoke PRIVATE -static)
        add_test(NAME northstarcpp_material_graph_hlsl
            COMMAND "$<TARGET_FILE:northstarcpp_material_graph_hlsl_smoke>"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
        set_tests_properties(northstarcpp_material_graph_hlsl PROPERTIES TIMEOUT 30)
    endif()

    add_executable(northstarcpp_material_resources_smoke
        "${PROJECT_SOURCE_DIR}/tests/material_resources_smoke.cpp")
    target_link_libraries(northstarcpp_material_resources_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_material_resources_smoke PRIVATE -static)
    add_dependencies(northstarcpp_material_resources_smoke
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_material_resources
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
        "$<TARGET_FILE:northstarcpp_material_resources_smoke>"
        "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_material_resources PROPERTIES TIMEOUT 50)
    add_executable(northstarcpp_character_namespace_rebase_smoke
        "${PROJECT_SOURCE_DIR}/tests/character_namespace_rebase_smoke.cpp")
    target_include_directories(northstarcpp_character_namespace_rebase_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/models")
    target_link_options(northstarcpp_character_namespace_rebase_smoke PRIVATE -static)
    add_test(NAME northstarcpp_character_namespace_rebase
        COMMAND "$<TARGET_FILE:northstarcpp_character_namespace_rebase_smoke>")
    set_tests_properties(northstarcpp_character_namespace_rebase PROPERTIES TIMEOUT 15)

    add_executable(northstarcpp_skeletal_asset_interop_smoke
        "${PROJECT_SOURCE_DIR}/tests/skeletal_asset_interop_smoke.cpp")
    target_link_libraries(northstarcpp_skeletal_asset_interop_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_skeletal_asset_interop_smoke PRIVATE -static)
    add_dependencies(northstarcpp_skeletal_asset_interop_smoke
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_skeletal_asset_interop
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
        "$<TARGET_FILE:northstarcpp_skeletal_asset_interop_smoke>"
        "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_skeletal_asset_interop PROPERTIES TIMEOUT 45)
    # Real game pipeline: YMT + YDD + YCD + YMAT + YTD -> skinned renderer.
    # WARP makes this a reproducible hardware-independent acceptance test.
    add_test(NAME northstarcpp_project_character_warp
        COMMAND "$<TARGET_FILE:northstarcpp_player>"
        --warp --character-smoke --explore
        --scene "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscn"
        --shared-assets "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_project_character_warp PROPERTIES TIMEOUT 110)


endif()

if(BUILD_TESTING)
    add_executable(northstarcpp_foundation_services_smoke
        "${PROJECT_SOURCE_DIR}/tests/foundation_engine_services_smoke.cpp")
    target_link_libraries(northstarcpp_foundation_services_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_foundation_services_smoke PRIVATE -static)
    add_test(NAME northstarcpp_foundation_runtime_services
        COMMAND "$<TARGET_FILE:northstarcpp_foundation_services_smoke>")
endif()


add_library(northstarcpp_runtime STATIC modules/host/scene.cpp modules/host/scene_v3.cpp modules/host/scene_binary.cpp modules/host/scene_nef8.cpp modules/host/scene_nef8_chunked.cpp modules/host/scene_component_registry.cpp modules/host/scene_world.cpp modules/host/physics.cpp modules/host/ecs_world.cpp modules/host/audio.cpp modules/host/logger.cpp modules/host/perlin.cpp modules/host/scripting.cpp modules/host/input.cpp modules/host/assets.cpp modules/host/fps_assets.cpp modules/host/fps_game.cpp modules/host/camera.cpp modules/host/camera_track_assets.cpp modules/host/scene_animation_assets.cpp modules/host/mesh_asset_assets.cpp modules/host/module_loader.cpp)
target_sources(northstarcpp_runtime PRIVATE
    modules/host/asset_list_file.cpp
    modules/models/character.cpp
    modules/animation/skeleton.cpp
    modules/host/save_game.cpp
    modules/host/asset_streaming.cpp
    modules/host/scene_chunk_streaming.cpp
    modules/host/scene_navigation_cells.cpp
    modules/host/asset_zones.cpp
    modules/host/shared_asset_models.cpp
    modules/host/shared_asset_textures.cpp
    modules/host/shared_assets.cpp
    modules/host/shared_asset_dictionary_index.cpp
    modules/host/procedural_environment.cpp
    modules/host/perlin_service.cpp
    modules/host/geometry_service.cpp
    modules/host/provider_inspector.cpp
    modules/host/geometry_scene.cpp
    modules/material/material.cpp
    modules/material/material_graph.cpp
    modules/material/material_functions.cpp)
northstarcpp_unprefix_module_output(northstarcpp_runtime)
target_include_directories(northstarcpp_runtime PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:sdk/include>")
target_include_directories(northstarcpp_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared"
    "${NORTHSTARCPP_VENDOR_ROOT}/bcdec"
    "${northstarcpp_engine_sources}")
northstarcpp_builtin_module(northstarcpp_runtime perlin improved modules/perlin/improved/improved.cpp)
target_sources(northstarcpp_runtime PRIVATE
    modules/host/collision_service.cpp
    modules/collision/ray_triangle_core.cpp)
target_include_directories(northstarcpp_runtime PRIVATE
    "${northstarcpp_engine_sources}/northstarcpp/collision/sources")
northstarcpp_builtin_module(northstarcpp_runtime collision collision
    modules/collision/collision.cpp)
northstarcpp_builtin_module(northstarcpp_runtime geometry geometry modules/geometry/geometry.cpp)
if(BUILD_TESTING)
    add_library(northstarcpp_collision SHARED
        modules/collision/collision.cpp
        modules/collision/ray_triangle_core.cpp)
    target_compile_definitions(northstarcpp_collision PRIVATE
        NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_collision PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/collision")
    target_link_libraries(northstarcpp_collision PRIVATE northstarcpp_math)
    target_link_options(northstarcpp_collision PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_collision)
    set_target_properties(northstarcpp_collision PROPERTIES
        PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
endif()
northstarcpp_builtin_module(northstarcpp_runtime time qpc modules/time/qpc/qpc.cpp)
target_sources(northstarcpp_runtime PRIVATE
    modules/time/frame_clock.cpp)
northstarcpp_builtin_registry(northstarcpp_runtime)
# Bootstrap JSON uses vendored header-only Boost.PropertyTree (no runtime DLL).
target_include_directories(northstarcpp_runtime SYSTEM PRIVATE "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
target_link_libraries(northstarcpp_runtime PUBLIC northstarcpp_math
    northstarcpp_core northstarcpp_models northstarcpp_animation
    PRIVATE northstarcpp_blake3 "$<BUILD_INTERFACE:ZLIB::ZLIB>" "$<INSTALL_INTERFACE:NorthStarCpp::zlib>")
if(WIN32)
    target_link_libraries(northstarcpp_runtime PUBLIC windowscodecs ole32)
endif()
target_compile_features(northstarcpp_runtime PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_library(northstarcpp_time_qpc SHARED modules/time/qpc/qpc.cpp)
    target_compile_definitions(northstarcpp_time_qpc PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_time_qpc PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared" "${northstarcpp_engine_sources}")
    target_link_libraries(northstarcpp_time_qpc PRIVATE northstarcpp_core)
    target_link_options(northstarcpp_time_qpc PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_time_qpc)
    set_target_properties(northstarcpp_time_qpc PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    add_dependencies(northstarcpp_foundation_services_smoke northstarcpp_time_qpc)
    add_test(NAME northstarcpp_foundation_services_external_clock
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_TIME_PLUGIN=$<TARGET_FILE:northstarcpp_time_qpc>"
            "$<TARGET_FILE:northstarcpp_foundation_services_smoke>")
endif()

# Foundation geometry provider uses the same ABI as independently loaded DLLs.
if(BUILD_TESTING)
    add_library(northstarcpp_geometry SHARED
        modules/geometry/geometry.cpp)
    target_compile_definitions(northstarcpp_geometry PRIVATE
        NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_geometry PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared"
        "${northstarcpp_engine_sources}")
    # Direct original source code, not the duplicated modern tessellators.
    target_link_libraries(northstarcpp_geometry PRIVATE
        northstarcpp_core northstarcpp_math)
    target_link_options(northstarcpp_geometry PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_geometry)
    set_target_properties(northstarcpp_geometry PROPERTIES
        PREFIX "" OUTPUT_NAME "geometry_test_fixture"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    # Deliberately NOT an installed module: engine.geometry is linked as
    # a builtin provider. This DLL only verifies external ABI replacement.

    # Compare ABI output *bit-for-bit* against original foundation source
    # through both built-in and independently loaded DLL providers.
    add_executable(northstarcpp_foundation_geometry_parity
        "${PROJECT_SOURCE_DIR}/tests/geometry_foundation_source_parity.cpp")
    target_include_directories(northstarcpp_foundation_geometry_parity PRIVATE
        "${northstarcpp_engine_sources}")
    target_compile_definitions(northstarcpp_foundation_geometry_parity PRIVATE
        NORTHSTARCPP_STATIC_LIBRARIES NORTHSTARCPP_STANDALONE_MATH)
    target_link_libraries(northstarcpp_foundation_geometry_parity PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_foundation_geometry_parity PRIVATE -static)
    add_dependencies(northstarcpp_foundation_geometry_parity
        northstarcpp_geometry)
    add_test(NAME northstarcpp_foundation_source_builtin
        COMMAND "$<TARGET_FILE:northstarcpp_foundation_geometry_parity>")
    add_test(NAME northstarcpp_foundation_source_external
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_GEOMETRY_PLUGIN=$<TARGET_FILE:northstarcpp_geometry>"
            "$<TARGET_FILE:northstarcpp_foundation_geometry_parity>")
    add_executable(northstarcpp_geometry_abi_smoke
        "${PROJECT_SOURCE_DIR}/tests/geometry_abi_smoke.cpp")
    target_link_libraries(northstarcpp_geometry_abi_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_geometry_abi_smoke PRIVATE -static)
    add_test(NAME northstarcpp_foundation_geometry_abi
        COMMAND "$<TARGET_FILE:northstarcpp_geometry_abi_smoke>")

    foreach(_flavor IN ITEMS external invalid)
        add_library(northstarcpp_geometry_${_flavor} MODULE
            "${PROJECT_SOURCE_DIR}/tests/geometry_override_provider.cpp")
        target_compile_definitions(northstarcpp_geometry_${_flavor} PRIVATE
            NORTHSTARCPP_MODULE_BUILD)
        if(_flavor STREQUAL "invalid")
            target_compile_definitions(northstarcpp_geometry_${_flavor} PRIVATE
                NORTHSTARCPP_INVALID_GEOMETRY_FIXTURE)
        endif()
        target_include_directories(northstarcpp_geometry_${_flavor} PRIVATE
            "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
        target_link_options(northstarcpp_geometry_${_flavor} PRIVATE -static)
        northstarcpp_unprefix_module_output(northstarcpp_geometry_${_flavor})
        set_target_properties(northstarcpp_geometry_${_flavor} PROPERTIES
            PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules"
            LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    endforeach()
    add_executable(northstarcpp_geometry_override_smoke
        "${PROJECT_SOURCE_DIR}/tests/geometry_override_smoke.cpp")
    target_link_libraries(northstarcpp_geometry_override_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_geometry_override_smoke PRIVATE -static)
    add_dependencies(northstarcpp_geometry_override_smoke
        northstarcpp_geometry_external northstarcpp_geometry_invalid)
    add_test(NAME northstarcpp_foundation_geometry_override
        COMMAND "${CMAKE_COMMAND}"
            "-DROOT=${PROJECT_BINARY_DIR}/geometry_provider_precedence"
            "-DPROBE=$<TARGET_FILE:northstarcpp_geometry_override_smoke>"
            "-DEXTERNAL=$<TARGET_FILE:northstarcpp_geometry_external>"
            "-DINVALID=$<TARGET_FILE:northstarcpp_geometry_invalid>"
            -P "${PROJECT_SOURCE_DIR}/tests/geometry_override_smoke.cmake")
    set_tests_properties(northstarcpp_foundation_geometry_abi
        northstarcpp_foundation_geometry_override PROPERTIES TIMEOUT 35)
    add_test(NAME northstarcpp_provider_catalog_sync
        COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/test_provider_catalog_sync.py")
    set_tests_properties(northstarcpp_provider_catalog_sync PROPERTIES TIMEOUT 15)
    # Host and Editor share this ABI-validated, read-only provider snapshot.
    add_executable(northstarcpp_provider_inspector_smoke
        "${PROJECT_SOURCE_DIR}/tests/provider_inspector_smoke.cpp")
    target_link_libraries(northstarcpp_provider_inspector_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_provider_inspector_smoke PRIVATE -static)
    add_dependencies(northstarcpp_provider_inspector_smoke
        northstarcpp_geometry_external)
    add_test(NAME northstarcpp_provider_inspector_builtin
        COMMAND "${CMAKE_COMMAND}" -E env
        --unset=NORTHSTARCPP_GEOMETRY_PLUGIN
        "$<TARGET_FILE:northstarcpp_provider_inspector_smoke>" builtin)
    add_test(NAME northstarcpp_provider_inspector_override
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_GEOMETRY_PLUGIN=$<TARGET_FILE:northstarcpp_geometry_external>"
        "$<TARGET_FILE:northstarcpp_provider_inspector_smoke>" external)
    set_tests_properties(northstarcpp_provider_inspector_builtin
        northstarcpp_provider_inspector_override PROPERTIES TIMEOUT 30)
endif()

if(BUILD_TESTING)
    # This test links *through the real engine runtime* to exercise the
    # foundation source implementations, linked as native engine code.
    add_executable(northstarcpp_foundation_original_smoke
        "${PROJECT_SOURCE_DIR}/tests/foundation_original_smoke.cpp")
    target_include_directories(northstarcpp_foundation_original_smoke PRIVATE
        "${northstarcpp_engine_sources}")
    target_compile_definitions(northstarcpp_foundation_original_smoke PRIVATE
        NORTHSTARCPP_STATIC_LIBRARIES NORTHSTARCPP_STANDALONE_MATH)
    target_link_libraries(northstarcpp_foundation_original_smoke PRIVATE
        northstarcpp_runtime)
    add_test(NAME northstarcpp_foundation_original
        COMMAND northstarcpp_foundation_original_smoke)
endif()

if(BUILD_TESTING)
    add_executable(northstarcpp_save_game_smoke
        "${PROJECT_SOURCE_DIR}/tests/save_game_smoke.cpp")
    target_link_libraries(northstarcpp_save_game_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_save_game_smoke PRIVATE -static)
    target_compile_options(northstarcpp_save_game_smoke PRIVATE -UNDEBUG)
    add_test(NAME northstarcpp_save_game
        COMMAND "$<TARGET_FILE:northstarcpp_save_game_smoke>"
                "${PROJECT_BINARY_DIR}/save_game_smoke_storage")
    set_tests_properties(northstarcpp_save_game PROPERTIES TIMEOUT 25)
endif()

if(BUILD_TESTING)
    add_executable(northstarcpp_mesh_asset_smoke
        "${PROJECT_SOURCE_DIR}/tests/mesh_asset_smoke.cpp")
    target_link_libraries(northstarcpp_mesh_asset_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_mesh_asset_smoke PRIVATE -static)
    set_target_properties(northstarcpp_mesh_asset_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene_obj_mesh
        COMMAND "$<TARGET_FILE:northstarcpp_mesh_asset_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/assets/models/pedestal.obj"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/main.nscn")
    set_tests_properties(northstarcpp_scene_obj_mesh PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_scene_animation_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_animation_smoke.cpp")
    target_link_libraries(northstarcpp_scene_animation_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_animation_smoke PRIVATE -static)
    set_target_properties(northstarcpp_scene_animation_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene_animation
        COMMAND "$<TARGET_FILE:northstarcpp_scene_animation_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/main.nscn"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/animations/lift.anim.json")
    set_tests_properties(northstarcpp_scene_animation PROPERTIES TIMEOUT 20)
    northstarcpp_test_perlin_override(northstarcpp_runtime)
    add_executable(northstarcpp_perlin_smoke "${PROJECT_SOURCE_DIR}/tests/perlin_smoke.cpp")
    target_link_libraries(northstarcpp_perlin_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_perlin_smoke PRIVATE -static)
    add_dependencies(northstarcpp_perlin_smoke northstarcpp_perlin_improved)
    set_target_properties(northstarcpp_perlin_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_perlin
        COMMAND "$<TARGET_FILE:northstarcpp_perlin_smoke>"
            default "$<TARGET_FILE:northstarcpp_perlin_improved>")
    add_test(NAME northstarcpp_perlin_reject_incompatible
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PERLIN_PLUGIN=$<TARGET_FILE:northstarcpp_audio_winmm>"
            "$<TARGET_FILE:northstarcpp_perlin_smoke>" reject)
    set_tests_properties(northstarcpp_perlin northstarcpp_perlin_reject_incompatible
        PROPERTIES TIMEOUT 35)
endif()

# Native NSCN migration utility keeps legacy scene files intact.
add_executable(northstarcpp_scene_convert modules/tools/scene_convert.cpp)
target_link_libraries(northstarcpp_scene_convert PRIVATE northstarcpp_runtime)
target_link_options(northstarcpp_scene_convert PRIVATE -static)
set_target_properties(northstarcpp_scene_convert PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
install(TARGETS northstarcpp_scene_convert RUNTIME DESTINATION tools COMPONENT Tools)

# Native content authoring tool: produces an editable, collidable scene V3.
add_executable(perlin_world_generator modules/tools/perlin_world_generator.cpp)
target_link_libraries(perlin_world_generator PRIVATE northstarcpp_runtime)
target_link_options(perlin_world_generator PRIVATE -static)
set_target_properties(perlin_world_generator PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
install(TARGETS perlin_world_generator RUNTIME DESTINATION tools COMPONENT Tools)

if(BUILD_TESTING)
    add_executable(northstarcpp_procedural_environment_smoke
        "${PROJECT_SOURCE_DIR}/tests/procedural_environment_smoke.cpp")
    target_link_libraries(northstarcpp_procedural_environment_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_procedural_environment_smoke PRIVATE -static)
    set_target_properties(northstarcpp_procedural_environment_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_procedural_environment
        COMMAND "$<TARGET_FILE:northstarcpp_procedural_environment_smoke>")
    add_executable(northstarcpp_perlin_service_smoke
        "${PROJECT_SOURCE_DIR}/tests/perlin_service_smoke.cpp")
    target_include_directories(northstarcpp_perlin_service_smoke SYSTEM PRIVATE
        "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_perlin_service_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_perlin_service_smoke PRIVATE -static)
    set_target_properties(northstarcpp_perlin_service_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_perlin_json_service
        COMMAND "$<TARGET_FILE:northstarcpp_perlin_service_smoke>")
    add_test(NAME northstarcpp_perlin_world_export
        COMMAND "$<TARGET_FILE:perlin_world_generator>"
            "${PROJECT_BINARY_DIR}/perlin-test-world.nscn" 2026)
endif()

if(BUILD_TESTING)
    # Test-only provider proves dynamic ABI negotiation without depending on V8.
    add_library(northstarcpp_scripting_mock SHARED
        "${PROJECT_SOURCE_DIR}/tests/scripting_mock_provider.cpp")
    target_compile_definitions(northstarcpp_scripting_mock PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_link_options(northstarcpp_scripting_mock PRIVATE -static)
    target_include_directories(northstarcpp_scripting_mock PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    northstarcpp_unprefix_module_output(northstarcpp_scripting_mock)
    set_target_properties(northstarcpp_scripting_mock PROPERTIES
        PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    add_library(northstarcpp_script_service_fixture MODULE
        "${PROJECT_SOURCE_DIR}/tests/script_service_fixture.cpp")
    target_compile_definitions(northstarcpp_script_service_fixture PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_script_service_fixture PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_script_service_fixture PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_script_service_fixture)
    set_target_properties(northstarcpp_script_service_fixture PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    # Project gameplay rules are tested without requiring the V8 SDK.
    find_program(NORTHSTARCPP_NODE_EXECUTABLE NAMES node)
    if(NORTHSTARCPP_NODE_EXECUTABLE)
        add_test(NAME northstarcpp_entity_event_batch
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/entity_event_batch_smoke.cjs")
        set_tests_properties(northstarcpp_entity_event_batch PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_project_gameplay_scripts
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/gameplay_scripts_smoke.cjs")
        set_tests_properties(northstarcpp_project_gameplay_scripts
            PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_shared_camera_script
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/gameplay_scripts_smoke.cjs" --camera-only)
        set_tests_properties(northstarcpp_shared_camera_script
            PROPERTIES TIMEOUT 15)
        # Guard SDK editor autocomplete against changes in the canonical
        # runtime enum classes; no handwritten duplicate key tables.
        add_test(NAME northstarcpp_input_typings_sync
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/dev/generate_input_typings.py" --check)
        set_tests_properties(northstarcpp_input_typings_sync
            PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_input_enum_classes
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/input_enum_classes_smoke.cjs")
        set_tests_properties(northstarcpp_input_enum_classes
            PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_scene_script_sdk
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/scene_script_sdk_smoke.cjs")
        set_tests_properties(northstarcpp_scene_script_sdk
            PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_declarative_player_scripts
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/gameplay_scripts_smoke.cjs" --player-only)
        set_tests_properties(northstarcpp_declarative_player_scripts
            PROPERTIES TIMEOUT 20)

    endif()

    add_executable(northstarcpp_scripting_smoke
        "${PROJECT_SOURCE_DIR}/tests/scripting_smoke.cpp")
    target_link_libraries(northstarcpp_scripting_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scripting_smoke PRIVATE -static)
    if(TARGET northstarcpp_scripting_v8_sidecar)
        add_executable(northstarcpp_v8_entity_event_abi_smoke
            "${PROJECT_SOURCE_DIR}/tests/v8_entity_event_abi_smoke.cpp")
        target_link_libraries(northstarcpp_v8_entity_event_abi_smoke PRIVATE
            northstarcpp_runtime)
        target_link_options(northstarcpp_v8_entity_event_abi_smoke PRIVATE -static)
        add_dependencies(northstarcpp_v8_entity_event_abi_smoke
            northstarcpp_scripting_v8_sidecar)
        add_test(NAME northstarcpp_v8_entity_event_abi COMMAND
            "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_v8_sidecar>"
            "$<TARGET_FILE:northstarcpp_v8_entity_event_abi_smoke>")
        set_tests_properties(northstarcpp_v8_entity_event_abi PROPERTIES TIMEOUT 35)
    endif()
    if(TARGET northstarcpp_scripting_v8_sidecar)
        add_dependencies(northstarcpp_scripting_smoke northstarcpp_scripting_v8_sidecar)
    endif()
    add_dependencies(northstarcpp_scripting_smoke
        northstarcpp_scripting_mock northstarcpp_script_service_fixture
        northstarcpp_audio_winmm northstarcpp_assets_directory)
    add_test(NAME northstarcpp_script_service_override COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_mock>"
        "NORTHSTARCPP_SCENE_SCRIPT_PLUGIN=$<TARGET_FILE:northstarcpp_script_service_fixture>"
        "NORTHSTARCPP_FIXTURE_SCRIPT_PLUGIN=$<TARGET_FILE:northstarcpp_script_service_fixture>"
        "$<TARGET_FILE:northstarcpp_scripting_smoke>" service)
    set_tests_properties(northstarcpp_script_service_override PROPERTIES TIMEOUT 30)
    set_target_properties(northstarcpp_scripting_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(Python3_EXECUTABLE)
        add_test(NAME northstarcpp_v8_official_sdk_build_plan
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/v8_official_sdk_build_pipeline_smoke.py")
        set_tests_properties(northstarcpp_v8_official_sdk_build_plan
            PROPERTIES TIMEOUT 35)
    endif()
    add_test(NAME northstarcpp_scripting_abi
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_mock>"
            "$<TARGET_FILE:northstarcpp_scripting_smoke>" mock
            "$<TARGET_FILE:northstarcpp_scripting_mock>"
            "${PROJECT_SOURCE_DIR}/tests/fixtures")
    add_test(NAME northstarcpp_scripting_reject_incompatible
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_audio_winmm>"
            "$<TARGET_FILE:northstarcpp_scripting_smoke>" reject)
    if(NORTHSTARCPP_HAS_V8)
        add_dependencies(northstarcpp_scripting_smoke northstarcpp_scripting_v8)
        add_test(NAME northstarcpp_scripting_v8
            COMMAND "${CMAKE_COMMAND}" -E env
                "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_v8>"
                "$<TARGET_FILE:northstarcpp_scripting_smoke>" v8
                "$<TARGET_FILE:northstarcpp_scripting_v8>"
                "${PROJECT_SOURCE_DIR}/tests/fixtures")
    endif()
    set_tests_properties(northstarcpp_scripting_abi
        northstarcpp_scripting_reject_incompatible PROPERTIES TIMEOUT 30)
    if(NORTHSTARCPP_HAS_V8)
        set_tests_properties(northstarcpp_scripting_v8 PROPERTIES TIMEOUT 30)
    endif()
endif()

add_library(northstarcpp_viewport STATIC modules/viewport/viewport.cpp)
northstarcpp_unprefix_module_output(northstarcpp_viewport)
target_include_directories(northstarcpp_viewport PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/host")
target_link_libraries(northstarcpp_viewport PUBLIC northstarcpp_runtime)

# Native Windows crash capture shared by editor, player and Project Manager.
# bugtrap.exe is the out-of-process report viewer; it is part of the default build.
add_library(northstarcpp_crash STATIC modules/shared/crash_handler.cpp)
northstarcpp_unprefix_module_output(northstarcpp_crash)
target_include_directories(northstarcpp_crash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
target_link_libraries(northstarcpp_crash PUBLIC dbghelp)
target_compile_features(northstarcpp_crash PUBLIC cxx_std_17)

add_executable(bugtrap WIN32 modules/shared/bugtrap_main.cpp)
target_link_libraries(bugtrap PRIVATE shell32)
target_link_options(bugtrap PRIVATE -static)
set_target_properties(bugtrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(BUILD_TESTING)
    add_test(NAME northstarcpp_bugtrap_smoke
        COMMAND "$<TARGET_FILE:bugtrap>" --self-test)
endif()

# Native C++ Project Manager. Project catalog is reusable via engine/SDK.
add_library(northstarcpp_projects STATIC modules/project_manager/project_catalog.cpp)
northstarcpp_unprefix_module_output(northstarcpp_projects)
target_include_directories(northstarcpp_projects PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:sdk/include>")
target_include_directories(northstarcpp_projects SYSTEM PRIVATE "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
target_compile_features(northstarcpp_projects PUBLIC cxx_std_17)
target_link_libraries(northstarcpp_projects PUBLIC northstarcpp_runtime)

# Project Browser is a Qt 6 Widgets application; native project catalog stays C++.
# Resolve Qt installed alongside the selected MinGW compiler (MSYS2) without
# assuming a fixed system-wide C:/Qt installation.
get_filename_component(_northstar_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_northstar_compiler_prefix "${_northstar_compiler_bin}" DIRECTORY)
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Concurrent
    HINTS "${_northstar_compiler_prefix}" "$ENV{QTDIR}")
add_executable(northstarcpp_project_manager WIN32
    modules/project_manager/qt_project_browser.cpp)
target_link_libraries(northstarcpp_project_manager PRIVATE
    northstarcpp_projects northstarcpp_runtime northstarcpp_crash Qt6::Widgets)
add_dependencies(northstarcpp_project_manager northstarcpp_logger_multisink)
qt_add_resources(northstarcpp_project_manager northstarcpp_project_browser_resources
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources"
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/fontawesome/fa-solid-900.ttf")
# Qt's resource compiler uses the build-tree path to avoid resolving ICO
# files relative to engine/northstarcpp/resources in older Qt 6 packages.
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
configure_file("${NORTHSTARCPP_ENGINE_ICONS_DIR}/logoStar.ico"
    "${PROJECT_BINARY_DIR}/generated/logoStar.ico" COPYONLY)
qt_add_resources(northstarcpp_project_manager northstarcpp_project_browser_logo
    PREFIX "/"
    BASE "${PROJECT_BINARY_DIR}/generated"
    FILES "${PROJECT_BINARY_DIR}/generated/logoStar.ico")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/fontawesome/LICENSE.txt"
    DESTINATION licenses/FontAwesome COMPONENT Licenses)
add_dependencies(northstarcpp_project_manager bugtrap)
target_compile_definitions(northstarcpp_project_manager PRIVATE UNICODE _UNICODE
    "NORTHSTARCPP_ENGINE_VERSION=\"${PROJECT_VERSION}\"")
target_link_options(northstarcpp_project_manager PRIVATE -static)
# Build-time-only deployment makes the native C++ Qt application executable
# when launched directly from the build tree, including via the editor.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(TARGET northstarcpp_project_manager POST_BUILD
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/deploy_qt.py"
        --developer-executable "$<TARGET_FILE:northstarcpp_project_manager>"
        --gpp "${CMAKE_CXX_COMPILER}"
    COMMENT "Bundling Qt Widgets and Font Awesome Project Browser runtime"
    VERBATIM)
set_target_properties(northstarcpp_project_manager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")

# Qt6 light-mode camera timeline editor. Live path preview and editable
# keyframes use the same serialization code consumed by engine.assets.
add_executable(northstarcpp_camera_timeline_editor WIN32
    modules/tools/camera_timeline_editor.cpp)
target_link_libraries(northstarcpp_camera_timeline_editor PRIVATE
    northstarcpp_runtime Qt6::Widgets)
add_dependencies(northstarcpp_camera_timeline_editor northstarcpp_project_manager)
target_link_options(northstarcpp_camera_timeline_editor PRIVATE -static)
set_target_properties(northstarcpp_camera_timeline_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(BUILD_TESTING)
    add_test(NAME northstarcpp_camera_editor_smoke
        COMMAND "$<TARGET_FILE:northstarcpp_camera_timeline_editor>" --self-test)
endif()

add_library(northstarcpp_editor_tools STATIC
    modules/editor/editor_tools.cpp
    modules/editor/editor_prefab.cpp
    modules/editor/editor_prefab_composition.cpp
    modules/editor/editor_camera.cpp
    modules/editor/editor_mesh_picking.cpp
    modules/editor/editor_asset_thumbnails.cpp
    modules/editor/editor_asset_preview_3d.cpp
    modules/editor/editor_asset_material_preview.cpp)
target_include_directories(northstarcpp_editor_tools PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor"
    "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
    PRIVATE "${NORTHSTARCPP_VENDOR_ROOT}/bcdec")
target_link_libraries(northstarcpp_editor_tools PUBLIC northstarcpp_runtime)
target_compile_features(northstarcpp_editor_tools PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_collision_foundation_smoke
        "${PROJECT_SOURCE_DIR}/tests/collision_foundation_smoke.cpp")
    target_include_directories(northstarcpp_collision_foundation_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_collision_foundation_smoke PRIVATE
        northstarcpp_editor_tools)
    add_dependencies(northstarcpp_collision_foundation_smoke
        northstarcpp_collision)
    add_test(NAME northstarcpp_foundation_collision
        COMMAND "$<TARGET_FILE:northstarcpp_collision_foundation_smoke>")
    set_tests_properties(northstarcpp_foundation_collision PROPERTIES TIMEOUT 30)
endif()



# Platform-neutral virtual asset index shared by desktop editor frontends.
add_library(northstarcpp_asset_catalog STATIC
    modules/editor/asset_catalog.cpp)
target_include_directories(northstarcpp_asset_catalog PUBLIC
    "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
target_compile_features(northstarcpp_asset_catalog PUBLIC cxx_std_17)
target_link_libraries(northstarcpp_asset_catalog PUBLIC northstarcpp_runtime)

# Qt 6 Content Browser UI; reusable in any editor dock or document tab.
# No Win32 common controls, native file dialogs, or Shell icons are used.
add_library(northstarcpp_content_browser_qt STATIC
    modules/editor/qt_asset_card_delegate.hpp
    modules/editor/qt_asset_card_delegate.cpp
    modules/editor/qt_content_browser.hpp
    modules/editor/qt_content_browser.cpp
    modules/editor/qt_asset_preview_dialog.hpp
    modules/editor/qt_asset_preview_dialog.cpp
    modules/editor/qt_material_authoring.cpp
    modules/editor/qt_material_instances.cpp
    modules/editor/qt_material_function_editor.cpp
    modules/editor/qt_asset_warnings.hpp
    modules/editor/qt_asset_warnings.cpp
    modules/editor/editor_asset_material_source.hpp
    modules/editor/editor_asset_material_source.cpp)
set_target_properties(northstarcpp_content_browser_qt PROPERTIES AUTOMOC ON)
if(BUILD_TESTING)
    add_executable(northstarcpp_qt_asset_warnings_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_asset_warnings_smoke.cpp"
        modules/editor/qt_asset_warnings.cpp)
    target_include_directories(northstarcpp_qt_asset_warnings_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_asset_warnings_smoke PRIVATE Qt6::Widgets)
    target_compile_features(northstarcpp_qt_asset_warnings_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_qt_asset_warnings
        COMMAND "$<TARGET_FILE:northstarcpp_qt_asset_warnings_smoke>")
    set_tests_properties(northstarcpp_qt_asset_warnings PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 35)
endif()

# Embed all four project folder icons into both standalone Asset Browser and
# the integrated Editor dock. Installed builds do not need loose artwork.
qt_add_resources(northstarcpp_content_browser_qt northstar_folder_icons
    PREFIX "/northstar/icons"
    BASE "${NORTHSTARCPP_ENGINE_ICONS_DIR}"
    FILES
        "${NORTHSTARCPP_ENGINE_ICONS_DIR}/folder.png"
        "${NORTHSTARCPP_ENGINE_ICONS_DIR}/texturesFolder.png"
        "${NORTHSTARCPP_ENGINE_ICONS_DIR}/modelsFolder.png"
        "${NORTHSTARCPP_ENGINE_ICONS_DIR}/materialsFolder.png")

target_include_directories(northstarcpp_content_browser_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_content_browser_qt PUBLIC
    Qt6::Widgets Qt6::Concurrent northstarcpp_asset_catalog
    PRIVATE northstarcpp_editor_tools)
target_compile_features(northstarcpp_content_browser_qt PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_qt_asset_preview_warnings_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_asset_preview_warnings_smoke.cpp")
    target_include_directories(northstarcpp_qt_asset_preview_warnings_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_asset_preview_warnings_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_editor_tools
        northstarcpp_runtime Qt6::Widgets)
    add_test(NAME northstarcpp_qt_asset_preview_warnings
        COMMAND "$<TARGET_FILE:northstarcpp_qt_asset_preview_warnings_smoke>")
    if(WIN32)
        set_tests_properties(northstarcpp_qt_asset_preview_warnings PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 45)
    else()
        set_tests_properties(northstarcpp_qt_asset_preview_warnings PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
    endif()
endif()


add_executable(northstarcpp_asset_browser WIN32
    modules/editor/qt_asset_browser_main.cpp)
target_link_libraries(northstarcpp_asset_browser PRIVATE
    northstarcpp_content_browser_qt northstarcpp_editor_tools
    northstarcpp_runtime Qt6::Widgets)
set_target_properties(northstarcpp_asset_browser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_link_options(northstarcpp_asset_browser PRIVATE -static)
add_dependencies(northstarcpp_asset_browser northstarcpp_project_manager
    northstarcpp_assets_directory)
add_custom_command(TARGET northstarcpp_asset_browser POST_BUILD
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/deploy_qt.py"
        --developer-executable "$<TARGET_FILE:northstarcpp_asset_browser>"
        --gpp "${CMAKE_CXX_COMPILER}"
    COMMENT "Bundling Qt6 Concurrent and Content Browser runtime"
    VERBATIM)

install(TARGETS northstarcpp_asset_browser
    RUNTIME DESTINATION bin COMPONENT Runtime)

if(BUILD_TESTING)
    # A QA launcher uses its own executable name, so Windows can retain a
    # running release/editor Asset Browser while this target is rebuilt.
    option(NORTHSTARCPP_QA_ASSET_BROWSER
        "Build a separately named, non-installed Qt asset browser test host" OFF)
    set(northstarcpp_browser_test_target northstarcpp_asset_browser)
    if(NORTHSTARCPP_QA_ASSET_BROWSER)
        add_executable(northstarcpp_asset_browser_cards_qa WIN32
            modules/editor/qt_asset_browser_main.cpp)
        target_link_libraries(northstarcpp_asset_browser_cards_qa PRIVATE
            northstarcpp_content_browser_qt northstarcpp_editor_tools
            northstarcpp_runtime Qt6::Widgets)
        set_target_properties(northstarcpp_asset_browser_cards_qa PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        target_link_options(northstarcpp_asset_browser_cards_qa PRIVATE -static)
        set(northstarcpp_browser_test_target northstarcpp_asset_browser_cards_qa)
    endif()
    add_executable(northstarcpp_asset_catalog_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_catalog_smoke.cpp")
    target_link_libraries(northstarcpp_asset_catalog_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_asset_catalog
        COMMAND "$<TARGET_FILE:northstarcpp_asset_catalog_smoke>"
            "${PROJECT_BINARY_DIR}/asset_catalog_smoke"
            "${PROJECT_SOURCE_DIR}/sharedAssets")
    add_executable(northstarcpp_prefab_browser_smoke
        "${PROJECT_SOURCE_DIR}/tests/prefab_browser_smoke.cpp")
    target_include_directories(northstarcpp_prefab_browser_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_prefab_browser_smoke PRIVATE
        northstarcpp_asset_catalog northstarcpp_editor_tools Qt6::Gui)
    add_test(NAME northstarcpp_prefab_browser
        COMMAND "$<TARGET_FILE:northstarcpp_prefab_browser_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform")
    set_tests_properties(northstarcpp_prefab_browser
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_test(NAME northstarcpp_prefab_composition_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --prefab-composition-smoke
            --content "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    set_tests_properties(northstarcpp_prefab_composition_qt
        PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_prefab_cards_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --prefab-cards-smoke
            --content "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    set_tests_properties(northstarcpp_prefab_cards_qt
        PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_content_browser_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>" --smoke)
    add_test(NAME northstarcpp_asset_cards_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --asset-cards-smoke
            --content "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_asset_cards_qt
        PROPERTIES TIMEOUT 65 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_folder_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --folder-icons-smoke
            --content "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_folder_icons_qt
        PROPERTIES TIMEOUT 50 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_nef8_content_browser_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --smoke --content "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_nef8_content_browser_qt
        PROPERTIES TIMEOUT 75 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_nef8_modal_preview_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --smoke --preview-modal-smoke
            --content "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_nef8_modal_preview_qt
        PROPERTIES TIMEOUT 110 RUN_SERIAL TRUE)
    # Windows developer deployment bundles qwindows.dll, not qoffscreen.dll.
    if(WIN32)
        set_tests_properties(northstarcpp_content_browser_qt PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 30)
    else()
        set_tests_properties(northstarcpp_content_browser_qt PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    endif()
endif()

# Dockable cross-platform Qt editor shell. The CPU scene proxy viewport is
# a deliberate fallback until the active GPU backend exposes a Qt render target.
add_executable(northstarcpp_editor_qt WIN32
    modules/editor/qt_editor_main.cpp
    modules/editor/qt_editor_window.hpp
    modules/editor/qt_editor_window.cpp
    modules/editor/qt_material_authoring.hpp
    modules/editor/qt_material_instances.hpp
    modules/editor/qt_material_instance_dialog.cpp
    modules/editor/qt_material_graph_editor.hpp
    modules/editor/qt_material_graph_editor.cpp
    modules/editor/qt_material_graph_clipboard.cpp
    modules/editor/qt_material_graph_groups.cpp
    modules/editor/qt_material_function_smoke.cpp
    modules/editor/qt_material_gpu_preview.hpp
    modules/editor/qt_material_gpu_preview.cpp
    modules/editor/qt_editor_viewport.hpp
    modules/editor/qt_editor_viewport.cpp)
set_target_properties(northstarcpp_editor_qt PROPERTIES
    AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(northstarcpp_editor_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_editor_qt PRIVATE
    Qt6::Widgets northstarcpp_content_browser_qt northstarcpp_editor_tools
    northstarcpp_asset_catalog northstarcpp_runtime)
# Reuse the *existing* engine rendering facade (engine.render ABI 5)
# instead of implementing an editor-only graphics backend. Its current
# surface is HWND/D3D11; non-Windows editor builds keep the Qt proxy.
if(WIN32)
    target_link_libraries(northstarcpp_editor_qt PRIVATE northstarcpp_viewport)
    add_dependencies(northstarcpp_editor_qt
        northstarcpp_render_d3d11 northstarcpp_assets_directory)
endif()
target_link_options(northstarcpp_editor_qt PRIVATE -static)
# Qt editor links the Content Browser library and does not need a running
# standalone Asset Browser executable to be rebuilt or replaced.
install(TARGETS northstarcpp_editor_qt RUNTIME DESTINATION bin COMPONENT Runtime)
if(BUILD_TESTING)
    add_test(NAME northstarcpp_qt_material_instance_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --material-instance-smoke)
    set_tests_properties(northstarcpp_qt_material_instance_authoring PROPERTIES
        TIMEOUT 45)
    add_test(NAME northstarcpp_qt_material_function_gpu
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --material-function-smoke)
    set_tests_properties(northstarcpp_qt_material_function_gpu PROPERTIES TIMEOUT 75)
    add_test(NAME northstarcpp_qt_material_graph_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --material-graph-smoke)
    set_tests_properties(northstarcpp_qt_material_graph_authoring PROPERTIES
        TIMEOUT 45)
    add_test(NAME northstarcpp_qt_editor_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --smoke)
    if(WIN32)
        add_test(NAME northstarcpp_qt_editor_gpu_surface
            COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --gpu-smoke)
        set_tests_properties(northstarcpp_qt_editor_gpu_surface PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 60)
    endif()
    if(WIN32)
        set_tests_properties(northstarcpp_qt_editor_authoring PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 40)
    else()
        set_tests_properties(northstarcpp_qt_editor_authoring PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
    endif()
endif()

# The Qt editor is the only editor executable; window.cpp is still compiled
# exclusively by Player until its shared entry point is split out.
add_executable(northstarcpp_player WIN32
    modules/editor/window.cpp modules/editor/content_browser.cpp)
target_link_libraries(northstarcpp_player PRIVATE comctl32)
foreach(app IN ITEMS northstarcpp_player)
    target_compile_definitions(${app} PRIVATE UNICODE _UNICODE)
    target_include_directories(${app} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport"
        "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
    target_include_directories(${app} SYSTEM PRIVATE "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
    target_link_libraries(${app} PRIVATE northstarcpp_editor_tools northstarcpp_viewport northstarcpp_crash comdlg32 shell32)
    add_dependencies(${app} bugtrap)
    add_dependencies(${app} ${northstarcpp_default_modules})
    target_link_options(${app} PRIVATE -static)
    set_target_properties(${app} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
endforeach()

if(BUILD_TESTING AND Python3_EXECUTABLE)
    add_test(NAME northstarcpp_player_provider_inspection
        COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/player_provider_inspection_smoke.py"
        "$<TARGET_FILE:northstarcpp_player>"
        "$<TARGET_FILE:northstarcpp_geometry_external>"
        "$<TARGET_FILE:northstarcpp_geometry_invalid>")
    set_tests_properties(northstarcpp_player_provider_inspection PROPERTIES TIMEOUT 50)
endif()

# The Project Browser opens the Qt Editor, which launches Player with its providers.
add_dependencies(northstarcpp_editor_qt northstarcpp_player)
add_dependencies(northstarcpp_project_manager northstarcpp_editor_qt)

# The editor uses northstar-editor.ico; Project Manager, player, and camera timeline
# editor use logoStar.ico; BugTrap uses Bug.ico. Each EXE embeds its own icon.
set(NORTHSTARCPP_LOGO_FILE "${NORTHSTARCPP_ENGINE_ICONS_DIR}/logoStar.ico")
set(NORTHSTARCPP_EDITOR_ICON_FILE "${NORTHSTARCPP_ENGINE_ICONS_DIR}/northstar-editor.ico")
set(NORTHSTARCPP_BUG_ICON_FILE "${NORTHSTARCPP_ENGINE_ICONS_DIR}/Bug.ico")
if(NOT EXISTS "${NORTHSTARCPP_LOGO_FILE}")
    message(FATAL_ERROR "Required NorthStarCpp logo missing: ${NORTHSTARCPP_LOGO_FILE}")
endif()
if(NOT EXISTS "${NORTHSTARCPP_EDITOR_ICON_FILE}")
    message(FATAL_ERROR "Required NorthStar Editor icon missing: ${NORTHSTARCPP_EDITOR_ICON_FILE}")
endif()
if(NOT EXISTS "${NORTHSTARCPP_BUG_ICON_FILE}")
    message(FATAL_ERROR "Required BugTrap icon missing: ${NORTHSTARCPP_BUG_ICON_FILE}")
endif()
file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/logo.rc.in"
    "${PROJECT_BINARY_DIR}/generated/northstarcpp_logo.rc" @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/editor_icon.rc.in"
    "${PROJECT_BINARY_DIR}/generated/northstarcpp_editor_icon.rc" @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/bugtrap_icon.rc.in"
    "${PROJECT_BINARY_DIR}/generated/bugtrap_icon.rc" @ONLY)
foreach(brand_target IN ITEMS northstarcpp_project_manager
                              northstarcpp_player northstarcpp_camera_timeline_editor)
    target_sources(${brand_target} PRIVATE
        "${PROJECT_BINARY_DIR}/generated/northstarcpp_logo.rc")
endforeach()
target_sources(northstarcpp_editor_qt PRIVATE
    "${PROJECT_BINARY_DIR}/generated/northstarcpp_editor_icon.rc")
target_sources(northstarcpp_asset_browser PRIVATE
    "${PROJECT_BINARY_DIR}/generated/northstarcpp_editor_icon.rc")
target_include_directories(northstarcpp_asset_browser PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/resources")
target_sources(bugtrap PRIVATE
    "${PROJECT_BINARY_DIR}/generated/bugtrap_icon.rc")
foreach(brand_target IN ITEMS northstarcpp_project_manager northstarcpp_editor_qt
                              northstarcpp_player northstarcpp_camera_timeline_editor bugtrap)
    target_include_directories(${brand_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources")
endforeach()

# BugTrap's Windows File Properties are versioned from project(VERSION ...).
# This resource supplies VS_VERSION_INFO independently of the shared icon.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/bugtrap_version.rc.in"
    "${PROJECT_BINARY_DIR}/generated/bugtrap_version.rc" @ONLY)
target_sources(bugtrap PRIVATE
    "${PROJECT_BINARY_DIR}/generated/bugtrap_version.rc")

install(TARGETS northstarcpp_player northstarcpp_project_manager
                northstarcpp_camera_timeline_editor bugtrap
    RUNTIME DESTINATION bin COMPONENT Runtime)
install(TARGETS northstarcpp_runtime northstarcpp_viewport northstarcpp_projects
    EXPORT NorthStarCppTargets ARCHIVE DESTINATION sdk/lib COMPONENT SDK)

if(BUILD_TESTING)
    add_executable(northstarcpp_render_memory_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_memory_smoke.cpp")
    target_include_directories(northstarcpp_render_memory_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_render_memory_smoke PRIVATE -static)
    add_test(NAME northstarcpp_d3d11_memory_residency
        COMMAND "$<TARGET_FILE:northstarcpp_render_memory_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d11>")
    set_tests_properties(northstarcpp_d3d11_memory_residency PROPERTIES TIMEOUT 60)

    add_executable(northstarcpp_physics_residency_smoke
        "${PROJECT_SOURCE_DIR}/tests/physics_residency_smoke.cpp")
    target_include_directories(northstarcpp_physics_residency_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_physics_residency_smoke PRIVATE -static)
    add_test(NAME northstarcpp_bullet_residency
        COMMAND "$<TARGET_FILE:northstarcpp_physics_residency_smoke>"
                "$<TARGET_FILE:northstarcpp_physics_bullet>")
    add_test(NAME northstarcpp_jolt_residency
        COMMAND "$<TARGET_FILE:northstarcpp_physics_residency_smoke>"
                "$<TARGET_FILE:northstarcpp_physics_jolt>")
    set_tests_properties(northstarcpp_bullet_residency northstarcpp_jolt_residency
        PROPERTIES TIMEOUT 45)

    add_executable(northstarcpp_prefab_composition_smoke
        "${PROJECT_SOURCE_DIR}/tests/prefab_composition_smoke.cpp")
    target_link_libraries(northstarcpp_prefab_composition_smoke PRIVATE
        northstarcpp_editor_tools)
    add_dependencies(northstarcpp_prefab_composition_smoke
        northstarcpp_assets_directory)
    set_target_properties(northstarcpp_prefab_composition_smoke
        PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_prefab_composition
        COMMAND "$<TARGET_FILE:northstarcpp_prefab_composition_smoke>"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(northstarcpp_prefab_composition
        PROPERTIES TIMEOUT 50 WORKING_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_executable(northstarcpp_editor_prefab_smoke
        "${PROJECT_SOURCE_DIR}/tests/editor_prefab_smoke.cpp")
    target_link_libraries(northstarcpp_editor_prefab_smoke PRIVATE northstarcpp_editor_tools)
    target_link_options(northstarcpp_editor_prefab_smoke PRIVATE -static)
    add_test(NAME northstarcpp_editor_prefab
        COMMAND "$<TARGET_FILE:northstarcpp_editor_prefab_smoke>"
                "${PROJECT_BINARY_DIR}/editor-prefab-smoke.nprefab")
    add_executable(northstarcpp_editor_tools_smoke "${PROJECT_SOURCE_DIR}/tests/editor_tools_smoke.cpp")
    target_link_libraries(northstarcpp_editor_tools_smoke PRIVATE northstarcpp_editor_tools)
    add_dependencies(northstarcpp_editor_tools_smoke northstarcpp_ecs_flecs)
    set_target_properties(northstarcpp_editor_tools_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_editor_authoring
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_ECS_PLUGIN=$<TARGET_FILE:northstarcpp_ecs_flecs>"
            "$<TARGET_FILE:northstarcpp_editor_tools_smoke>")
    add_executable(northstarcpp_editor_camera_smoke "${PROJECT_SOURCE_DIR}/tests/editor_camera_smoke.cpp")
    target_link_libraries(northstarcpp_editor_camera_smoke PRIVATE northstarcpp_editor_tools)
    set_target_properties(northstarcpp_editor_camera_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_editor_camera_picking COMMAND northstarcpp_editor_camera_smoke)
    add_executable(northstarcpp_editor_asset_thumbnails_smoke
        "${PROJECT_SOURCE_DIR}/tests/editor_asset_thumbnails_smoke.cpp"
        modules/editor/editor_asset_material_source.cpp
        modules/editor/asset_catalog.cpp)
    target_link_libraries(northstarcpp_editor_asset_thumbnails_smoke PRIVATE northstarcpp_editor_tools)
    set_target_properties(northstarcpp_editor_asset_thumbnails_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_editor_asset_thumbnails COMMAND northstarcpp_editor_asset_thumbnails_smoke)
    add_dependencies(northstarcpp_editor_asset_thumbnails_smoke northstarcpp_assets_directory)
    add_test(NAME northstarcpp_editor_asset_pipeline
        COMMAND "${CMAKE_COMMAND}" -E env
          "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
          "$<TARGET_FILE:northstarcpp_editor_asset_thumbnails_smoke>"
          "${PROJECT_SOURCE_DIR}/sharedAssets"
          "${PROJECT_BINARY_DIR}/editor-visual-regression")
    # Legacy HWND screenshot test retired. Qt editor authoring tests replace it.
    # Verify logoStar.ico is used by engine apps and Bug.ico only by BugTrap.
    add_executable(northstarcpp_windows_logo_smoke
        "${PROJECT_SOURCE_DIR}/tests/windows_logo_smoke.cpp")
    target_include_directories(northstarcpp_windows_logo_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(northstarcpp_windows_logo_smoke PRIVATE shell32)
    target_compile_definitions(northstarcpp_windows_logo_smoke PRIVATE UNICODE _UNICODE)
    target_link_options(northstarcpp_windows_logo_smoke PRIVATE -static)
    add_test(NAME northstarcpp_logo_resources
        COMMAND "$<TARGET_FILE:northstarcpp_windows_logo_smoke>"
            "$<TARGET_FILE:northstarcpp_project_manager>"
            "$<TARGET_FILE:northstarcpp_editor_qt>"
            "$<TARGET_FILE:northstarcpp_player>"
            "$<TARGET_FILE:northstarcpp_camera_timeline_editor>"
            "$<TARGET_FILE:bugtrap>")
    set_tests_properties(northstarcpp_logo_resources PROPERTIES TIMEOUT 25)
    add_executable(northstarcpp_scene_world_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_world_smoke.cpp")
    target_link_libraries(northstarcpp_scene_world_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_world_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_world_v3
        COMMAND northstarcpp_scene_world_smoke
            "${PROJECT_BINARY_DIR}/scene_world_v3_smoke")
    add_test(NAME northstarcpp_project_manager_window
        COMMAND "$<TARGET_FILE:northstarcpp_project_manager>" --smoke)
    set_tests_properties(northstarcpp_project_manager_window PROPERTIES TIMEOUT 30)
    add_executable(northstarcpp_project_catalog_smoke
        "${PROJECT_SOURCE_DIR}/tests/project_catalog_smoke.cpp")
    target_link_libraries(northstarcpp_project_catalog_smoke PRIVATE
        northstarcpp_projects)
    add_test(NAME northstarcpp_project_manager_catalog
        COMMAND northstarcpp_project_catalog_smoke
            "${PROJECT_BINARY_DIR}/project_manager_smoke")
    if(EXISTS "${PROJECT_SOURCE_DIR}/sharedAssets/models/skydome.ydd")
        add_executable(northstarcpp_shared_assets_smoke "${PROJECT_SOURCE_DIR}/tests/shared_assets_smoke.cpp")
        target_link_libraries(northstarcpp_shared_assets_smoke PRIVATE northstarcpp_runtime)
        target_link_options(northstarcpp_shared_assets_smoke PRIVATE -static)
        set_target_properties(northstarcpp_shared_assets_smoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_dependencies(northstarcpp_shared_assets_smoke northstarcpp_assets_directory)
        add_test(NAME northstarcpp_shared_assets COMMAND northstarcpp_shared_assets_smoke
            "${PROJECT_SOURCE_DIR}/sharedAssets")
        add_test(NAME northstarcpp_player_asset_zones
            COMMAND northstarcpp_player --warp --check-scene
                --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/static_scene.nscene"
                --shared-assets "${PROJECT_SOURCE_DIR}/sharedAssets"
                --asset-zones "${PROJECT_SOURCE_DIR}/tests/fixtures/asset_zones.json")
        set_tests_properties(northstarcpp_player_asset_zones PROPERTIES TIMEOUT 45)
    endif()
    # Provider-neutral logger integration, filtering, JSONL and rotation.
    add_library(northstarcpp_logger_mock SHARED
        "${PROJECT_SOURCE_DIR}/tests/logger_mock_provider.cpp")
    target_include_directories(northstarcpp_logger_mock PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_definitions(northstarcpp_logger_mock PRIVATE NORTHSTARCPP_MODULE_BUILD)
    northstarcpp_unprefix_module_output(northstarcpp_logger_mock)
    set_target_properties(northstarcpp_logger_mock PROPERTIES
        PREFIX "" RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    add_executable(northstarcpp_logger_bootstrap_smoke
        "${PROJECT_SOURCE_DIR}/tests/logger_bootstrap_smoke.cpp")
    target_link_libraries(northstarcpp_logger_bootstrap_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_logger_bootstrap_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_logger_bootstrap
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/logger_bootstrap_smoke.py"
            --executable "$<TARGET_FILE:northstarcpp_logger_bootstrap_smoke>"
            --directory "${PROJECT_BINARY_DIR}/logger_bootstrap_smoke")
    set_tests_properties(northstarcpp_logger_bootstrap PROPERTIES TIMEOUT 80)
    add_executable(northstarcpp_logger_smoke
        "${PROJECT_SOURCE_DIR}/tests/logger_smoke.cpp")
    target_link_libraries(northstarcpp_logger_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_logger_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_logger_smoke
        northstarcpp_logger_multisink northstarcpp_logger_mock northstarcpp_audio_winmm)
    add_test(NAME northstarcpp_logger_multisink
        COMMAND "$<TARGET_FILE:northstarcpp_logger_smoke>" default
            "${PROJECT_BINARY_DIR}/logger_smoke")
    if(Python3_EXECUTABLE)
        add_test(NAME northstarcpp_logger_console_colors
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/logger_color_smoke.py"
                --executable "$<TARGET_FILE:northstarcpp_logger_smoke>"
                --directory "${PROJECT_BINARY_DIR}/logger_color_smoke")
        set_tests_properties(northstarcpp_logger_console_colors PROPERTIES TIMEOUT 60)
    endif()

    add_test(NAME northstarcpp_logger_provider_swap
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_LOGGER_PLUGIN=$<TARGET_FILE:northstarcpp_logger_mock>"
            "$<TARGET_FILE:northstarcpp_logger_smoke>" fixture
            "${PROJECT_BINARY_DIR}/logger_smoke")
    add_test(NAME northstarcpp_logger_reject_incompatible
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_LOGGER_PLUGIN=$<TARGET_FILE:northstarcpp_audio_winmm>"
            "$<TARGET_FILE:northstarcpp_logger_smoke>" reject
            "${PROJECT_BINARY_DIR}/logger_smoke")
    if(Python3_EXECUTABLE)
        add_test(NAME northstarcpp_logger_application_lifecycle
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/logger_application_smoke.py"
                --build "${PROJECT_BINARY_DIR}"
                --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/static_scene.nscene")
        # The lifecycle probe starts engine.logger applications and mutates
        # their log destinations. Do not compete with other GUI/process smokes.
        set_tests_properties(northstarcpp_logger_application_lifecycle
            PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endif()
    # Pure C++ boot diagnostics, runnable without a game window or GPU.
    add_executable(northstarcpp_startup_report_smoke
        "${PROJECT_SOURCE_DIR}/tests/startup_report_smoke.cpp")
    add_test(NAME northstarcpp_startup_report
        COMMAND "$<TARGET_FILE:northstarcpp_startup_report_smoke>"
            "${PROJECT_BINARY_DIR}/startup-report-qa.json")
    set_tests_properties(northstarcpp_startup_report PROPERTIES TIMEOUT 20)

    # Full native logger and Player startup reporting through both file sinks.
    if(Python3_EXECUTABLE AND WIN32)
        add_test(NAME northstarcpp_startup_report_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/startup_report_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscn"
                "${PROJECT_SOURCE_DIR}/sharedAssets")
        set_tests_properties(northstarcpp_startup_report_runtime
            PROPERTIES TIMEOUT 105)
    endif()

    # CPU frame metrics: optional diagnostics, never part of the provider ABI.
    add_executable(northstarcpp_frame_profiler_smoke
        "${PROJECT_SOURCE_DIR}/tests/frame_profiler_smoke.cpp")
    add_test(NAME northstarcpp_frame_profiler
        COMMAND northstarcpp_frame_profiler_smoke)
    set_tests_properties(northstarcpp_frame_profiler PROPERTIES TIMEOUT 30)

    # Standalone cache regression: no plugin DLL, GPU or runtime linkage needed.
    add_executable(northstarcpp_asset_streaming_internal_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_streaming_internal_smoke.cpp"
        modules/host/asset_streaming.cpp)
    target_include_directories(northstarcpp_asset_streaming_internal_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules/api/include")
    target_link_options(northstarcpp_asset_streaming_internal_smoke PRIVATE -static)
    add_test(NAME northstarcpp_asset_streaming_internal
        COMMAND northstarcpp_asset_streaming_internal_smoke)
    set_tests_properties(northstarcpp_asset_streaming_internal PROPERTIES TIMEOUT 30)

    add_executable(northstarcpp_assets_smoke "${PROJECT_SOURCE_DIR}/tests/assets_smoke.cpp")
    target_link_libraries(northstarcpp_assets_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_assets_smoke PRIVATE -static)
    set_target_properties(northstarcpp_assets_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_assets_smoke northstarcpp_assets_directory)
    add_test(NAME northstarcpp_assets_vfs
        COMMAND northstarcpp_assets_smoke "${PROJECT_BINARY_DIR}/assets_smoke"
                "${PROJECT_SOURCE_DIR}/tests/fixtures/static_scene.nscene")
    set_tests_properties(northstarcpp_assets_vfs PROPERTIES FIXTURES_SETUP vfs_archive)
    add_test(NAME northstarcpp_player_archive
        COMMAND northstarcpp_player --warp --check-scene --scene level/main.nscene
                --archive "${PROJECT_BINARY_DIR}/assets_smoke/game.db")
    add_test(NAME northstarcpp_player_split_archive
        COMMAND northstarcpp_player --warp --check-scene --scene level/main.nscene
                --archive "${PROJECT_BINARY_DIR}/assets_smoke/game.fat"
                --archive-data "${PROJECT_BINARY_DIR}/assets_smoke/payload.db")
    set_tests_properties(northstarcpp_player_archive northstarcpp_player_split_archive
        PROPERTIES FIXTURES_REQUIRED vfs_archive TIMEOUT 45)
    add_library(northstarcpp_input_mock SHARED "${PROJECT_SOURCE_DIR}/tests/input_mock_provider.cpp")
    target_compile_definitions(northstarcpp_input_mock PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_input_mock PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_input_mock PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_input_mock)
    set_target_properties(northstarcpp_input_mock PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    add_executable(northstarcpp_input_smoke "${PROJECT_SOURCE_DIR}/tests/input_smoke.cpp")
    target_link_libraries(northstarcpp_input_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_input_smoke PRIVATE -static)
    set_target_properties(northstarcpp_input_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_input_smoke northstarcpp_input_win32 northstarcpp_input_mock northstarcpp_audio_winmm)
    add_test(NAME northstarcpp_input_win32 COMMAND northstarcpp_input_smoke default)
    add_test(NAME northstarcpp_input_provider_independent
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_INPUT_PLUGIN=$<TARGET_FILE:northstarcpp_input_mock>"
            "$<TARGET_FILE:northstarcpp_input_smoke>" mock)
    add_test(NAME northstarcpp_input_reject_incompatible
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_INPUT_PLUGIN=$<TARGET_FILE:northstarcpp_audio_winmm>"
            "$<TARGET_FILE:northstarcpp_input_smoke>" reject)

    # Reference provider with no Flecs dependency: proves the host uses only ABI v2.
    add_library(northstarcpp_ecs_mock SHARED "${PROJECT_SOURCE_DIR}/tests/ecs_mock_provider.cpp")
    target_include_directories(northstarcpp_ecs_mock PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_definitions(northstarcpp_ecs_mock PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_link_options(northstarcpp_ecs_mock PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_ecs_mock)
    set_target_properties(northstarcpp_ecs_mock PROPERTIES PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")

    add_executable(northstarcpp_ecs_smoke "${PROJECT_SOURCE_DIR}/tests/ecs_smoke.cpp")
    target_link_libraries(northstarcpp_ecs_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_ecs_smoke northstarcpp_ecs_flecs)
    set_target_properties(northstarcpp_ecs_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_flecs_ecs COMMAND northstarcpp_ecs_smoke)
    add_dependencies(northstarcpp_ecs_smoke northstarcpp_ecs_mock)
    add_test(NAME northstarcpp_ecs_provider_independent COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_ECS_PLUGIN=$<TARGET_FILE:northstarcpp_ecs_mock>"
        "$<TARGET_FILE:northstarcpp_ecs_smoke>")
    add_test(NAME northstarcpp_editor_ecs_provider_independent
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_ECS_PLUGIN=$<TARGET_FILE:northstarcpp_ecs_mock>"
            "${CMAKE_COMMAND}"
            "-DEDITOR=$<TARGET_FILE:northstarcpp_editor_qt>"
            "-DPLAYER=$<TARGET_FILE:northstarcpp_player>"
            "-DTEST_ROOT=${PROJECT_BINARY_DIR}/editor_ecs_mock"
            -P "${PROJECT_SOURCE_DIR}/tests/editor_player_smoke.cmake")
    set_tests_properties(northstarcpp_editor_ecs_provider_independent PROPERTIES TIMEOUT 55)

    # Verified NEF8 payload readiness controls runtime ECS/physics state.
    add_executable(northstarcpp_scene_runtime_lifecycle_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_runtime_lifecycle_smoke.cpp")
    target_link_libraries(northstarcpp_scene_runtime_lifecycle_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_runtime_lifecycle_smoke PRIVATE -static)
    add_dependencies(northstarcpp_scene_runtime_lifecycle_smoke
        northstarcpp_ecs_flecs northstarcpp_physics_bullet northstarcpp_physics_jolt)
    set_target_properties(northstarcpp_scene_runtime_lifecycle_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene_runtime_bullet
        COMMAND "$<TARGET_FILE:northstarcpp_scene_runtime_lifecycle_smoke>"
            "${PROJECT_BINARY_DIR}/scene-runtime-bullet.nscn")
    add_test(NAME northstarcpp_scene_runtime_jolt
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
            "$<TARGET_FILE:northstarcpp_scene_runtime_lifecycle_smoke>"
            "${PROJECT_BINARY_DIR}/scene-runtime-jolt.nscn")
    set_tests_properties(northstarcpp_scene_runtime_bullet
        northstarcpp_scene_runtime_jolt PROPERTIES TIMEOUT 45)

    add_executable(northstarcpp_physics_smoke "${PROJECT_SOURCE_DIR}/tests/physics_smoke.cpp")
    target_link_libraries(northstarcpp_physics_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_physics_smoke northstarcpp_ecs_flecs northstarcpp_physics_bullet)
    set_target_properties(northstarcpp_physics_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_bullet_physics COMMAND northstarcpp_physics_smoke)
    add_test(NAME northstarcpp_bullet_selected COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_PHYSICS_PROVIDER=bullet"
        "$<TARGET_FILE:northstarcpp_physics_smoke>")
    add_test(NAME northstarcpp_jolt_selected COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
        "NORTHSTARCPP_EXPECT_PHYSICS=Jolt"
        "$<TARGET_FILE:northstarcpp_physics_smoke>")
    add_dependencies(northstarcpp_physics_smoke northstarcpp_physics_jolt)
    add_test(NAME northstarcpp_jolt_physics COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:northstarcpp_physics_smoke>"
        "-DPHYSICS=$<TARGET_FILE:northstarcpp_physics_jolt>"
        "-DECS=$<TARGET_FILE:northstarcpp_ecs_flecs>"
        "-DTEST_ROOT=${PROJECT_BINARY_DIR}/jolt_physics_smoke"
        -P "${PROJECT_SOURCE_DIR}/tests/jolt_discovery_smoke.cmake")
    set_tests_properties(northstarcpp_jolt_physics PROPERTIES TIMEOUT 120)
    # FPSArena is an optional example project, not a required engine component.
    # Preserve the test names and explicitly mark them SKIPPED (exit 77) only
    # when the entire example is absent. Incomplete present projects still fail.
    function(northstarcpp_optional_fps_test name)
        if(Python3_EXECUTABLE)
            add_test(NAME "${name}"
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/optional_sample_test.py"
                    --sample "${PROJECT_SOURCE_DIR}/tests/fixtures/legacy_fps_project"
                    -- ${ARGN})
            set_tests_properties("${name}" PROPERTIES SKIP_RETURN_CODE 77)
        else()
            add_test(NAME "${name}" COMMAND ${ARGN})
        endif()
    endfunction()
    add_executable(northstarcpp_fps_assets_smoke "${PROJECT_SOURCE_DIR}/tests/fps_assets_smoke.cpp")
    target_link_libraries(northstarcpp_fps_assets_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_fps_assets_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${PROJECT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_fps_assets_smoke northstarcpp_assets_directory)
    northstarcpp_optional_fps_test(northstarcpp_fps_assets
        "$<TARGET_FILE:northstarcpp_fps_assets_smoke>"
        "${PROJECT_SOURCE_DIR}/tests/fixtures/legacy_fps_project/assets")
    # Native provider-neutral camera director (no Rockstar dependencies).
    add_executable(northstarcpp_camera_smoke "${PROJECT_SOURCE_DIR}/tests/camera_smoke.cpp")
    target_link_libraries(northstarcpp_camera_smoke PRIVATE northstarcpp_runtime)
    add_test(NAME northstarcpp_camera_director COMMAND northstarcpp_camera_smoke)
    add_executable(northstarcpp_camera_physics_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_physics_smoke.cpp")
    target_link_libraries(northstarcpp_camera_physics_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_camera_physics_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_camera_physics_smoke
        northstarcpp_ecs_flecs northstarcpp_physics_bullet northstarcpp_physics_jolt)
    foreach(provider IN ITEMS bullet jolt)
        add_test(NAME northstarcpp_camera_physics_${provider}
            COMMAND northstarcpp_camera_physics_smoke)
        set_tests_properties(northstarcpp_camera_physics_${provider}
            PROPERTIES ENVIRONMENT "NORTHSTARCPP_PHYSICS_PROVIDER=${provider}" TIMEOUT 35)
    endforeach()
    # Exercise camera frame ABI 4 with the actual D3D11 software renderer.
    add_executable(northstarcpp_camera_render_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_render_smoke.cpp")
    target_include_directories(northstarcpp_camera_render_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_link_libraries(northstarcpp_camera_render_smoke PRIVATE
        northstarcpp_viewport user32)
    add_dependencies(northstarcpp_camera_render_smoke northstarcpp_render_d3d11)
    set_target_properties(northstarcpp_camera_render_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_camera_d3d11
        COMMAND northstarcpp_camera_render_smoke "${PROJECT_SOURCE_DIR}/sharedAssets")
    set_tests_properties(northstarcpp_camera_d3d11 PROPERTIES TIMEOUT 45)
    if(Python3_EXECUTABLE)
        add_executable(northstarcpp_optional_asset_recovery_smoke
            "${PROJECT_SOURCE_DIR}/tests/optional_asset_recovery_smoke.cpp")
        target_include_directories(northstarcpp_optional_asset_recovery_smoke PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
        target_link_libraries(northstarcpp_optional_asset_recovery_smoke PRIVATE
            northstarcpp_viewport user32)
        add_dependencies(northstarcpp_optional_asset_recovery_smoke
            northstarcpp_render_d3d11 northstarcpp_assets_directory)
        set_target_properties(northstarcpp_optional_asset_recovery_smoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_test(NAME northstarcpp_optional_asset_recovery
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/optional_asset_recovery_smoke.py"
                "$<TARGET_FILE:northstarcpp_optional_asset_recovery_smoke>"
                "${PROJECT_SOURCE_DIR}/sharedAssets")
        set_tests_properties(northstarcpp_optional_asset_recovery PROPERTIES TIMEOUT 60)
    endif()
    if(EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test/assets/textures/world_surfaces_runtime.ytd")
        add_test(NAME northstarcpp_world_surfaces_gpu
            COMMAND northstarcpp_camera_render_smoke
                "${PROJECT_SOURCE_DIR}/sharedAssets"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test")
        set_tests_properties(northstarcpp_world_surfaces_gpu PROPERTIES TIMEOUT 90)
        add_test(NAME northstarcpp_world_surfaces_fps_scene
            COMMAND northstarcpp_camera_render_smoke
                "${PROJECT_SOURCE_DIR}/sharedAssets"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test/main.nscn")
        set_tests_properties(northstarcpp_world_surfaces_fps_scene PROPERTIES
            TIMEOUT 90)
    endif()
    # Minimal first-person physics platform: renderer, sky assets, Bullet/Jolt.
    add_executable(northstarcpp_exploration_smoke
        "${PROJECT_SOURCE_DIR}/tests/exploration_smoke.cpp")
    target_link_libraries(northstarcpp_exploration_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_exploration_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_exploration_smoke
        northstarcpp_ecs_flecs northstarcpp_physics_bullet
        northstarcpp_physics_jolt northstarcpp_assets_directory)
    add_test(NAME northstarcpp_exploration_bullet
        COMMAND northstarcpp_exploration_smoke
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    add_test(NAME northstarcpp_exploration_jolt
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PLUGIN=$<TARGET_FILE:northstarcpp_physics_jolt>"
            "$<TARGET_FILE:northstarcpp_exploration_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    add_test(NAME northstarcpp_exploration_player
        COMMAND "$<TARGET_FILE:northstarcpp_player>" --warp --check-scene
            --explore --scene
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscn")
    set_tests_properties(northstarcpp_exploration_player PROPERTIES TIMEOUT 60)
    add_executable(northstarcpp_fps_arena_smoke "${PROJECT_SOURCE_DIR}/tests/fps_arena_smoke.cpp")
    target_link_libraries(northstarcpp_fps_arena_smoke PRIVATE northstarcpp_runtime)
    add_test(NAME northstarcpp_fps_arena COMMAND northstarcpp_fps_arena_smoke)
    add_executable(northstarcpp_scene_navigation_cells_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_navigation_cells_smoke.cpp")
    target_link_libraries(northstarcpp_scene_navigation_cells_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_navigation_cells_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_navigation_cells
        COMMAND "$<TARGET_FILE:northstarcpp_scene_navigation_cells_smoke>")
    add_executable(northstarcpp_scene_async_vfs_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_async_vfs_smoke.cpp")
    target_link_libraries(northstarcpp_scene_async_vfs_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_async_vfs_smoke PRIVATE -static)
    add_dependencies(northstarcpp_scene_async_vfs_smoke northstarcpp_assets_directory)
    set_target_properties(northstarcpp_scene_async_vfs_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene_async_vfs
        COMMAND "$<TARGET_FILE:northstarcpp_scene_async_vfs_smoke>"
                "${PROJECT_BINARY_DIR}/scene-async-vfs")
    add_executable(northstarcpp_scene_chunk_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_chunk_smoke.cpp")
    target_link_libraries(northstarcpp_scene_chunk_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_chunk_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_chunk
        COMMAND "$<TARGET_FILE:northstarcpp_scene_chunk_smoke>"
                "${PROJECT_BINARY_DIR}/scene-chunk-smoke.nscn")
    add_executable(northstarcpp_scene_binary_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_binary_smoke.cpp")
    target_link_libraries(northstarcpp_scene_binary_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_binary_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_binary
        COMMAND "$<TARGET_FILE:northstarcpp_scene_binary_smoke>"
                "${PROJECT_BINARY_DIR}/binary-scene-smoke.nscn"
                "${PROJECT_SOURCE_DIR}/tests/fixtures/legacy_nscn_v1.nscn")

    add_dependencies(northstarcpp_scene_binary_smoke northstarcpp_scene_convert)
    add_executable(northstarcpp_scene_chunked_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_chunked_smoke.cpp")
    target_link_libraries(northstarcpp_scene_chunked_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_scene_chunked_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_chunked
        COMMAND "$<TARGET_FILE:northstarcpp_scene_chunked_smoke>"
            "${PROJECT_BINARY_DIR}/scene-chunked-smoke.nscn")
    add_executable(northstarcpp_scene_chunked_stress
        "${PROJECT_SOURCE_DIR}/tests/scene_chunked_stress.cpp")
    target_link_libraries(northstarcpp_scene_chunked_stress PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_chunked_stress PRIVATE -static)
    add_test(NAME northstarcpp_scene_chunked_stress
        COMMAND "$<TARGET_FILE:northstarcpp_scene_chunked_stress>"
            "${PROJECT_BINARY_DIR}/chunked-scene-stress.nscn")
    set_tests_properties(northstarcpp_scene_chunked_stress PROPERTIES TIMEOUT 60)
    add_test(NAME northstarcpp_scene_nef8_crosslang
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/scene_nef8_crosslang.py"
            --converter "$<TARGET_FILE:northstarcpp_scene_convert>"
            --source "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene")
    set_tests_properties(northstarcpp_scene_nef8_crosslang PROPERTIES TIMEOUT 60)
    add_executable(northstarcpp_scene_smoke "${PROJECT_SOURCE_DIR}/tests/scene_smoke.cpp")
    target_link_libraries(northstarcpp_scene_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_scene_smoke northstarcpp_ecs_flecs)
    # The module resolver searches the executable's directory by default.
    # Keep scene tests beside the flat ECS provider, exactly like shipped apps.
    set_target_properties(northstarcpp_scene_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene COMMAND northstarcpp_scene_smoke "${PROJECT_BINARY_DIR}/scene_smoke")
    if(TARGET northstarcpp_scripting_v8_sidecar AND Python3_EXECUTABLE)
        add_test(NAME northstarcpp_player_bundled_scripting
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_bundled_scripting_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_bundled_scripting
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_scene_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_scene_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_scene_runtime
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_prefab_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_prefab_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_prefab_runtime
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_pistol_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_pistol_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_pistol_runtime
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    endif()
    # Qt Editor hands an actual saved NSCN to the standalone Game Player.
    if(Python3_EXECUTABLE)
        add_test(NAME northstarcpp_player_asset_mount_regression
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_asset_mount_smoke.py"
                --editor "$<TARGET_FILE:northstarcpp_editor_qt>"
                --player "$<TARGET_FILE:northstarcpp_player>"
                --shared "${PROJECT_SOURCE_DIR}/sharedAssets")
        set_tests_properties(northstarcpp_player_asset_mount_regression
            PROPERTIES TIMEOUT 150 RUN_SERIAL TRUE)
    endif()
    add_test(NAME northstarcpp_qt_editor_player_launch
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PLAYER_EXECUTABLE=$<TARGET_FILE:northstarcpp_player>"
            "$<TARGET_FILE:northstarcpp_editor_qt>" --player-check)
    set_tests_properties(northstarcpp_qt_editor_player_launch
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_editor_player COMMAND "${CMAKE_COMMAND}"
        "-DEDITOR=$<TARGET_FILE:northstarcpp_editor_qt>"
        "-DPLAYER=$<TARGET_FILE:northstarcpp_player>"
        "-DTEST_ROOT=${PROJECT_BINARY_DIR}/editor_player_smoke"
        -P "${PROJECT_SOURCE_DIR}/tests/editor_player_smoke.cmake")
    set_tests_properties(northstarcpp_editor_player PROPERTIES TIMEOUT 55)
    add_test(NAME northstarcpp_player_scene_validation
        COMMAND northstarcpp_player --warp --check-scene
                --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/static_scene.nscene")
    set_tests_properties(northstarcpp_player_scene_validation PROPERTIES TIMEOUT 45)
    northstarcpp_optional_fps_test(northstarcpp_fps_player
        "$<TARGET_FILE:northstarcpp_player>" --warp --check-scene --fps
        --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/legacy_fps_project/main.nscn")
    northstarcpp_optional_fps_test(northstarcpp_fps_ui
        "$<TARGET_FILE:northstarcpp_player>" --warp --fps-ui-smoke --fps
        --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/legacy_fps_project/main.nscn")
    set_tests_properties(northstarcpp_fps_ui PROPERTIES TIMEOUT 45)
    set_tests_properties(northstarcpp_fps_player PROPERTIES TIMEOUT 60)
    add_test(NAME northstarcpp_player_reject_invalid_scene
        COMMAND northstarcpp_player --warp --check-scene
                --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/invalid_scene.nscene")
    set_tests_properties(northstarcpp_player_reject_invalid_scene
        PROPERTIES TIMEOUT 30 WILL_FAIL TRUE)

    add_library(northstarcpp_capability_fixture SHARED "${PROJECT_SOURCE_DIR}/tests/capability_fixture.cpp")
    target_compile_definitions(northstarcpp_capability_fixture PRIVATE NORTHSTARCPP_MODULE_BUILD)
    target_include_directories(northstarcpp_capability_fixture PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_capability_fixture PRIVATE -static)
    northstarcpp_unprefix_module_output(northstarcpp_capability_fixture)
    set_target_properties(northstarcpp_capability_fixture PROPERTIES PREFIX "")
    set_target_properties(northstarcpp_capability_fixture PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
    add_executable(northstarcpp_capability_smoke "${PROJECT_SOURCE_DIR}/tests/capability_smoke.cpp")
    target_link_libraries(northstarcpp_capability_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_capability_smoke PRIVATE -static)
    set_target_properties(northstarcpp_capability_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_capability_smoke northstarcpp_capability_fixture
        northstarcpp_ecs_smoke northstarcpp_editor_qt northstarcpp_player)
    add_test(NAME northstarcpp_capability_swap COMMAND "${CMAKE_COMMAND}"
        "-DTEST_ROOT=${PROJECT_BINARY_DIR}/capability_smoke"
        "-DPROBE=$<TARGET_FILE:northstarcpp_capability_smoke>"
        "-DFIXTURE=$<TARGET_FILE:northstarcpp_capability_fixture>"
        "-DECS_FIXTURE=$<TARGET_FILE:northstarcpp_ecs_mock>"
        "-DRENDER=$<TARGET_FILE:northstarcpp_render_d3d11>"
        "-DBULLET=$<TARGET_FILE:northstarcpp_physics_bullet>"
        "-DINPUT=$<TARGET_FILE:northstarcpp_input_win32>"
        "-DASSETS=$<TARGET_FILE:northstarcpp_assets_directory>"
        "-DJOLT=$<TARGET_FILE:northstarcpp_physics_jolt>"
        "-DWINMM=$<TARGET_FILE:northstarcpp_audio_winmm>"
        "-DECS_SMOKE=$<TARGET_FILE:northstarcpp_ecs_smoke>"
        "-DEDITOR=$<TARGET_FILE:northstarcpp_editor_qt>"
        "-DPLAYER=$<TARGET_FILE:northstarcpp_player>"
        "-DAPP_SMOKE=${PROJECT_SOURCE_DIR}/tests/editor_player_smoke.cmake"
        -P "${PROJECT_SOURCE_DIR}/tests/capability_smoke.cmake")
    set_tests_properties(northstarcpp_capability_swap PROPERTIES TIMEOUT 100)
endif()
