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

# Bullet sources are retained; the provider is disabled for normal builds.
option(NORTHSTARCPP_BUILD_BULLET_PROVIDER "Build the optional Bullet physics provider" OFF)
if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
    northstarcpp_add_bullet()
endif()

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
    if(capability STREQUAL "physics" OR
        (capability STREQUAL "audio" AND provider STREQUAL "northstar") OR
        (capability STREQUAL "render" AND
        (provider STREQUAL "d3d12" OR provider STREQUAL "vulkan")))
        unset(selection)
    endif()
    northstarcpp_register_plugin(TARGET ${target} CAPABILITY ${capability}
        PROVIDER ${provider} ${selection})
endfunction()

northstarcpp_module(northstarcpp_ecs_flecs ecs flecs modules/ecs/flecs/flecs.cpp)
target_compile_definitions(northstarcpp_ecs_flecs PRIVATE NORTHSTARCPP_ECS_BUILD)
target_link_libraries(northstarcpp_ecs_flecs PRIVATE flecs_static)

if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
    northstarcpp_module(northstarcpp_physics_bullet physics bullet modules/physics/bullet/bullet.cpp)
endif()
if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
    target_link_libraries(northstarcpp_physics_bullet PRIVATE BulletDynamics)
endif()

add_library(northstarcpp_collision_cooker STATIC modules/physics/vhacd_cooker.cpp)
target_compile_features(northstarcpp_collision_cooker PUBLIC cxx_std_17)
target_include_directories(northstarcpp_collision_cooker PUBLIC
    "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
    PRIVATE "${NORTHSTARCPP_VENDOR_ROOT}/sources/VHACD/include")
if(BUILD_TESTING)
    add_executable(northstarcpp_unified_ddc_tasks_smoke
        "${PROJECT_SOURCE_DIR}/tests/unified_ddc_tasks_smoke.cpp")
    target_link_libraries(northstarcpp_unified_ddc_tasks_smoke PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_unified_ddc_tasks_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_unified_ddc_tasks
        COMMAND "$<TARGET_FILE:northstarcpp_unified_ddc_tasks_smoke>")
    add_executable(northstarcpp_collision_vhacd_smoke
        "${PROJECT_SOURCE_DIR}/tests/collision_vhacd_smoke.cpp")
    target_link_libraries(northstarcpp_collision_vhacd_smoke
        PRIVATE northstarcpp_collision_cooker)
    add_test(NAME northstarcpp_collision_vhacd
             COMMAND "$<TARGET_FILE:northstarcpp_collision_vhacd_smoke>")
endif()
if(BUILD_TESTING)
    add_executable(northstarcpp_collision_project_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_collision_profiles_smoke.cpp")
    target_link_libraries(northstarcpp_collision_project_smoke PRIVATE Qt6::Core)
    target_include_directories(northstarcpp_collision_project_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    add_test(NAME northstarcpp_collision_project
        COMMAND "$<TARGET_FILE:northstarcpp_collision_project_smoke>")
endif()
northstarcpp_module(northstarcpp_physics_jolt physics jolt modules/physics/jolt/jolt.cpp)
# engine.vehicle/native is an independently replaceable ABI provider.
# Without registration the project browser rejects persisted native choices.
northstarcpp_module(northstarcpp_vehicle_native vehicle native
    modules/vehicle/native/native.cpp)
target_link_libraries(northstarcpp_physics_jolt PRIVATE northstarcpp_collision_cooker)
target_link_libraries(northstarcpp_physics_jolt PRIVATE Jolt)
set(northstarcpp_physics_provider_targets northstarcpp_physics_jolt)
set(northstarcpp_physics_provider_names jolt)
if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
    list(APPEND northstarcpp_physics_provider_targets northstarcpp_physics_bullet)
    list(APPEND northstarcpp_physics_provider_names bullet)
endif()

# Native, provider-neutral Terrain streaming / CityRoot foundations.
# Renderer/physics adapters consume generated meshes and dirty transforms.
add_library(northstarcpp_world_foundation STATIC
    modules/world/world_foundation.cpp)
target_link_libraries(northstarcpp_world_foundation PUBLIC northstarcpp_api)
target_compile_features(northstarcpp_world_foundation PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_world_foundation_smoke
        "${PROJECT_SOURCE_DIR}/tests/world_foundation_smoke.cpp")
    target_link_libraries(northstarcpp_world_foundation_smoke PRIVATE
        northstarcpp_world_foundation)
    add_test(NAME northstarcpp_world_foundation
        COMMAND "$<TARGET_FILE:northstarcpp_world_foundation_smoke>")
endif()

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
    modules/render/d3d11/renderer_lights.cpp
    modules/render/d3d11/renderer_surfaces.cpp
    modules/render/d3d11/renderer_frame.cpp
    modules/render/d3d11/renderer_pick.cpp
    modules/render/d3d11/renderer_editor_grid.cpp
)
target_link_libraries(northstarcpp_render_d3d11 PRIVATE northstarcpp_ui northstarcpp_math d3d11 dxgi d3dcompiler_47 dxguid)

target_compile_definitions(northstarcpp_render_d3d11 PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)

northstarcpp_module(northstarcpp_render_d3d12 render d3d12 modules/render/d3d12/d3d12.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE modules/render/d3d12/shader_compiler.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE modules/render/d3d12/renderer_geometry.cpp)
target_sources(northstarcpp_render_d3d12 PRIVATE
    modules/render/d3d12/renderer_device.cpp
    modules/render/d3d12/renderer_resources.cpp
    modules/render/d3d12/renderer_upload_pages.cpp
    modules/render/d3d12/renderer_materials.cpp
    modules/render/d3d12/renderer_rigid_instances.cpp
    modules/render/d3d12/renderer_shader_reload.cpp
    modules/render/d3d12/renderer_film_look.cpp
    modules/render/d3d12/renderer_scene.cpp
    modules/render/d3d12/renderer_diagnostics.cpp
    modules/render/d3d12/renderer_frame.cpp
    modules/render/d3d12/renderer_pick.cpp
    modules/render/d3d12/renderer_pick_async.cpp
    modules/render/d3d12/renderer_editor_grid.cpp
    modules/render/d3d12/renderer_resource_states.cpp
    modules/render/d3d12/renderer_transient_alias.cpp
    modules/render/d3d12/renderer_texture_pool.cpp
    modules/render/d3d12/renderer_pass_barriers.cpp
    modules/render/d3d12/renderer_pass_shadow.cpp
    modules/render/d3d12/renderer_pass_gbuffer.cpp
    modules/render/d3d12/renderer_pass_deferred.cpp
    modules/render/d3d12/renderer_motion.cpp
    modules/render/d3d12/renderer_pass_postfx.cpp
    modules/render/d3d12/renderer_pass_present.cpp
    modules/render/d3d12/renderer_ui.cpp
)
target_link_libraries(northstarcpp_render_d3d12 PRIVATE northstarcpp_ui northstarcpp_math d3d12 dxgi d3dcompiler_47 dxguid)

target_compile_definitions(northstarcpp_render_d3d12 PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)

# Experimental Vulkan GPU provider. Never make it the default before
# feature parity and game-scene acceptance. Compatible with render ABI 5.
if(WIN32)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        northstarcpp_module(northstarcpp_render_vulkan render vulkan
            modules/render/vulkan/vulkan.cpp)
        target_sources(northstarcpp_render_vulkan PRIVATE
            modules/render/vulkan/renderer_device.cpp
            modules/render/vulkan/renderer_frame.cpp
    modules/render/vulkan/renderer_frame_diagnostics.cpp
            modules/render/vulkan/renderer_frame_diagnostics.cpp
            modules/render/vulkan/renderer_deferred.cpp
            modules/render/vulkan/renderer_ssao.cpp
            modules/render/vulkan/renderer_csm.cpp
            modules/render/vulkan/renderer_csm_draw.cpp
            modules/render/vulkan/renderer_pipeline_cache.cpp
            modules/render/vulkan/renderer_hdr_resources.cpp
            modules/render/vulkan/renderer_hdr_pass.cpp
            modules/render/vulkan/renderer_velocity.cpp
            modules/render/vulkan/renderer_hdr_exposure.cpp
            modules/render/vulkan/renderer_hdr_forward_graph.cpp
    modules/render/vulkan/renderer_film_look.cpp
    modules/render/vulkan/renderer_pick.cpp
    modules/render/vulkan/renderer_pick_sources.cpp
    modules/render/vulkan/renderer_pick_async.cpp
            modules/render/vulkan/renderer_materials.cpp
            modules/render/vulkan/renderer_shader_reload.cpp
            modules/render/vulkan/renderer_skin.cpp
            modules/render/vulkan/renderer_surfaces.cpp
            modules/render/vulkan/renderer_ui.cpp
            modules/render/vulkan/material_graph_glsl.cpp)
        target_compile_features(northstarcpp_render_vulkan PRIVATE cxx_std_17)
        target_compile_definitions(northstarcpp_render_vulkan PRIVATE VK_USE_PLATFORM_WIN32_KHR)
        target_link_libraries(northstarcpp_render_vulkan PRIVATE Vulkan::Vulkan)
        # Shaderc is loaded lazily, and staged for isolated GPU QA builds.
        get_filename_component(_northstar_vk_sdk_include_parent
            "${Vulkan_INCLUDE_DIRS}" DIRECTORY)
        find_file(_northstar_shaderc_runtime
            NAMES shaderc_shared.dll libshaderc_shared.dll
            HINTS "${_northstar_vk_sdk_include_parent}/Bin"
            NO_DEFAULT_PATH)
        if(EXISTS "${_northstar_shaderc_runtime}")
            add_custom_command(TARGET northstarcpp_render_vulkan POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_northstar_shaderc_runtime}"
                    "$<TARGET_FILE_DIR:northstarcpp_render_vulkan>"
                VERBATIM)
        endif()
    else()
        message(STATUS "Vulkan SDK not found; skipping experimental Vulkan provider")
    endif()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_root_sky_source "${CMAKE_CURRENT_SOURCE_DIR}/resources/sharedAssets/shaders/sky")
set(_root_sky_header "${CMAKE_CURRENT_SOURCE_DIR}/modules/render/shared/engine_sky_generated.hpp")
add_custom_target(northstarcpp_embed_root_sky_shaders
 COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tools/dev/embed_engine_sky_shaders.py"
   --output "${_root_sky_header}"
 DEPENDS "${_root_sky_source}/atmosphere.hlsl"
         "${_root_sky_source}/atmosphere.glsl"
         "${_root_sky_source}/sun_optics.hlsl"
         "${_root_sky_source}/sun_optics.glsl"
         "${PROJECT_SOURCE_DIR}/tools/dev/embed_engine_sky_shaders.py"
 BYPRODUCTS "${_root_sky_header}" VERBATIM)
if(TARGET northstarcpp_render_d3d12)
 add_dependencies(northstarcpp_render_d3d12 northstarcpp_embed_root_sky_shaders)
endif()
if(TARGET northstarcpp_render_vulkan)
 add_dependencies(northstarcpp_render_vulkan northstarcpp_embed_root_sky_shaders)
endif()

if(BUILD_TESTING)
    if(TARGET northstarcpp_render_vulkan)
        find_program(NORTHSTARCPP_GLSLC_EXECUTABLE NAMES glslc glslc.exe)
        if(NORTHSTARCPP_GLSLC_EXECUTABLE)
            set(_shader_hot_spirv "${PROJECT_BINARY_DIR}/generated/shader_hot_green.spv")
            add_custom_command(OUTPUT "${_shader_hot_spirv}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/generated"
                COMMAND "${NORTHSTARCPP_GLSLC_EXECUTABLE}"
                    --target-env=vulkan1.2 -fshader-stage=frag
                    -o "${_shader_hot_spirv}"
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_green.frag"
                DEPENDS "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_green.frag"
                VERBATIM)
            add_custom_target(northstarcpp_shader_hot_fixture
                DEPENDS "${_shader_hot_spirv}")
            add_executable(northstarcpp_render_shader_hot_reload_smoke
                "${PROJECT_SOURCE_DIR}/tests/render_shader_hot_reload_smoke.cpp")
            target_include_directories(northstarcpp_render_shader_hot_reload_smoke PRIVATE
                "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
            target_compile_features(northstarcpp_render_shader_hot_reload_smoke PRIVATE cxx_std_17)
            target_link_options(northstarcpp_render_shader_hot_reload_smoke PRIVATE -static)
            add_dependencies(northstarcpp_render_shader_hot_reload_smoke
                northstarcpp_shader_hot_fixture)
            set(_shader_hot_vertex "${PROJECT_BINARY_DIR}/generated/shader_hot_vertex.spv")
            set(_shader_hot_gbuffer "${PROJECT_BINARY_DIR}/generated/shader_hot_gbuffer.spv")
            add_custom_command(OUTPUT "${_shader_hot_vertex}" "${_shader_hot_gbuffer}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${PROJECT_BINARY_DIR}/generated"
                COMMAND "${NORTHSTARCPP_GLSLC_EXECUTABLE}" --target-env=vulkan1.2
                    -fshader-stage=vert -o "${_shader_hot_vertex}"
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_passthrough.vert"
                COMMAND "${NORTHSTARCPP_GLSLC_EXECUTABLE}" --target-env=vulkan1.2
                    -fshader-stage=frag -o "${_shader_hot_gbuffer}"
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_gbuffer.frag"
                DEPENDS
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_passthrough.vert"
                    "${PROJECT_SOURCE_DIR}/tests/fixtures/shader_hot_reload_gbuffer.frag"
                VERBATIM)
            add_custom_target(northstarcpp_shader_hot_v2_fixture
                DEPENDS "${_shader_hot_vertex}" "${_shader_hot_gbuffer}")
            add_executable(northstarcpp_render_shader_hot_reload_v2_smoke
                "${PROJECT_SOURCE_DIR}/tests/render_shader_hot_reload_v2_smoke.cpp")
            target_include_directories(northstarcpp_render_shader_hot_reload_v2_smoke PRIVATE
                "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
            target_compile_features(northstarcpp_render_shader_hot_reload_v2_smoke PRIVATE cxx_std_17)
            target_link_options(northstarcpp_render_shader_hot_reload_v2_smoke PRIVATE -static)
            add_dependencies(northstarcpp_render_shader_hot_reload_v2_smoke
                northstarcpp_shader_hot_v2_fixture)
            add_test(NAME northstarcpp_vulkan_shader_hot_reload_v2_forward
                COMMAND "$<TARGET_FILE:northstarcpp_render_shader_hot_reload_v2_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>"
                    "${_shader_hot_vertex}" "${_shader_hot_spirv}" forward)
            add_test(NAME northstarcpp_vulkan_shader_hot_reload_v2_deferred
                COMMAND "$<TARGET_FILE:northstarcpp_render_shader_hot_reload_v2_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>"
                    "${_shader_hot_vertex}" "${_shader_hot_gbuffer}" deferred)
            set_tests_properties(
                northstarcpp_vulkan_shader_hot_reload_v2_forward
                northstarcpp_vulkan_shader_hot_reload_v2_deferred
                PROPERTIES TIMEOUT 60)
            add_test(NAME northstarcpp_vulkan_shader_hot_reload_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_render_shader_hot_reload_smoke>"
                        "$<TARGET_FILE:northstarcpp_render_vulkan>"
                        "${_shader_hot_spirv}")
            set_tests_properties(northstarcpp_vulkan_shader_hot_reload_hardware
                PROPERTIES TIMEOUT 90)
        endif()
        add_executable(northstarcpp_render_vulkan_smoke
            "${PROJECT_SOURCE_DIR}/tests/render_vulkan_smoke.cpp")
        target_include_directories(northstarcpp_render_vulkan_smoke PRIVATE
            "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
        target_compile_features(northstarcpp_render_vulkan_smoke PRIVATE cxx_std_17)
        target_link_options(northstarcpp_render_vulkan_smoke PRIVATE -static)
        add_dependencies(northstarcpp_render_vulkan_smoke northstarcpp_render_vulkan)
        add_test(NAME northstarcpp_vulkan_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
        set_tests_properties(northstarcpp_vulkan_hardware PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 75)
        # The deferred AO image adds a descriptor and a Render Graph pass;
        # exercise both GBuffer layouts, including real GPU readback/resize.
        foreach(_ao_gbuffer_version 1 2)
            add_test(NAME northstarcpp_vulkan_ssao_gbuffer_${_ao_gbuffer_version}_hardware
                COMMAND "${CMAKE_COMMAND}" -E env
                    NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
                    NORTHSTARCPP_ENABLE_VULKAN_SSAO=1
                    NORTHSTARCPP_GBUFFER_VERSION=${_ao_gbuffer_version}
                    "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_vulkan_ssao_gbuffer_${_ao_gbuffer_version}_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
        endforeach()
        # CSM descriptor/pipeline permutation matrix: both GBuffer layouts
        # with and without half-resolution SSAO.
        foreach(_csm_gbuffer 1 2)
            foreach(_csm_ssao 0 1)
                add_test(
                    NAME northstarcpp_vulkan_csm_gbuffer_${_csm_gbuffer}_ssao_${_csm_ssao}_hardware
                    COMMAND "${CMAKE_COMMAND}" -E env
                        NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
                        NORTHSTARCPP_ENABLE_VULKAN_CSM=1
                        NORTHSTARCPP_ENABLE_VULKAN_SSAO=${_csm_ssao}
                        NORTHSTARCPP_GBUFFER_VERSION=${_csm_gbuffer}
                        "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                        "$<TARGET_FILE:northstarcpp_render_vulkan>")
                set_tests_properties(
                    northstarcpp_vulkan_csm_gbuffer_${_csm_gbuffer}_ssao_${_csm_ssao}_hardware
                    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
            endforeach()
        endforeach()
        # HDR shader+resource+RenderGraph permutation coverage.
        foreach(_hdr_gbuffer 1 2)
            foreach(_hdr_ssao 0 1)
                foreach(_hdr_csm 0 1)
                    add_test(
                        NAME northstarcpp_vulkan_hdr_gbuffer_${_hdr_gbuffer}_ssao_${_hdr_ssao}_csm_${_hdr_csm}_hardware
                        COMMAND "${CMAKE_COMMAND}" -E env
                            NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
                            NORTHSTARCPP_ENABLE_VULKAN_HDR=1
                            NORTHSTARCPP_ENABLE_VULKAN_SSAO=${_hdr_ssao}
                            NORTHSTARCPP_ENABLE_VULKAN_CSM=${_hdr_csm}
                            NORTHSTARCPP_GBUFFER_VERSION=${_hdr_gbuffer}
                            "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                            "$<TARGET_FILE:northstarcpp_render_vulkan>")
                    set_tests_properties(
                        northstarcpp_vulkan_hdr_gbuffer_${_hdr_gbuffer}_ssao_${_hdr_ssao}_csm_${_hdr_csm}_hardware
                        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
                endforeach()
            endforeach()
        endforeach()
        foreach(_hdr_bloom 0 1 2 3)
            add_test(NAME northstarcpp_vulkan_hdr_auto_bloom_${_hdr_bloom}_hardware
                COMMAND "${CMAKE_COMMAND}" -E env
                    NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
                    NORTHSTARCPP_ENABLE_VULKAN_HDR=1
                    NORTHSTARCPP_VULKAN_AUTO_EXPOSURE=1
                    NORTHSTARCPP_VULKAN_BLOOM_QUALITY=${_hdr_bloom}
                    NORTHSTARCPP_GBUFFER_VERSION=2
                    "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_vulkan_hdr_auto_bloom_${_hdr_bloom}_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90)
        endforeach()
        add_test(NAME northstarcpp_vulkan_stress_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>" --stress)
        set_tests_properties(northstarcpp_vulkan_stress_hardware PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 150)
        add_test(NAME northstarcpp_vulkan_gpu_profile_hardware
            COMMAND "${CMAKE_COMMAND}" -E env
                NORTHSTARCPP_VULKAN_VALIDATION=1
                "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                "$<TARGET_FILE:northstarcpp_render_vulkan>"
                --stress --gpu-profile)
        set_tests_properties(northstarcpp_vulkan_gpu_profile_hardware PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 200)
        add_test(NAME northstarcpp_vulkan_validation_hardware
            COMMAND "${CMAKE_COMMAND}" -E env
                NORTHSTARCPP_VULKAN_VALIDATION=1
                "$<TARGET_FILE:northstarcpp_render_vulkan_smoke>"
                "$<TARGET_FILE:northstarcpp_render_vulkan>")
        set_tests_properties(northstarcpp_vulkan_validation_hardware PROPERTIES
            SKIP_RETURN_CODE 77 TIMEOUT 90)
        # MSYS2 keeps the Vulkan validation layer manifest beside its DLL,
        # rather than registering it as a system-wide Windows SDK layer.
        get_filename_component(_northstar_vulkan_toolchain_bin
            "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if(EXISTS "${_northstar_vulkan_toolchain_bin}/VkLayer_khronos_validation.json")
            set_tests_properties(northstarcpp_vulkan_gpu_profile_hardware
                northstarcpp_vulkan_validation_hardware PROPERTIES
                ENVIRONMENT "VK_LAYER_PATH=${_northstar_vulkan_toolchain_bin}")
        endif()
    endif()
    add_executable(northstarcpp_editor_grid_gpu_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_editor_grid_smoke.cpp")
    target_include_directories(northstarcpp_editor_grid_gpu_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_editor_grid_gpu_smoke PRIVATE -static)
    add_dependencies(northstarcpp_editor_grid_gpu_smoke
        northstarcpp_render_d3d11 northstarcpp_render_d3d12)
    foreach(backend d3d11 d3d12)
        add_test(NAME northstarcpp_editor_grid_gpu_${backend}_warp
            COMMAND "$<TARGET_FILE:northstarcpp_editor_grid_gpu_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_${backend}>")
        set_tests_properties(northstarcpp_editor_grid_gpu_${backend}_warp
            PROPERTIES TIMEOUT 60)
        add_test(NAME northstarcpp_editor_grid_gpu_${backend}_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_editor_grid_gpu_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_${backend}>"
                    --hardware)
        set_tests_properties(northstarcpp_editor_grid_gpu_${backend}_hardware
            PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endforeach()
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
    set_tests_properties(northstarcpp_d3d11_material_warp PROPERTIES
        TIMEOUT 40
        ENVIRONMENT "NORTHSTARCPP_SHADER_CACHE_DIR=${PROJECT_BINARY_DIR}/shader-cache-ctest")
    add_test(NAME northstarcpp_d3d12_material_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d11_material_smoke>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_material_warp PROPERTIES
        TIMEOUT 45
        ENVIRONMENT "NORTHSTARCPP_SHADER_CACHE_DIR=${PROJECT_BINARY_DIR}/shader-cache-ctest")
    add_executable(northstarcpp_shader_disk_cache_smoke
        "${PROJECT_SOURCE_DIR}/tests/shader_disk_cache_smoke.cpp")
    target_include_directories(northstarcpp_shader_disk_cache_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/render/shared")
    target_link_options(northstarcpp_shader_disk_cache_smoke PRIVATE -static)
    add_test(NAME northstarcpp_shader_disk_cache
        COMMAND "$<TARGET_FILE:northstarcpp_shader_disk_cache_smoke>"
            "${PROJECT_SOURCE_DIR}/out/shader-cache-unit-20261115")
    set_tests_properties(northstarcpp_shader_disk_cache PROPERTIES TIMEOUT 15)
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
    add_test(NAME northstarcpp_d3d12_enhanced_barriers_hardware
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_enhanced_barriers_smoke>" --hardware)
    set_tests_properties(northstarcpp_d3d12_enhanced_barriers_hardware
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
    add_test(NAME northstarcpp_d3d12_gpu_timestamp_profile_hardware
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ENABLE_TRANSIENT_ALIASING=1
            NORTHSTARCPP_ENABLE_BLOOM=1
            NORTHSTARCPP_ENABLE_HDR=1
            NORTHSTARCPP_ENABLE_SHADOWS=1
            NORTHSTARCPP_ENABLE_PCSS=1
            "$<TARGET_FILE:northstarcpp_d3d12_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>"
            --hardware --stress --gpu-profile)
    set_tests_properties(northstarcpp_d3d12_gpu_timestamp_profile_hardware
        PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)

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

if(BUILD_TESTING)
    add_executable(northstarcpp_d3d12_upload_pages_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_d3d12_upload_pages_smoke.cpp"
        modules/render/d3d12/renderer_upload_pages.cpp)
    target_compile_definitions(northstarcpp_d3d12_upload_pages_smoke PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)
    target_include_directories(northstarcpp_d3d12_upload_pages_smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_libraries(northstarcpp_d3d12_upload_pages_smoke PRIVATE
        northstarcpp_ui northstarcpp_math d3d12)
    add_test(NAME northstarcpp_d3d12_upload_pages
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_upload_pages_smoke>")
    set_tests_properties(northstarcpp_d3d12_upload_pages PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Backend-independent scheduling contract and smoke coverage.
if(BUILD_TESTING)
    add_executable(northstarcpp_render_graph_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_graph_smoke.cpp")
    target_compile_features(northstarcpp_render_graph_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_graph
        COMMAND "$<TARGET_FILE:northstarcpp_render_graph_smoke>")
    add_executable(northstarcpp_render_reference_pipeline_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_reference_pipeline_smoke.cpp")
    target_compile_features(northstarcpp_render_reference_pipeline_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_reference_pipeline
        COMMAND "$<TARGET_FILE:northstarcpp_render_reference_pipeline_smoke>")
    add_executable(northstarcpp_viewport_skin_batches_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_skin_batches_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_skin_batches_smoke PRIVATE northstarcpp_math)
    add_test(NAME northstarcpp_viewport_skin_batches
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_skin_batches_smoke>")
    # Backend-neutral camera-cut / reprojection validity contract.
    add_executable(northstarcpp_render_temporal_history_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_temporal_history_smoke.cpp")
    target_compile_features(northstarcpp_render_temporal_history_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_temporal_history
        COMMAND "$<TARGET_FILE:northstarcpp_render_temporal_history_smoke>")
    add_executable(northstarcpp_render_temporal_motion_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_temporal_motion_smoke.cpp")
    target_compile_features(northstarcpp_render_temporal_motion_smoke PRIVATE cxx_std_17)
    add_executable(northstarcpp_render_temporal_instance_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_temporal_instance_smoke.cpp")
    target_compile_features(northstarcpp_render_temporal_instance_smoke PRIVATE cxx_std_17)
    target_include_directories(northstarcpp_render_temporal_instance_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/api/include")
    add_executable(northstarcpp_render_taa_reference_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_taa_reference_smoke.cpp")
    target_compile_features(northstarcpp_render_taa_reference_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_taa_reference
        COMMAND "$<TARGET_FILE:northstarcpp_render_taa_reference_smoke>")
    add_test(NAME northstarcpp_render_temporal_instance
        COMMAND "$<TARGET_FILE:northstarcpp_render_temporal_instance_smoke>")
    add_test(NAME northstarcpp_render_temporal_motion
        COMMAND "$<TARGET_FILE:northstarcpp_render_temporal_motion_smoke>")
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
        target_link_libraries(northstarcpp_render_graph_vulkan_executor_smoke PRIVATE northstarcpp_api)
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

# The mandatory NorthStar Audio provider supports overlapping voices ABI 2.
# Retired the single-stream WinMM provider entirely.
northstarcpp_module(northstarcpp_audio_northstar audio northstar modules/audio/northstar/northstar.cpp)
target_sources(northstarcpp_audio_northstar PRIVATE
    modules/audio/northstar/native_mixer.cpp
    modules/audio/northstar/native_device.cpp)
set_target_properties(northstarcpp_audio_northstar PROPERTIES OUTPUT_NAME audio_northstar)
if(WIN32)
    target_link_libraries(northstarcpp_audio_northstar PRIVATE winmm)
endif()

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

target_sources(northstarcpp_assets_directory PRIVATE modules/assets/directory/fat_archive.cpp modules/assets/directory/nsp_archive.cpp)
target_include_directories(northstarcpp_assets_directory SYSTEM PRIVATE "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
# Keep the stable engine.assets/directory provider identity and CMake target.
# The external filename changes; the generated manifest uses TARGET_FILE_NAME.
set_target_properties(northstarcpp_assets_directory PROPERTIES OUTPUT_NAME "AssetManager")


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

target_include_directories(northstarcpp_animation SYSTEM PRIVATE
    "${NORTHSTARCPP_VENDOR_INCLUDE_DIR}")
target_compile_features(northstarcpp_animation PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_grounded_locomotion_smoke
        "${PROJECT_SOURCE_DIR}/tests/grounded_locomotion_smoke.cpp")
    target_link_libraries(northstarcpp_grounded_locomotion_smoke PRIVATE
        northstarcpp_viewport northstarcpp_runtime)
    target_link_options(northstarcpp_grounded_locomotion_smoke PRIVATE -static)
    set_target_properties(northstarcpp_grounded_locomotion_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_grounded_locomotion
        COMMAND "$<TARGET_FILE:northstarcpp_grounded_locomotion_smoke>")
    set_tests_properties(northstarcpp_grounded_locomotion PROPERTIES TIMEOUT 45)
    add_executable(northstarcpp_procedural_ik_smoke
        "${PROJECT_SOURCE_DIR}/tests/procedural_ik_smoke.cpp")
    target_link_libraries(northstarcpp_procedural_ik_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_procedural_ik_smoke PRIVATE -static)
    set_target_properties(northstarcpp_procedural_ik_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_procedural_ik
        COMMAND "$<TARGET_FILE:northstarcpp_procedural_ik_smoke>"
            "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/sharedAssets")
    set_tests_properties(northstarcpp_procedural_ik PROPERTIES TIMEOUT 40)
    add_executable(northstarcpp_animation_graph_smoke
        "${PROJECT_SOURCE_DIR}/tests/animation_graph_smoke.cpp")
    target_link_libraries(northstarcpp_animation_graph_smoke PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_animation_graph_smoke PRIVATE -static)
    set_target_properties(northstarcpp_animation_graph_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_animation_graph
        COMMAND "$<TARGET_FILE:northstarcpp_animation_graph_smoke>"
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/sharedAssets")
    set_tests_properties(northstarcpp_animation_graph PROPERTIES TIMEOUT 90)
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

    add_executable(northstarcpp_material_precache_smoke
        "${PROJECT_SOURCE_DIR}/tests/material_precache_smoke.cpp")
    target_link_libraries(northstarcpp_material_precache_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_material_precache_smoke PRIVATE -static)
    add_dependencies(northstarcpp_material_precache_smoke
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_material_precache
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
            "$<TARGET_FILE:northstarcpp_material_precache_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    set_tests_properties(northstarcpp_material_precache PROPERTIES TIMEOUT 30)

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
        "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_material_resources PROPERTIES TIMEOUT 50)
    add_executable(northstarcpp_character_namespace_rebase_smoke
        "${PROJECT_SOURCE_DIR}/tests/character_namespace_rebase_smoke.cpp")
    target_include_directories(northstarcpp_character_namespace_rebase_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/models")
    target_link_options(northstarcpp_character_namespace_rebase_smoke PRIVATE -static)
    add_test(NAME northstarcpp_character_namespace_rebase
        COMMAND "$<TARGET_FILE:northstarcpp_character_namespace_rebase_smoke>")
    set_tests_properties(northstarcpp_character_namespace_rebase PROPERTIES TIMEOUT 15)

    add_executable(northstarcpp_nightcity_native_skin_acceptance
        "${PROJECT_SOURCE_DIR}/tests/nightcity_native_skin_acceptance.cpp")
    target_link_libraries(northstarcpp_nightcity_native_skin_acceptance PRIVATE
        northstarcpp_runtime)
    target_link_options(northstarcpp_nightcity_native_skin_acceptance PRIVATE -static)
    add_dependencies(northstarcpp_nightcity_native_skin_acceptance
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_nightcity_native_skin
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
        "$<TARGET_FILE:northstarcpp_nightcity_native_skin_acceptance>"
        "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
        "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/assets")
    set_tests_properties(northstarcpp_nightcity_native_skin PROPERTIES TIMEOUT 120)

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
        "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_skeletal_asset_interop PROPERTIES TIMEOUT 45)
    # Real game pipeline: YMT + YDD + YCD + YMAT + YTD -> skinned renderer.
    # WARP makes this a reproducible hardware-independent acceptance test.
    add_test(NAME northstarcpp_project_character_warp
        COMMAND "$<TARGET_FILE:northstarcpp_player>"
        --warp --character-smoke --explore
        --scene "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscn"
        --shared-assets "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
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


# Engine API JavaScript lives in the engine source, never in project/sharedAssets.
# Generate a C++ read-only resource table and link it into each native runtime.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(NORTHSTARCPP_EMBEDDED_API_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/resources/api")
if(NOT EXISTS "${NORTHSTARCPP_EMBEDDED_API_ROOT}/northstar/bootstrap.js")
    # Support the transitional bundled engine API root until it is moved.
    # Never copy API files from old build trees or project-level scripts.
    set(NORTHSTARCPP_EMBEDDED_API_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/sharedAssets/api")
endif()
if(NOT EXISTS "${NORTHSTARCPP_EMBEDDED_API_ROOT}/northstar/bootstrap.js")
    message(FATAL_ERROR "Embedded NorthStar API scripts missing in engine resources")
endif()
file(GLOB_RECURSE NORTHSTARCPP_EMBEDDED_API_INPUTS CONFIGURE_DEPENDS
    "${NORTHSTARCPP_EMBEDDED_API_ROOT}/*.js"
    "${NORTHSTARCPP_EMBEDDED_API_ROOT}/*.d.ts")
set(NORTHSTARCPP_EMBEDDED_API_CPP
    "${PROJECT_BINARY_DIR}/generated/northstar_embedded_api_scripts.cpp")
add_custom_command(
    OUTPUT "${NORTHSTARCPP_EMBEDDED_API_CPP}"
    COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/dev/embed_engine_api.py"
        --root "${NORTHSTARCPP_EMBEDDED_API_ROOT}"
        --out "${NORTHSTARCPP_EMBEDDED_API_CPP}"
    DEPENDS "${PROJECT_SOURCE_DIR}/tools/dev/embed_engine_api.py"
            "${PROJECT_SOURCE_DIR}/tools/dev/compose_northstar_bootstrap.py"
            "${NORTHSTARCPP_EMBEDDED_API_ROOT}/manifest.json"
            ${NORTHSTARCPP_EMBEDDED_API_INPUTS}
    COMMENT "Embedding immutable NorthStar Engine API scripts"
    VERBATIM)
if(BUILD_TESTING)
    add_test(NAME northstarcpp_sdk_bootstrap_composition
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tools/dev/compose_northstar_bootstrap.py"
            --root "${NORTHSTARCPP_EMBEDDED_API_ROOT}" --verify)
endif()

add_library(northstarcpp_runtime STATIC modules/host/scene.cpp modules/host/scene_v3.cpp modules/host/scene_binary.cpp modules/host/scene_nef8.cpp modules/host/scene_nef8_chunked.cpp modules/host/scene_component_registry.cpp modules/host/scene_world.cpp modules/host/physics.cpp modules/host/ecs_world.cpp modules/host/audio.cpp modules/host/logger.cpp modules/host/perlin.cpp modules/host/scripting.cpp modules/host/input.cpp modules/host/assets.cpp modules/host/fps_assets.cpp modules/host/fps_game.cpp modules/host/fps_weapon_effects.cpp modules/host/camera.cpp modules/host/camera_cutscene.cpp modules/host/camera_presets.cpp modules/host/camera_track_assets.cpp modules/host/scene_animation_assets.cpp modules/host/mesh_asset_assets.cpp modules/host/module_loader.cpp)
target_sources(northstarcpp_runtime PRIVATE
    "${NORTHSTARCPP_EMBEDDED_API_CPP}"
    modules/host/engine_script_bootstrap.cpp
    modules/host/terrain_physics_bridge.cpp
    modules/host/character_motor_provider_bridge.cpp
    modules/host/asset_list_file_writer.cpp
    modules/host/nac_nef8_metadata_write.cpp
    modules/host/nac_nef8_metadata_read.cpp
    modules/host/nac_encode.cpp
    modules/host/nac_decode.cpp
    modules/host/nac_inspect.cpp
    modules/host/asset_list_file.cpp
    modules/models/character.cpp
    modules/animation/skeleton.cpp
    modules/animation/animation_graph_xml.cpp
    modules/animation/animation_graph_runtime.cpp
    modules/animation/animation_graph_async.cpp
    modules/animation/skeleton_ik.cpp
    modules/animation/procedural_ik.cpp
    modules/animation/posture_ik.cpp
    modules/host/save_game.cpp
    modules/host/asset_streaming.cpp
    modules/host/scene_chunk_streaming.cpp
    modules/host/scene_navigation_cells.cpp
    modules/host/asset_zones.cpp
    modules/host/shared_asset_models.cpp
    modules/host/material_ymat_authoring.cpp
    modules/host/material_ymat_xml.cpp
    modules/host/material_graph_xml.cpp
    modules/host/shared_asset_textures.cpp
    modules/host/shared_assets.cpp
    modules/host/shared_asset_dictionary_index.cpp
    modules/host/procedural_environment.cpp
    modules/host/perlin_service.cpp
    modules/host/geometry_service.cpp
    modules/host/vehicle_service.cpp
    modules/host/vehicle_rig_xml.cpp
    modules/host/provider_inspector.cpp
    modules/host/geometry_scene.cpp
    modules/material/material.cpp
    modules/material/material_graph.cpp
    modules/material/material_functions.cpp
    modules/material/material_precache.cpp)
northstarcpp_unprefix_module_output(northstarcpp_runtime)
add_executable(northstarcpp_nac_pack "${PROJECT_SOURCE_DIR}/tools/nac_pack.cpp")
target_link_libraries(northstarcpp_nac_pack PRIVATE northstarcpp_runtime)
target_link_options(northstarcpp_nac_pack PRIVATE -static)
set_target_properties(northstarcpp_nac_pack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(northstarcpp_runtime PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}>"
    "$<INSTALL_INTERFACE:sdk/include>")
target_include_directories(northstarcpp_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/host"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared"
    "${NORTHSTARCPP_VENDOR_ROOT}/bcdec"
    )
northstarcpp_builtin_module(northstarcpp_runtime perlin improved modules/perlin/improved/improved.cpp)
northstarcpp_builtin_module(northstarcpp_runtime ui native modules/ui/native/native.cpp)
# Native game vehicle dynamics: builtin by default; optional DLL replaces the API.
northstarcpp_builtin_module(northstarcpp_runtime vehicle native modules/vehicle/native/native.cpp)
if(BUILD_TESTING)
    add_executable(northstarcpp_vehicle_native_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_native_smoke.cpp"
        modules/vehicle/native/native.cpp)
    target_include_directories(northstarcpp_vehicle_native_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_vehicle_native_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_native
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_native_smoke>")
    set_tests_properties(northstarcpp_vehicle_native PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_mt28_native_preload_integration_smoke
        "${PROJECT_SOURCE_DIR}/tests/mt28_native_preload_integration_smoke.cpp")
    target_include_directories(northstarcpp_mt28_native_preload_integration_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_mt28_native_preload_integration_smoke
        PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_mt28_native_preload_integration_smoke PRIVATE -static)
    add_test(NAME northstarcpp_mt28_native_preload_integration
        COMMAND "$<TARGET_FILE:northstarcpp_mt28_native_preload_integration_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity")
    set_tests_properties(northstarcpp_mt28_native_preload_integration
        PROPERTIES TIMEOUT 60)
    add_executable(northstarcpp_viewport_native_preload_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_native_preload_smoke.cpp")
    target_include_directories(northstarcpp_viewport_native_preload_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_viewport_native_preload_smoke PRIVATE northstarcpp_math)
    target_link_options(northstarcpp_viewport_native_preload_smoke PRIVATE -static)
    add_test(NAME northstarcpp_viewport_native_preload
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_native_preload_smoke>")
    set_tests_properties(northstarcpp_viewport_native_preload PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_d3d12_rigid_instances_warp_smoke
        "${PROJECT_SOURCE_DIR}/tests/d3d12_rigid_instances_warp_smoke.cpp")
    target_include_directories(northstarcpp_d3d12_rigid_instances_warp_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_d3d12_rigid_instances_warp_smoke PRIVATE -static)
    add_dependencies(northstarcpp_d3d12_rigid_instances_warp_smoke northstarcpp_render_d3d12)
    add_test(NAME northstarcpp_d3d12_rigid_instances_warp
        COMMAND "$<TARGET_FILE:northstarcpp_d3d12_rigid_instances_warp_smoke>"
            "$<TARGET_FILE:northstarcpp_render_d3d12>")
    set_tests_properties(northstarcpp_d3d12_rigid_instances_warp
        PROPERTIES TIMEOUT 45)
    add_executable(northstarcpp_render_frame_texture_memo_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_frame_texture_memo_smoke.cpp")
    target_include_directories(northstarcpp_render_frame_texture_memo_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_render_frame_texture_memo_smoke PRIVATE -static)
    add_test(NAME northstarcpp_render_frame_texture_memo
        COMMAND "$<TARGET_FILE:northstarcpp_render_frame_texture_memo_smoke>")
    set_tests_properties(northstarcpp_render_frame_texture_memo PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_vehicle_world_scaling_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_world_scaling_smoke.cpp"
        modules/vehicle/native/native.cpp)
    target_include_directories(northstarcpp_vehicle_world_scaling_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_vehicle_world_scaling_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_world_scaling
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_world_scaling_smoke>")
    set_tests_properties(northstarcpp_vehicle_world_scaling PROPERTIES TIMEOUT 40)
    add_executable(northstarcpp_vehicle_powertrain_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_powertrain_smoke.cpp"
        modules/vehicle/native/native.cpp)
    target_include_directories(northstarcpp_vehicle_powertrain_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_vehicle_powertrain_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_powertrain
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_powertrain_smoke>")
    set_tests_properties(northstarcpp_vehicle_powertrain PROPERTIES TIMEOUT 35)
    add_executable(northstarcpp_vehicle_dynamics_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_dynamics_smoke.cpp"
        modules/vehicle/native/native.cpp)
    target_include_directories(northstarcpp_vehicle_dynamics_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_vehicle_dynamics_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_dynamics
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_dynamics_smoke>")
    set_tests_properties(northstarcpp_vehicle_dynamics PROPERTIES TIMEOUT 25)
    add_executable(northstarcpp_vehicle_cabin_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_cabin_smoke.cpp"
        modules/vehicle/native/native.cpp)
    target_include_directories(northstarcpp_vehicle_cabin_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
    target_link_options(northstarcpp_vehicle_cabin_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_cabin
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_cabin_smoke>")
    set_tests_properties(northstarcpp_vehicle_cabin PROPERTIES TIMEOUT 30)

    add_executable(northstarcpp_vehicle_rig_xml_smoke
        "${PROJECT_SOURCE_DIR}/tests/vehicle_rig_xml_smoke.cpp")
    target_link_libraries(northstarcpp_vehicle_rig_xml_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_vehicle_rig_xml_smoke PRIVATE -static)
    add_test(NAME northstarcpp_vehicle_rig_xml
        COMMAND "$<TARGET_FILE:northstarcpp_vehicle_rig_xml_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/vehicles/mahir_mt28.nvehicle"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/vehicles/mahir_mt28_powertrain_example.nvehicle")
    set_tests_properties(northstarcpp_vehicle_rig_xml PROPERTIES TIMEOUT 20)
    add_executable(northstarcpp_native_vehicle_scene_smoke
        "${PROJECT_SOURCE_DIR}/tests/native_vehicle_scene_smoke.cpp")
    target_link_libraries(northstarcpp_native_vehicle_scene_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_native_vehicle_scene_smoke PRIVATE -static)
    add_dependencies(northstarcpp_native_vehicle_scene_smoke
        northstarcpp_physics_jolt northstarcpp_ecs_flecs)
    add_custom_command(TARGET northstarcpp_native_vehicle_scene_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:northstarcpp_physics_jolt>"
            "$<TARGET_FILE_DIR:northstarcpp_native_vehicle_scene_smoke>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:northstarcpp_ecs_flecs>"
            "$<TARGET_FILE_DIR:northstarcpp_native_vehicle_scene_smoke>")

    add_test(NAME northstarcpp_native_vehicle_scene
        COMMAND "$<TARGET_FILE:northstarcpp_native_vehicle_scene_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/main.nscn"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/vehicles/mahir_mt28.nvehicle")
    add_executable(northstarcpp_jolt_vehicle_abi_smoke
        "${PROJECT_SOURCE_DIR}/tests/jolt_vehicle_integration_smoke.cpp")
    target_include_directories(northstarcpp_jolt_vehicle_abi_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_options(northstarcpp_jolt_vehicle_abi_smoke PRIVATE -static)
    add_dependencies(northstarcpp_jolt_vehicle_abi_smoke
        northstarcpp_physics_jolt)
    add_test(NAME northstarcpp_jolt_vehicle_abi
        COMMAND "$<TARGET_FILE:northstarcpp_jolt_vehicle_abi_smoke>"
            "$<TARGET_FILE:northstarcpp_physics_jolt>")
    set_tests_properties(northstarcpp_jolt_vehicle_abi PROPERTIES TIMEOUT 40)
    set_tests_properties(northstarcpp_native_vehicle_scene PROPERTIES
        TIMEOUT 75 ENVIRONMENT "NORTHSTARCPP_PHYSICS_PROVIDER=jolt")

endif()
# Native text shaping is optional for engine.ui. Keep its C ABI stable
# if FreeType/HarfBuzz are absent; the embedded Latin font remains fallback.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(NORTHSTAR_UI_FT QUIET IMPORTED_TARGET freetype2)
    pkg_check_modules(NORTHSTAR_UI_HB QUIET IMPORTED_TARGET harfbuzz)
    if(TARGET PkgConfig::NORTHSTAR_UI_FT AND TARGET PkgConfig::NORTHSTAR_UI_HB)
        target_compile_definitions(northstarcpp_runtime PRIVATE
            NORTHSTARCPP_UI_HAS_FONT_SHAPING=1)
        target_link_libraries(northstarcpp_runtime PUBLIC
            PkgConfig::NORTHSTAR_UI_FT PkgConfig::NORTHSTAR_UI_HB)
        # Propagate conditional SDK requirements to the root package config.
        set(NORTHSTARCPP_SDK_HAS_FONT_SHAPING ON PARENT_SCOPE)
        message(STATUS "NorthStar native UI: FreeType/HarfBuzz enabled")
    else()
        message(STATUS "NorthStar native UI: embedded font fallback (no shaping dependencies)")
    endif()
endif()
target_sources(northstarcpp_runtime PRIVATE
    modules/host/collision_service.cpp
    modules/collision/ray_triangle_core.cpp)
target_include_directories(northstarcpp_runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/collision")
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
if(BUILD_TESTING)
    add_executable(northstarcpp_ui_native_smoke
        "${PROJECT_SOURCE_DIR}/tests/ui_native_smoke.cpp")
    target_include_directories(northstarcpp_ui_native_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/host")
    target_link_libraries(northstarcpp_ui_native_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_ui_native_smoke PRIVATE -static)
    add_test(NAME northstarcpp_ui_native
        COMMAND "$<TARGET_FILE:northstarcpp_ui_native_smoke>")
    set_tests_properties(northstarcpp_ui_native PROPERTIES TIMEOUT 20)
endif()
# Bootstrap JSON uses vendored header-only Boost.PropertyTree (no runtime DLL).
# Keep internal Boost/property_tree consumers on the same Boost ABI.
target_include_directories(northstarcpp_runtime SYSTEM PUBLIC
    "$<BUILD_INTERFACE:${NORTHSTARCPP_VENDOR_INCLUDE_DIR}>")
target_link_libraries(northstarcpp_runtime PUBLIC northstarcpp_world_foundation northstarcpp_math
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
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
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
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
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
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
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
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
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
    # The standalone correctness tests work without optional sample projects.
    # Include the AnimationLab integration fixture only when present.
    if(EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/main.nscn" AND
       EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/animations/lift.anim.json")
        add_test(NAME northstarcpp_scene_animation
            COMMAND "$<TARGET_FILE:northstarcpp_scene_animation_smoke>"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/main.nscn"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarAnimationLab/animations/lift.anim.json")
    else()
        add_test(NAME northstarcpp_scene_animation
            COMMAND "$<TARGET_FILE:northstarcpp_scene_animation_smoke>")
    endif()
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
            "NORTHSTARCPP_PERLIN_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
            "$<TARGET_FILE:northstarcpp_perlin_smoke>" reject)
    set_tests_properties(northstarcpp_perlin northstarcpp_perlin_reject_incompatible
        PROPERTIES TIMEOUT 35)
endif()

# Native NSCN migration utility keeps legacy scene files intact.
add_executable(northstarcpp_mt28_collider_author modules/tools/mt28_collider_author.cpp)
target_link_libraries(northstarcpp_mt28_collider_author PRIVATE northstarcpp_runtime)
target_link_options(northstarcpp_mt28_collider_author PRIVATE -static)
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
    # A pure C++ contract test, independent of optional V8 SDK components.
    add_executable(northstarcpp_script_api_hierarchy_smoke
        "${PROJECT_SOURCE_DIR}/tests/script_api_hierarchy_smoke.cpp")
    target_compile_features(northstarcpp_script_api_hierarchy_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_script_api_hierarchy
        COMMAND "$<TARGET_FILE:northstarcpp_script_api_hierarchy_smoke>")
    find_program(NORTHSTARCPP_NODE_EXECUTABLE NAMES node)
    if(NORTHSTARCPP_NODE_EXECUTABLE)
        add_test(NAME northstarcpp_script_api_hierarchy_bridge
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/script_api_hierarchy_bridge_smoke.cjs")
        add_test(NAME northstarcpp_render_nested_namespace
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/render_nested_namespace_smoke.cjs")
        add_test(NAME northstarcpp_entity_event_batch
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/entity_event_batch_smoke.cjs")
        set_tests_properties(northstarcpp_entity_event_batch PROPERTIES TIMEOUT 15)
        add_test(NAME northstarcpp_root_events_and_footsteps
            COMMAND "${NORTHSTARCPP_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_footsteps_events_smoke.cjs")
        set_tests_properties(northstarcpp_root_events_and_footsteps
            PROPERTIES TIMEOUT 15)
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
        northstarcpp_audio_northstar northstarcpp_assets_directory)
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
            "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
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

add_library(northstarcpp_viewport STATIC
    modules/viewport/viewport.cpp
    modules/viewport/viewport_draw.cpp
    modules/viewport/viewport_frame_builder.cpp
    modules/viewport/viewport_frame_submission.cpp
    modules/viewport/viewport_procedural_geometry.cpp
    modules/viewport/viewport_skeletal_animation.cpp
    modules/viewport/viewport_weapon_grip_profiles.cpp
    modules/viewport/viewport_procedural_ik.cpp
    modules/viewport/viewport_grounded_locomotion.cpp
    modules/viewport/viewport_posture_ik.cpp
    modules/viewport/viewport_skin_cache.cpp)
northstarcpp_unprefix_module_output(northstarcpp_viewport)
target_include_directories(northstarcpp_viewport PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/host")
target_link_libraries(northstarcpp_viewport PUBLIC northstarcpp_runtime PRIVATE northstarcpp_blake3)

# Native Windows crash capture shared by editor, player and Project Manager.
# bugtrap.exe is the out-of-process report viewer; it is part of the default build.
add_library(northstarcpp_crash STATIC modules/shared/crash_handler.cpp)
northstarcpp_unprefix_module_output(northstarcpp_crash)
target_include_directories(northstarcpp_crash PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/modules/shared")
target_link_libraries(northstarcpp_crash PUBLIC dbghelp)
target_compile_features(northstarcpp_crash PUBLIC cxx_std_17)

add_executable(bugtrap WIN32 modules/shared/bugtrap_main.cpp)
target_link_libraries(bugtrap PRIVATE shell32 user32 gdi32)
target_link_options(bugtrap PRIVATE -static)
set_target_properties(bugtrap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(BUILD_TESTING)
    add_test(NAME northstarcpp_bugtrap_smoke
        COMMAND "$<TARGET_FILE:bugtrap>" --self-test)
endif()

if(BUILD_TESTING AND WIN32 AND Python3_EXECUTABLE)
    # Real child-process failures: SEH and std::terminate must save a report + dump.
    add_executable(northstarcpp_crash_capture_smoke
        "${PROJECT_SOURCE_DIR}/tests/crash_capture_smoke.cpp")
    target_link_libraries(northstarcpp_crash_capture_smoke PRIVATE northstarcpp_crash)
    target_link_options(northstarcpp_crash_capture_smoke PRIVATE -static)
    set_target_properties(northstarcpp_crash_capture_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_crash_capture_smoke bugtrap)
    add_test(NAME northstarcpp_bugtrap_crash_capture
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/bugtrap_crash_capture_smoke.py"
            "$<TARGET_FILE:northstarcpp_crash_capture_smoke>"
            "$<TARGET_FILE:bugtrap>")
    set_tests_properties(northstarcpp_bugtrap_crash_capture PROPERTIES TIMEOUT 75)
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
    modules/project_manager/qt_project_browser.cpp
    modules/project_manager/provider_inventory.cpp)
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

# Cross-provider PostFX/GBuffer ABI semantics: fast, GPU-independent checks.
if(BUILD_TESTING)
    add_executable(northstarcpp_render_effect_contract_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_effect_contract_smoke.cpp")
    target_include_directories(northstarcpp_render_effect_contract_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_render_effect_contract_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_effect_contract_smoke
        COMMAND "$<TARGET_FILE:northstarcpp_render_effect_contract_smoke>")
endif()

# Crash-isolated hardware renderer probe used by Project Browser.
# This executable deliberately has no dependencies on Qt or GPU providers:
# it loads the exact selected module at runtime and returns one JSON report.
if(WIN32)
    add_executable(northstarcpp_gpu_provider_probe
        "${PROJECT_SOURCE_DIR}/tests/render_provider_device_probe.cpp")
    target_include_directories(northstarcpp_gpu_provider_probe PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_gpu_provider_probe PRIVATE cxx_std_17)
    target_link_options(northstarcpp_gpu_provider_probe PRIVATE -static)
    set_target_properties(northstarcpp_gpu_provider_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_project_manager northstarcpp_gpu_provider_probe)
    if(BUILD_TESTING)
        add_test(NAME northstarcpp_device_probe_d3d11_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d11>" --full)
        add_test(NAME northstarcpp_device_probe_d3d12_stress
            COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --stress=1000)
        set_tests_properties(northstarcpp_device_probe_d3d12_stress
            PROPERTIES TIMEOUT 90)
        add_test(NAME northstarcpp_device_probe_d3d12_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --full)
        set_tests_properties(
            northstarcpp_device_probe_d3d11_hardware
            northstarcpp_device_probe_d3d12_hardware
            PROPERTIES TIMEOUT 55)
        # Diagnostic GBuffer modes deliberately change material colors.
        # Use geometry/device/picking verification, not the regular --full
        # comparison against original lit/textured pixel values.
        foreach(_gbuffer_mode albedo normal roughness depth ao)
            add_test(NAME northstarcpp_device_probe_d3d12_gbuffer_${_gbuffer_mode}
                COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                        "$<TARGET_FILE:northstarcpp_render_d3d12>")
            set_tests_properties(
                northstarcpp_device_probe_d3d12_gbuffer_${_gbuffer_mode}
                PROPERTIES
                ENVIRONMENT "NORTHSTARCPP_GBUFFER_DEBUG=${_gbuffer_mode}"
                TIMEOUT 25)
        endforeach()
        if(TARGET northstarcpp_render_vulkan)
            add_test(NAME northstarcpp_renderer_gbuffer_parity_hardware
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/renderer_gbuffer_parity_gpu.py"
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(
                northstarcpp_renderer_gbuffer_parity_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
            add_test(NAME northstarcpp_renderer_parity_fixture_hardware
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/renderer_cross_api_fixture_gpu.py"
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(
                northstarcpp_renderer_parity_fixture_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 165)
        endif()
        add_test(NAME northstarcpp_d3d12_transparency_hardware
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/vulkan_transparency_layers_gpu.py"
                "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                "$<TARGET_FILE:northstarcpp_render_d3d12>")
        set_tests_properties(northstarcpp_d3d12_transparency_hardware
            PROPERTIES TIMEOUT 160 SKIP_RETURN_CODE 77)
        add_test(NAME northstarcpp_device_probe_d3d12_postfx_hardware
            COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --full)
        add_test(NAME northstarcpp_device_probe_d3d12_postfx_stress
            COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --stress=100)
        set_tests_properties(
            northstarcpp_device_probe_d3d12_postfx_hardware
            northstarcpp_device_probe_d3d12_postfx_stress
            PROPERTIES
                ENVIRONMENT "NORTHSTARCPP_ENABLE_BLOOM=1;NORTHSTARCPP_ENABLE_HDR=1;NORTHSTARCPP_ENABLE_SSAO=1;NORTHSTARCPP_ENABLE_SHADOWS=1;NORTHSTARCPP_ENABLE_PCSS=1"
                TIMEOUT 70)
        if(TARGET northstarcpp_render_vulkan)
            add_test(NAME northstarcpp_velocity_cross_api_hardware
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/render_velocity_parity_gpu.py"
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_velocity_cross_api_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240)
            add_test(NAME northstarcpp_vulkan_velocity_hardware
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/render_vulkan_velocity_gpu.py"
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_vulkan_velocity_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 160)
            add_test(NAME northstarcpp_vulkan_transparency_hardware
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/vulkan_transparency_layers_gpu.py"
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_vulkan_transparency_hardware
                PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 160)
            add_test(NAME northstarcpp_device_probe_vulkan_hdr_world_hardware
                COMMAND "${CMAKE_COMMAND}" -E env
                    NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1
                    NORTHSTARCPP_ENABLE_VULKAN_HDR=1
                    NORTHSTARCPP_VULKAN_AUTO_EXPOSURE=1
                    NORTHSTARCPP_GBUFFER_VERSION=2
                    "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>" --full)
            set_tests_properties(northstarcpp_device_probe_vulkan_hdr_world_hardware
                PROPERTIES TIMEOUT 50)
            add_test(NAME northstarcpp_device_probe_vulkan_stress
                COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                        "$<TARGET_FILE:northstarcpp_render_vulkan>" --stress=1000)
            set_tests_properties(northstarcpp_device_probe_vulkan_stress
                PROPERTIES TIMEOUT 90)
            add_test(NAME northstarcpp_device_probe_vulkan_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_gpu_provider_probe>"
                        "$<TARGET_FILE:northstarcpp_render_vulkan>" --full)
            set_tests_properties(northstarcpp_device_probe_vulkan_hardware
                PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()

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
    modules/editor/asset_catalog.cpp
    modules/editor/asset_catalog_nef8_cache.cpp)
target_include_directories(northstarcpp_asset_catalog PUBLIC
    "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
target_compile_features(northstarcpp_asset_catalog PUBLIC cxx_std_17)
target_link_libraries(northstarcpp_asset_catalog PUBLIC northstarcpp_runtime)

if(BUILD_TESTING)
    add_executable(northstarcpp_embedded_engine_api_smoke
        "${PROJECT_SOURCE_DIR}/tests/embedded_engine_api_smoke.cpp")
    target_link_libraries(northstarcpp_embedded_engine_api_smoke PRIVATE
        northstarcpp_asset_catalog northstarcpp_runtime)
    add_test(NAME northstarcpp_embedded_engine_api
        COMMAND "$<TARGET_FILE:northstarcpp_embedded_engine_api_smoke>"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
            "${NORTHSTARCPP_EMBEDDED_API_ROOT}")
endif()

add_library(northstarcpp_asset_dependency STATIC
    modules/editor/asset_dependency_registry.hpp
    modules/editor/asset_dependency_registry.cpp
    modules/editor/asset_dependency_registry_moves.cpp)
target_include_directories(northstarcpp_asset_dependency PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_asset_dependency PUBLIC
    northstarcpp_asset_catalog Qt6::Core northstarcpp_runtime)
target_compile_features(northstarcpp_asset_dependency PUBLIC cxx_std_17)

# Shared native Qt viewport Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р вЂ™Р’В Р В Р’В Р вЂ™Р’В Р В РІР‚в„ўР вЂ™Р’В Р В Р’В Р В РІР‚В Р В Р’В Р Р†Р вЂљРЎв„ўР В Р Р‹Р Р†РІР‚С›РЎС›Р В Р’В Р вЂ™Р’В Р В Р’В Р Р†Р вЂљР’В Р В Р’В Р вЂ™Р’В Р В Р вЂ Р В РІР‚С™Р РЋРІвЂћСћР В Р’В Р В Р вЂ№Р В Р Р‹Р РЋРІвЂћСћ consumed by both the full editor and the
# prefab collision editor hosted from the standalone Content Browser.
# Compile the implementation and its AUTOMOC metadata only once.
add_library(northstarcpp_qt_editor_viewport STATIC
    modules/editor/qt_editor_viewport.hpp
    modules/editor/qt_editor_viewport.cpp
    modules/editor/qt_editor_gizmo.hpp
    modules/editor/qt_editor_gizmo.cpp)
set_target_properties(northstarcpp_qt_editor_viewport PROPERTIES AUTOMOC ON)
target_include_directories(northstarcpp_qt_editor_viewport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_qt_editor_viewport PUBLIC
    Qt6::Widgets northstarcpp_editor_tools northstarcpp_runtime
    northstarcpp_viewport)
target_compile_features(northstarcpp_qt_editor_viewport PUBLIC cxx_std_17)

# Qt 6 Content Browser UI; reusable in any editor dock or document tab.
# No Win32 common controls, native file dialogs, or Shell icons are used.
add_library(northstarcpp_content_browser_qt STATIC
    modules/editor/qt_asset_card_delegate.hpp
    modules/editor/qt_asset_card_delegate.cpp
    modules/editor/qt_content_browser.hpp
    modules/editor/qt_content_browser_internal.hpp
    modules/editor/qt_content_browser.cpp
    modules/editor/qt_content_browser_import.cpp
    modules/editor/qt_content_import.hpp
    modules/editor/qt_content_import.cpp
    modules/editor/qt_content_import_merge.hpp
    modules/editor/qt_content_import_merge.cpp
    modules/editor/qt_text_asset_editor.hpp
    modules/editor/qt_text_asset_editor.cpp
    modules/editor/qt_text_asset_workspace.hpp
    modules/editor/qt_text_asset_workspace.cpp
    modules/editor/qt_content_browser_actions.cpp
    modules/editor/qt_content_browser_collections.cpp
    modules/editor/qt_content_browser_batch.cpp
    modules/editor/qt_content_browser_git.cpp
    modules/editor/qt_content_browser_registry.cpp
    modules/editor/qt_content_browser_mutation.cpp
    modules/editor/qt_content_browser_thumbnails.hpp
    modules/editor/qt_content_browser_thumbnails.cpp
    modules/editor/qt_asset_preview_dialog.hpp
    modules/editor/qt_asset_preview_dialog.cpp
    modules/editor/qt_particle_simulation_dialog.hpp
    modules/editor/qt_particle_simulation_dialog.cpp
    modules/editor/qt_audio_preview_dialog.hpp
    modules/editor/qt_audio_preview_dialog.cpp
    modules/editor/qt_material_authoring.cpp
    modules/editor/qt_material_instances.cpp
    modules/editor/qt_material_function_editor.cpp
    modules/editor/qt_material_asset_dialog.cpp
    modules/editor/qt_material_library_edit.cpp
    modules/editor/qt_material_graph_editor.hpp
    modules/editor/qt_material_graph_editor.cpp
    modules/editor/qt_material_graph_clipboard.cpp
    modules/editor/qt_material_graph_groups.cpp
    modules/editor/qt_asset_warnings.hpp
    modules/editor/qt_asset_warnings.cpp
    modules/editor/editor_asset_material_source.hpp
    modules/editor/editor_asset_material_source.cpp)
set_target_properties(northstarcpp_content_browser_qt PROPERTIES AUTOMOC ON)
if(BUILD_TESTING)
    add_executable(northstarcpp_qt_menu_hover_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_menu_hover_smoke.cpp")
    target_include_directories(northstarcpp_qt_menu_hover_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_menu_hover_smoke PRIVATE Qt6::Widgets)
    target_compile_features(northstarcpp_qt_menu_hover_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_qt_menu_hover
        COMMAND "$<TARGET_FILE:northstarcpp_qt_menu_hover_smoke>")
    set_tests_properties(northstarcpp_qt_menu_hover PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 35)
    add_executable(northstarcpp_qt_text_asset_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_text_asset_editor_smoke.cpp")
    target_include_directories(northstarcpp_qt_text_asset_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_text_asset_smoke PRIVATE
        northstarcpp_content_browser_qt Qt6::Widgets)
    target_compile_features(northstarcpp_qt_text_asset_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_qt_text_asset
        COMMAND "$<TARGET_FILE:northstarcpp_qt_text_asset_smoke>")
    set_tests_properties(northstarcpp_qt_text_asset PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 35)
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

# Qt artwork is now an external RCC inside northstar_shared.nsp.
# Keep Qt :/northstar/... names stable while eliminating generated C++ PNG blobs.
target_sources(northstarcpp_content_browser_qt PRIVATE
    modules/editor/qt_nsp_icons.cpp)
add_dependencies(northstarcpp_content_browser_qt
    northstarcpp_shared_nsp northstarcpp_assets_directory)

target_include_directories(northstarcpp_content_browser_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_content_browser_qt PUBLIC
    Qt6::Widgets Qt6::Concurrent northstarcpp_asset_catalog
    northstarcpp_asset_dependency
    PRIVATE northstarcpp_editor_tools northstarcpp_qt_editor_viewport northstarcpp_collision_cooker)
# Optional compressed-audio preview. NAC/WAV use engine.audio without Qt Multimedia.
find_package(Qt6 COMPONENTS Multimedia QUIET)
if(TARGET Qt6::Multimedia)
    target_link_libraries(northstarcpp_content_browser_qt PRIVATE Qt6::Multimedia)
    target_compile_definitions(northstarcpp_content_browser_qt PRIVATE NORTHSTARCPP_QT_MULTIMEDIA=1)
endif()
target_compile_features(northstarcpp_content_browser_qt PUBLIC cxx_std_17)
if(BUILD_TESTING)
    add_executable(northstarcpp_qt_particle_preview_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_particle_preview_smoke.cpp")
    target_include_directories(northstarcpp_qt_particle_preview_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_particle_preview_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_runtime Qt6::Widgets)
    target_link_options(northstarcpp_qt_particle_preview_smoke PRIVATE -static)
    set_target_properties(northstarcpp_qt_particle_preview_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_qt_particle_preview
        COMMAND "$<TARGET_FILE:northstarcpp_qt_particle_preview_smoke>"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_qt_particle_preview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    add_executable(northstarcpp_qt_nsp_icons_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_nsp_icons_smoke.cpp")
    target_include_directories(northstarcpp_qt_nsp_icons_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_nsp_icons_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_runtime Qt6::Widgets)
    target_link_options(northstarcpp_qt_nsp_icons_smoke PRIVATE -static)
    set_target_properties(northstarcpp_qt_nsp_icons_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_qt_nsp_icons_smoke
        northstarcpp_assets_directory northstarcpp_shared_nsp)
    add_test(NAME northstarcpp_qt_nsp_icons
        COMMAND "$<TARGET_FILE:northstarcpp_qt_nsp_icons_smoke>")
    set_tests_properties(northstarcpp_qt_nsp_icons PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
        TIMEOUT 40)
    add_executable(northstarcpp_code_editor_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_code_editor_smoke.cpp")
    target_include_directories(northstarcpp_code_editor_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_code_editor_smoke PRIVATE
        northstarcpp_content_browser_qt Qt6::Widgets)
    add_test(NAME northstarcpp_code_editor_smoke
        COMMAND "$<TARGET_FILE:northstarcpp_code_editor_smoke>")
    set_tests_properties(northstarcpp_code_editor_smoke PROPERTIES
        TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(northstarcpp_cutscene_sequencer_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_cutscene_timeline_smoke.cpp"
        modules/editor/qt_cutscene_timeline.cpp)
    target_include_directories(northstarcpp_cutscene_sequencer_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_cutscene_sequencer_smoke PRIVATE
        northstarcpp_runtime Qt6::Widgets)
    add_test(NAME northstarcpp_cutscene_sequencer
        COMMAND "$<TARGET_FILE:northstarcpp_cutscene_sequencer_smoke>"
            "${PROJECT_BINARY_DIR}/qa/cinematic-sequencer")
    set_tests_properties(northstarcpp_cutscene_sequencer PROPERTIES
        TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=windows")
    add_executable(northstarcpp_code_editor_dpi_scroll_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_code_editor_dpi_scroll_smoke.cpp")
    target_include_directories(northstarcpp_code_editor_dpi_scroll_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_code_editor_dpi_scroll_smoke PRIVATE
        northstarcpp_content_browser_qt Qt6::Widgets)
    add_test(NAME northstarcpp_code_editor_dpi_scroll
        COMMAND "$<TARGET_FILE:northstarcpp_code_editor_dpi_scroll_smoke>"
            "${PROJECT_BINARY_DIR}/qa/code-editor-dpi")
    set_tests_properties(northstarcpp_code_editor_dpi_scroll PROPERTIES
        TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_SCALE_FACTOR=1")
endif()
add_executable(northstarcpp_ymat_xml_migrate
    "${PROJECT_SOURCE_DIR}/tools/ymat_xml_migrate.cpp")
target_link_libraries(northstarcpp_ymat_xml_migrate PRIVATE northstarcpp_runtime)
target_compile_features(northstarcpp_ymat_xml_migrate PRIVATE cxx_std_17)

if(BUILD_TESTING)
    add_executable(northstarcpp_qt_material_library_edit_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_material_library_edit_smoke.cpp")
    target_include_directories(northstarcpp_qt_material_library_edit_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_material_library_edit_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_runtime Qt6::Core)
    add_test(NAME northstarcpp_qt_material_library_edit
        COMMAND "$<TARGET_FILE:northstarcpp_qt_material_library_edit_smoke>"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(northstarcpp_qt_material_library_edit PROPERTIES
        TIMEOUT 40)

    add_executable(northstarcpp_qt_material_asset_dialog_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_material_asset_dialog_smoke.cpp")
    target_include_directories(northstarcpp_qt_material_asset_dialog_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_qt_material_asset_dialog_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_runtime Qt6::Widgets)
    add_dependencies(northstarcpp_qt_material_asset_dialog_smoke
        northstarcpp_assets_directory)
    add_test(NAME northstarcpp_qt_material_asset_dialog
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
            "$<TARGET_FILE:northstarcpp_qt_material_asset_dialog_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    set_tests_properties(northstarcpp_qt_material_asset_dialog PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 55)
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
target_link_libraries(northstarcpp_asset_browser PRIVATE northstarcpp_crash)
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
        target_link_libraries(northstarcpp_asset_browser_cards_qa PRIVATE northstarcpp_crash)
        set_target_properties(northstarcpp_asset_browser_cards_qa PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        target_link_options(northstarcpp_asset_browser_cards_qa PRIVATE -static)
        set(northstarcpp_browser_test_target northstarcpp_asset_browser_cards_qa)
    endif()
    # Validate the project YTD through AssetCatalog and the native texture decoder.
    add_executable(northstarcpp_menu_ytd_content_browser_smoke
        "${PROJECT_SOURCE_DIR}/tests/menu_ytd_content_browser_smoke.cpp")
    target_link_libraries(northstarcpp_menu_ytd_content_browser_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_menu_ytd_content_browser
        COMMAND "$<TARGET_FILE:northstarcpp_menu_ytd_content_browser_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity/assets")
    set_tests_properties(northstarcpp_menu_ytd_content_browser PROPERTIES
        TIMEOUT 40)
    add_executable(northstarcpp_particle_asset_catalog_smoke
        "${PROJECT_SOURCE_DIR}/tests/particle_asset_catalog_smoke.cpp")
    target_link_libraries(northstarcpp_particle_asset_catalog_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_particle_asset_catalog
        COMMAND "$<TARGET_FILE:northstarcpp_particle_asset_catalog_smoke>"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
            "${PROJECT_BINARY_DIR}/particle_asset_catalog_smoke")
    set_tests_properties(northstarcpp_particle_asset_catalog PROPERTIES
        TIMEOUT 60)
    add_executable(northstarcpp_asset_catalog_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_catalog_smoke.cpp")
    target_link_libraries(northstarcpp_asset_catalog_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_asset_catalog
        COMMAND "$<TARGET_FILE:northstarcpp_asset_catalog_smoke>"
            "${PROJECT_BINARY_DIR}/asset_catalog_smoke"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    add_test(NAME northstarcpp_project_script_assets
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/project_script_assets_layout_smoke.py")
    add_executable(northstarcpp_asset_catalog_nef8_parallel_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_catalog_nef8_parallel_smoke.cpp")
    target_link_libraries(northstarcpp_asset_catalog_nef8_parallel_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_asset_catalog_nef8_parallel
        COMMAND "$<TARGET_FILE:northstarcpp_asset_catalog_nef8_parallel_smoke>"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets"
            "${PROJECT_BINARY_DIR}/asset_catalog_nef8_parallel_smoke")
    set_tests_properties(northstarcpp_asset_catalog_nef8_parallel PROPERTIES
        TIMEOUT 90 RUN_SERIAL TRUE)
    add_executable(northstarcpp_nac_asset_catalog_smoke
        "${PROJECT_SOURCE_DIR}/tests/nac_asset_catalog_smoke.cpp")
    target_link_libraries(northstarcpp_nac_asset_catalog_smoke PRIVATE
        northstarcpp_asset_catalog)
    target_link_options(northstarcpp_nac_asset_catalog_smoke PRIVATE -static)
    add_test(NAME northstarcpp_nac_asset_catalog
        COMMAND "$<TARGET_FILE:northstarcpp_nac_asset_catalog_smoke>"
            "${PROJECT_BINARY_DIR}/nac_asset_catalog_smoke")
    add_executable(northstarcpp_asset_catalog_index_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_catalog_index_smoke.cpp")
    target_link_libraries(northstarcpp_asset_catalog_index_smoke PRIVATE
        northstarcpp_asset_catalog)
    add_test(NAME northstarcpp_asset_catalog_index
        COMMAND "$<TARGET_FILE:northstarcpp_asset_catalog_index_smoke>"
            "${PROJECT_BINARY_DIR}/asset_catalog_index_smoke")
    set_tests_properties(northstarcpp_asset_catalog_index
        PROPERTIES TIMEOUT 45)
    add_executable(northstarcpp_asset_browser_advanced_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_browser_advanced_smoke.cpp")
    target_include_directories(northstarcpp_asset_browser_advanced_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_asset_browser_advanced_smoke PRIVATE
        northstarcpp_asset_catalog Qt6::Core)
    add_test(NAME northstarcpp_asset_browser_advanced
        COMMAND "$<TARGET_FILE:northstarcpp_asset_browser_advanced_smoke>")
    set_tests_properties(northstarcpp_asset_browser_advanced
        PROPERTIES TIMEOUT 35)
    add_executable(northstarcpp_asset_dependency_registry_smoke
        "${PROJECT_SOURCE_DIR}/tests/asset_dependency_registry_smoke.cpp")
    target_link_libraries(northstarcpp_asset_dependency_registry_smoke PRIVATE
        northstarcpp_asset_dependency Qt6::Core)
    add_test(NAME northstarcpp_asset_dependency_registry
        COMMAND "$<TARGET_FILE:northstarcpp_asset_dependency_registry_smoke>")
    set_tests_properties(northstarcpp_asset_dependency_registry PROPERTIES TIMEOUT 40)
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
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_prefab_composition_qt
        PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_prefab_cards_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --prefab-cards-smoke
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_prefab_cards_qt
        PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_content_browser_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>" --smoke)
    add_test(NAME northstarcpp_asset_cards_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --asset-cards-smoke
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_asset_cards_qt
        PROPERTIES TIMEOUT 65 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_git_badge_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --git-badge-smoke)
    set_tests_properties(northstarcpp_git_badge_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_content_browser_stable_dock_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --layout-stability-smoke)
    set_tests_properties(northstarcpp_content_browser_stable_dock_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_collision_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --collision-icons-smoke)
    set_tests_properties(northstarcpp_collision_icons_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_nac_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --nac-icons-smoke)
    set_tests_properties(northstarcpp_nac_icons_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_embedded_ycd_ymt_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --embedded-icons-smoke
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_embedded_ycd_ymt_icons_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_asset_category_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --category-icons-smoke)
    set_tests_properties(northstarcpp_asset_category_icons_qt
        PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_folder_icons_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --folder-icons-smoke
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_folder_icons_qt
        PROPERTIES TIMEOUT 50 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_nef8_content_browser_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --smoke --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
    set_tests_properties(northstarcpp_nef8_content_browser_qt
        PROPERTIES TIMEOUT 75 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_nef8_modal_preview_qt
        COMMAND "$<TARGET_FILE:${northstarcpp_browser_test_target}>"
            --smoke --preview-modal-smoke
            --content "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
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

# Camera-following XZ grid geometry is independent of Qt and the GPU
# backend. Validate long-distance authoring and adaptive density separately.
if(BUILD_TESTING)
    add_executable(northstarcpp_editor_grid_smoke
        "${PROJECT_SOURCE_DIR}/tests/editor_grid_smoke.cpp")
    target_include_directories(northstarcpp_editor_grid_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules/api/include")
    target_link_libraries(northstarcpp_editor_grid_smoke PRIVATE
        northstarcpp_math)
    add_test(NAME northstarcpp_editor_grid
        COMMAND "$<TARGET_FILE:northstarcpp_editor_grid_smoke>")
endif()

# Viewport gizmo QA: QPainter raster + genuine Qt pointer events against the
# same first-class Gizmo geometry used by the native GPU overlay.
if(BUILD_TESTING)
    add_executable(northstarcpp_qt_editor_gizmo_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_editor_gizmo_smoke.cpp"
        modules/editor/qt_editor_viewport.hpp
        modules/editor/qt_editor_viewport.cpp
        modules/editor/qt_editor_gizmo.cpp)
    set_target_properties(northstarcpp_qt_editor_gizmo_smoke PROPERTIES AUTOMOC ON)
    target_include_directories(northstarcpp_qt_editor_gizmo_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    # qt_editor_viewport calls material_authoring helpers, which are owned by
    # the shared Qt Content Browser library rather than the viewport target.
    target_link_libraries(northstarcpp_qt_editor_gizmo_smoke PRIVATE
        Qt6::Widgets northstarcpp_editor_tools northstarcpp_runtime
        northstarcpp_content_browser_qt)
    if(WIN32)
        target_link_libraries(northstarcpp_qt_editor_gizmo_smoke PRIVATE
            northstarcpp_viewport)
    endif()
    add_test(NAME northstarcpp_qt_editor_gizmo
        COMMAND "$<TARGET_FILE:northstarcpp_qt_editor_gizmo_smoke>")
    if(WIN32)
        set_tests_properties(northstarcpp_qt_editor_gizmo PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 50)
    else()
        set_tests_properties(northstarcpp_qt_editor_gizmo PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 50)
    endif()
endif()

# Layout-only QA uses the exact scroll wrapper installed by Qt Editor,
# without rebuilding Bullet/Jolt, GPU providers or runtime DLLs.
if(BUILD_TESTING)
    add_executable(northstarcpp_viewport_frame_profile_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_frame_profile_smoke.cpp")
    target_include_directories(northstarcpp_viewport_frame_profile_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_compile_features(northstarcpp_viewport_frame_profile_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_frame_quantiles
        COMMAND northstarcpp_viewport_frame_profile_smoke)
    add_executable(northstarcpp_inspector_scroll_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_inspector_scroll_smoke.cpp"
        modules/editor/qt_inspector_scroll.cpp)
    target_include_directories(northstarcpp_inspector_scroll_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_inspector_scroll_smoke PRIVATE Qt6::Widgets)
    add_test(NAME northstarcpp_inspector_scroll_components
        COMMAND "$<TARGET_FILE:northstarcpp_inspector_scroll_smoke>")
    set_tests_properties(northstarcpp_inspector_scroll_components PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()

# Dockable cross-platform Qt editor shell. The CPU scene proxy viewport is
# a deliberate fallback until the active GPU backend exposes a Qt render target.
# Pure PIE command-state contract: tests every combination without GUI,
# native Player or GPU dependencies.
if(BUILD_TESTING)
    add_executable(northstarcpp_pie_command_state_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_pie_command_state_smoke.cpp")
    target_include_directories(northstarcpp_pie_command_state_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_compile_features(northstarcpp_pie_command_state_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_pie_command_state
        COMMAND "$<TARGET_FILE:northstarcpp_pie_command_state_smoke>")
endif()
# Typed Character authoring + editor-only GPU marker, without renderer startup.
if(BUILD_TESTING)
    add_executable(northstarcpp_character_components_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_character_components_smoke.cpp")
    target_include_directories(northstarcpp_character_components_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_compile_features(northstarcpp_character_components_smoke PRIVATE cxx_std_17)
    target_link_libraries(northstarcpp_character_components_smoke PRIVATE northstarcpp_math)
    add_test(NAME northstarcpp_character_components
        COMMAND "$<TARGET_FILE:northstarcpp_character_components_smoke>")
endif()
add_executable(northstarcpp_editor_qt WIN32
    modules/editor/qt_editor_main.cpp
    modules/editor/qt_editor_window.hpp
    modules/editor/qt_place_actors_panel.hpp
    modules/editor/qt_place_actors_panel.cpp
    modules/editor/qt_play_surface.hpp
    modules/editor/qt_play_surface.cpp
    modules/editor/qt_inspector_scroll.hpp
    modules/editor/qt_inspector_scroll.cpp
    modules/editor/qt_camera_studio.cpp
    modules/editor/qt_cutscene_timeline.cpp
    modules/editor/qt_editor_window.cpp
    modules/editor/qt_editor_window_paths.cpp
    modules/editor/qt_editor_window_project_thumbnail.cpp
    modules/editor/qt_editor_window_play.cpp
    modules/editor/qt_editor_window_setup.cpp
    modules/editor/qt_editor_window_outliner.cpp
    modules/editor/qt_editor_window_inspector_workspace.cpp
    modules/editor/qt_editor_window_workspace_signals.cpp
    modules/editor/qt_ui_asset_editor.hpp
    modules/editor/qt_ui_asset_editor.cpp
    modules/editor/qt_editor_window_content_docks.cpp
    modules/editor/qt_editor_theme.cpp
    modules/editor/qt_editor_window_materials.cpp
    modules/editor/qt_editor_window_smoke.cpp
    modules/editor/qt_material_authoring.hpp
    modules/editor/qt_material_instances.hpp
    modules/editor/qt_material_instance_dialog.cpp
    modules/editor/qt_material_function_smoke.cpp
    modules/editor/qt_material_gpu_preview.hpp
    modules/editor/qt_material_gpu_preview.cpp
)
set_target_properties(northstarcpp_editor_qt PROPERTIES
    AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
# PIE icons share the same external NSP/RCC as the Asset Browser.
# None of these PNGs are compiled into the Qt Editor executable.
target_include_directories(northstarcpp_editor_qt PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
target_link_libraries(northstarcpp_editor_qt PRIVATE
    Qt6::Widgets northstarcpp_content_browser_qt northstarcpp_editor_tools
    northstarcpp_collision_cooker northstarcpp_asset_catalog northstarcpp_runtime northstarcpp_crash
    northstarcpp_qt_editor_viewport)
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
    add_test(NAME northstarcpp_editor_content_browser_tabs_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>"
            --content-dock-smoke)
    set_tests_properties(northstarcpp_editor_content_browser_tabs_qt PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 85 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_qt_material_instance_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --material-instance-smoke)
    set_tests_properties(northstarcpp_qt_material_instance_authoring PROPERTIES
        TIMEOUT 45)
    # A dedicated native GPU QA host avoids launching QtEditorWindow and
    # its asynchronous Asset Browser scan, and can be rebuilt while a user
    # has the production editor EXE open.
    add_executable(northstarcpp_material_function_gpu_smoke
        "${PROJECT_SOURCE_DIR}/tests/qt_material_function_gpu_smoke_main.cpp"
        modules/editor/qt_material_function_smoke.cpp
        modules/editor/qt_material_gpu_preview.hpp
        modules/editor/qt_material_gpu_preview.cpp)
    set_target_properties(northstarcpp_material_function_gpu_smoke PROPERTIES
        AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    target_include_directories(northstarcpp_material_function_gpu_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/editor")
    target_link_libraries(northstarcpp_material_function_gpu_smoke PRIVATE
        northstarcpp_content_browser_qt northstarcpp_qt_editor_viewport
        northstarcpp_editor_tools northstarcpp_runtime Qt6::Widgets)
    add_test(NAME northstarcpp_qt_material_function_gpu
        COMMAND "$<TARGET_FILE:northstarcpp_material_function_gpu_smoke>")
    # GPU preview uses a native HWND/swapchain. Qt offscreen cannot provide
    # this surface; force the Windows QPA backend and serialize GPU tests.
    set_tests_properties(northstarcpp_qt_material_function_gpu PROPERTIES
        TIMEOUT 75 ENVIRONMENT "QT_QPA_PLATFORM=windows" RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_qt_material_graph_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --material-graph-smoke)
    set_tests_properties(northstarcpp_qt_material_graph_authoring PROPERTIES
        TIMEOUT 45)
    add_test(NAME northstarcpp_qt_editor_mouse_look
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --mouse-look-smoke)
    if(WIN32)
        set_tests_properties(northstarcpp_qt_editor_mouse_look PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 40)
    else()
        set_tests_properties(northstarcpp_qt_editor_mouse_look PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
    endif()
    add_test(NAME northstarcpp_qt_editor_native_ui
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --native-ui-smoke)
    set_tests_properties(northstarcpp_qt_editor_native_ui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 40)
    add_test(NAME northstarcpp_qt_editor_authoring
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --smoke)
    if(WIN32)
        add_test(NAME northstarcpp_qt_editor_gpu_surface
            COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --gpu-smoke)
        set_tests_properties(northstarcpp_qt_editor_gpu_surface PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 60)
        set_tests_properties(northstarcpp_qt_editor_authoring PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=windows" TIMEOUT 40)
    else()
        set_tests_properties(northstarcpp_qt_editor_authoring PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 40)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(northstarcpp_runtime_frame_scheduler_smoke
        "${PROJECT_SOURCE_DIR}/tests/runtime_frame_scheduler_smoke.cpp"
        modules/editor/window_runtime_frame_scheduler.cpp)
    target_compile_features(northstarcpp_runtime_frame_scheduler_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_runtime_frame_scheduler
        COMMAND "$<TARGET_FILE:northstarcpp_runtime_frame_scheduler_smoke>")
    add_executable(northstarcpp_win32_message_pump_smoke
        "${PROJECT_SOURCE_DIR}/tests/win32_message_pump_smoke.cpp"
        modules/editor/window_runtime_message_pump.cpp)
    target_compile_features(northstarcpp_win32_message_pump_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_win32_message_pump
        COMMAND "$<TARGET_FILE:northstarcpp_win32_message_pump_smoke>")
    set_tests_properties(northstarcpp_win32_message_pump PROPERTIES TIMEOUT 5)
    add_executable(northstarcpp_viewport_procedural_geometry_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_procedural_geometry_smoke.cpp"
        modules/viewport/viewport_procedural_geometry.cpp)
    target_link_libraries(northstarcpp_viewport_procedural_geometry_smoke
        PRIVATE northstarcpp_api northstarcpp_math)
    target_compile_features(northstarcpp_viewport_procedural_geometry_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_procedural_geometry
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_procedural_geometry_smoke>")
    add_executable(northstarcpp_viewport_frame_submission_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_frame_submission_smoke.cpp"
        modules/viewport/viewport_frame_submission.cpp)
    target_link_libraries(northstarcpp_viewport_frame_submission_smoke PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_viewport_frame_submission_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_frame_submission
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_frame_submission_smoke>")
    add_test(NAME northstarcpp_viewport_draw_modularity
        COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/viewport_draw_modularity_smoke.py")
    add_test(NAME northstarcpp_window_script_bindings_modularity
        COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tests/window_script_bindings_modularity_smoke.py")
    add_executable(northstarcpp_window_script_domain_contracts_smoke
        "${PROJECT_SOURCE_DIR}/tests/window_script_domain_contracts_smoke.cpp")
    target_link_libraries(northstarcpp_window_script_domain_contracts_smoke
        PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_window_script_domain_contracts_smoke
        PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_window_script_domain_contracts
        COMMAND "$<TARGET_FILE:northstarcpp_window_script_domain_contracts_smoke>")
    add_executable(northstarcpp_viewport_frame_builder_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_frame_builder_smoke.cpp"
        modules/viewport/viewport_frame_builder.cpp)
    target_link_libraries(northstarcpp_viewport_frame_builder_smoke PRIVATE northstarcpp_api northstarcpp_math)
    target_compile_features(northstarcpp_viewport_frame_builder_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_frame_builder
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_frame_builder_smoke>")
    add_executable(northstarcpp_viewport_frame_scratch_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_frame_scratch_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_frame_scratch_smoke PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_viewport_frame_scratch_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_frame_scratch
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_frame_scratch_smoke>")
    add_executable(northstarcpp_viewport_mesh_residency_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_mesh_residency_smoke.cpp")
    target_compile_features(northstarcpp_viewport_mesh_residency_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_mesh_residency
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_mesh_residency_smoke>")
endif()

# The Qt editor is the only editor executable; window.cpp is still compiled
# exclusively by Player until its shared entry point is split out.
add_executable(northstarcpp_player WIN32
    modules/editor/window.cpp
    modules/editor/window_script_bindings.cpp
    modules/editor/window_script_scene_bindings.cpp
    modules/editor/window_script_camera_bindings.cpp
    modules/editor/window_script_ecs_bindings.cpp
    modules/editor/window_script_input_bindings.cpp
    modules/editor/window_script_player_bindings.cpp
    modules/editor/window_script_audio_bindings.cpp
    modules/editor/window_script_bindings_host.cpp
    modules/editor/window_script_save_bindings.cpp
    modules/editor/window_scene_streaming.cpp
    modules/editor/window_runtime_options.cpp
    modules/editor/window_runtime_script_contracts.cpp
    modules/editor/window_runtime_project_files.cpp
    modules/editor/window_win32_controls.cpp
    modules/editor/window_runtime_frame_scheduler.cpp
    modules/editor/window_runtime_services.cpp
    modules/editor/window_runtime_message_pump.cpp
    modules/editor/content_browser.cpp)
target_link_libraries(northstarcpp_player PRIVATE comctl32)
foreach(app IN ITEMS northstarcpp_player)
    target_compile_definitions(${app} PRIVATE UNICODE _UNICODE
        NORTHSTARCPP_RUNTIME_VERSION="${PROJECT_VERSION}")
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
# Release-only public launchers: Qt executables and DLLs remain in
# bin/qt_tools, while users start the branded executables directly from bin/.
# The launcher imports Windows system APIs only, never Qt/V8/engine DLLs.
foreach(qt_name IN ITEMS project_manager asset_browser editor_qt camera_timeline_editor)
    set(_launcher_target "northstarcpp_${qt_name}_qt_launcher")
    add_executable(${_launcher_target} WIN32 modules/tools/qt_release_launcher.cpp)
    target_compile_features(${_launcher_target} PRIVATE cxx_std_17)
    target_include_directories(${_launcher_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_options(${_launcher_target} PRIVATE -static -municode)
    target_link_libraries(${_launcher_target} PRIVATE shell32)
    if(qt_name STREQUAL "project_manager" OR qt_name STREQUAL "camera_timeline_editor")
        target_sources(${_launcher_target} PRIVATE
            "${PROJECT_BINARY_DIR}/generated/northstarcpp_logo.rc")
    else()
        target_sources(${_launcher_target} PRIVATE
            "${PROJECT_BINARY_DIR}/generated/northstarcpp_editor_icon.rc")
    endif()
    set_target_properties(${_launcher_target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/qt_launchers"
        OUTPUT_NAME "northstarcpp_${qt_name}_launcher")
    install(TARGETS ${_launcher_target}
        RUNTIME DESTINATION tools/qt_launchers COMPONENT Tools)
endforeach()

install(TARGETS northstarcpp_runtime northstarcpp_viewport northstarcpp_projects
    northstarcpp_world_foundation
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
    if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
        add_test(NAME northstarcpp_bullet_residency
            COMMAND "$<TARGET_FILE:northstarcpp_physics_residency_smoke>"
                    "$<TARGET_FILE:northstarcpp_physics_bullet>")
        set_tests_properties(northstarcpp_bullet_residency PROPERTIES TIMEOUT 45)
    endif()
    add_test(NAME northstarcpp_jolt_residency
        COMMAND "$<TARGET_FILE:northstarcpp_physics_residency_smoke>"
                "$<TARGET_FILE:northstarcpp_physics_jolt>")
    set_tests_properties(northstarcpp_jolt_residency PROPERTIES TIMEOUT 45)

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
    add_executable(northstarcpp_player_blueprint_physics_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_blueprint_physics_smoke.cpp")
    target_link_libraries(northstarcpp_player_blueprint_physics_smoke
        PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_player_blueprint_physics_smoke PRIVATE -static)
    add_test(NAME northstarcpp_player_blueprint_physics
        COMMAND "$<TARGET_FILE:northstarcpp_player_blueprint_physics_smoke>")
    set_tests_properties(northstarcpp_player_blueprint_physics
        PROPERTIES TIMEOUT 45)
    add_executable(northstarcpp_player_blueprint_qt_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_blueprint_qt_smoke.cpp")
    target_link_libraries(northstarcpp_player_blueprint_qt_smoke
        PRIVATE Qt6::Widgets northstarcpp_editor_tools)
    target_link_options(northstarcpp_player_blueprint_qt_smoke PRIVATE -static)
    add_test(NAME northstarcpp_player_blueprint_qt
        COMMAND "$<TARGET_FILE:northstarcpp_player_blueprint_qt_smoke>")
    set_tests_properties(northstarcpp_player_blueprint_qt PROPERTIES
        TIMEOUT 35 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(northstarcpp_player_blueprint_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_blueprint_smoke.cpp")
    target_link_libraries(northstarcpp_player_blueprint_smoke
        PRIVATE northstarcpp_editor_tools)
    target_link_options(northstarcpp_player_blueprint_smoke PRIVATE -static)
    add_test(NAME northstarcpp_player_blueprint
        COMMAND "$<TARGET_FILE:northstarcpp_player_blueprint_smoke>")
    set_tests_properties(northstarcpp_player_blueprint
        PROPERTIES TIMEOUT 40)
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
    add_executable(northstarcpp_camera_dynamics_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_dynamics_smoke.cpp")
    target_include_directories(northstarcpp_camera_dynamics_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_link_libraries(northstarcpp_camera_dynamics_smoke PRIVATE
        northstarcpp_runtime)
    add_test(NAME northstarcpp_camera_dynamics
        COMMAND "$<TARGET_FILE:northstarcpp_camera_dynamics_smoke>")
    add_executable(northstarcpp_editor_camera_smoke "${PROJECT_SOURCE_DIR}/tests/editor_camera_smoke.cpp")
    target_link_libraries(northstarcpp_editor_camera_smoke PRIVATE northstarcpp_editor_tools)
    set_target_properties(northstarcpp_editor_camera_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_editor_camera_picking COMMAND northstarcpp_editor_camera_smoke)
    add_executable(northstarcpp_editor_asset_thumbnails_smoke
        "${PROJECT_SOURCE_DIR}/tests/editor_asset_thumbnails_smoke.cpp"
        modules/editor/editor_asset_material_source.cpp)
    target_link_libraries(northstarcpp_editor_asset_thumbnails_smoke PRIVATE
        northstarcpp_editor_tools northstarcpp_asset_catalog)
    set_target_properties(northstarcpp_editor_asset_thumbnails_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_editor_asset_thumbnails COMMAND northstarcpp_editor_asset_thumbnails_smoke)
    add_dependencies(northstarcpp_editor_asset_thumbnails_smoke northstarcpp_assets_directory)
    add_test(NAME northstarcpp_editor_asset_pipeline
        COMMAND "${CMAKE_COMMAND}" -E env
          "NORTHSTARCPP_ASSETS_PLUGIN=$<TARGET_FILE:northstarcpp_assets_directory>"
          "$<TARGET_FILE:northstarcpp_editor_asset_thumbnails_smoke>"
          "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
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
    add_executable(northstarcpp_scene_portal_polygon_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_portal_polygon_smoke.cpp")
    target_link_libraries(northstarcpp_scene_portal_polygon_smoke PRIVATE northstarcpp_runtime)
    target_compile_features(northstarcpp_scene_portal_polygon_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_scene_portal_polygon
        COMMAND "$<TARGET_FILE:northstarcpp_scene_portal_polygon_smoke>")
    add_executable(northstarcpp_scene_project_profile
        "${PROJECT_SOURCE_DIR}/tests/scene_project_profile.cpp")
    target_link_libraries(northstarcpp_scene_project_profile PRIVATE northstarcpp_runtime)
    target_compile_features(northstarcpp_scene_project_profile PRIVATE cxx_std_17)
    add_executable(northstarcpp_viewport_hlod_geometry_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_hlod_geometry_smoke.cpp")
    target_include_directories(northstarcpp_viewport_hlod_geometry_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_link_libraries(northstarcpp_viewport_hlod_geometry_smoke PRIVATE
        northstarcpp_api northstarcpp_math)
    target_compile_features(northstarcpp_viewport_hlod_geometry_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_hlod_geometry
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_hlod_geometry_smoke>")
    add_executable(northstarcpp_scene_hlod_baker_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_hlod_baker_smoke.cpp")
    target_link_libraries(northstarcpp_scene_hlod_baker_smoke PRIVATE northstarcpp_api northstarcpp_math)
    target_compile_features(northstarcpp_scene_hlod_baker_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_scene_hlod_baker
        COMMAND "$<TARGET_FILE:northstarcpp_scene_hlod_baker_smoke>")
    add_executable(northstarcpp_scene_cost_profiler_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_cost_profiler_smoke.cpp")
    target_link_libraries(northstarcpp_scene_cost_profiler_smoke PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_scene_cost_profiler_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_scene_cost_profiler
        COMMAND "$<TARGET_FILE:northstarcpp_scene_cost_profiler_smoke>")
    add_executable(northstarcpp_scene_gpu_residency_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_gpu_residency_smoke.cpp")
    target_link_libraries(northstarcpp_scene_gpu_residency_smoke PRIVATE northstarcpp_api)
    target_compile_features(northstarcpp_scene_gpu_residency_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_scene_gpu_residency
        COMMAND "$<TARGET_FILE:northstarcpp_scene_gpu_residency_smoke>")
    add_executable(northstarcpp_scene_world_smoke
        "${PROJECT_SOURCE_DIR}/tests/scene_world_smoke.cpp")
    target_link_libraries(northstarcpp_scene_world_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_scene_world_smoke PRIVATE -static)
    add_test(NAME northstarcpp_scene_world_v3
        COMMAND northstarcpp_scene_world_smoke
            "${PROJECT_BINARY_DIR}/scene_world_v3_smoke")
    add_dependencies(northstarcpp_editor_qt northstarcpp_player)
    add_test(NAME northstarcpp_play_in_editor_render_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --embedded-render-smoke)
    set_tests_properties(northstarcpp_play_in_editor_render_qt
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_play_in_editor_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --embedded-game-smoke)
    set_tests_properties(northstarcpp_play_in_editor_qt
        PROPERTIES TIMEOUT 35 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_inspector_layout_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --inspector-layout-smoke)
    set_tests_properties(northstarcpp_inspector_layout_qt
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_content_browser_dock_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --content-dock-smoke)
    set_tests_properties(northstarcpp_content_browser_dock_qt
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_camera_studio_qt
        COMMAND "$<TARGET_FILE:northstarcpp_editor_qt>" --camera-studio-smoke)
    set_tests_properties(northstarcpp_camera_studio_qt
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
    add_test(NAME northstarcpp_project_manager_window
        COMMAND "$<TARGET_FILE:northstarcpp_project_manager>" --smoke)
    set_tests_properties(northstarcpp_project_manager_window PROPERTIES TIMEOUT 30)
    add_executable(northstarcpp_provider_selection_smoke
        "${PROJECT_SOURCE_DIR}/tests/project_provider_runtime_smoke.cpp")
    target_include_directories(northstarcpp_provider_selection_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_provider_selection_smoke PRIVATE cxx_std_17)
    target_link_libraries(northstarcpp_provider_selection_smoke PRIVATE
        northstarcpp_runtime)
    set_target_properties(northstarcpp_provider_selection_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_provider_selection_smoke
        northstarcpp_render_d3d11 northstarcpp_render_d3d12
        northstarcpp_physics_jolt northstarcpp_assets_directory)
    add_test(NAME northstarcpp_provider_select_render_d3d11
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_RENDER_PROVIDER=d3d11
            "$<TARGET_FILE:northstarcpp_provider_selection_smoke>" engine.render d3d11)
    add_test(NAME northstarcpp_provider_select_physics_jolt
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_PHYSICS_PROVIDER=jolt
            "$<TARGET_FILE:northstarcpp_provider_selection_smoke>" engine.physics jolt)
    add_test(NAME northstarcpp_provider_select_assets
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_ASSETS_PROVIDER=directory
            "$<TARGET_FILE:northstarcpp_provider_selection_smoke>" engine.assets directory)
    add_test(NAME northstarcpp_provider_select_missing_rejected
        COMMAND "${CMAKE_COMMAND}" -E env
            NORTHSTARCPP_RENDER_PROVIDER=does_not_exist
            "$<TARGET_FILE:northstarcpp_provider_selection_smoke>"
            engine.render !unavailable)
    if(TARGET northstarcpp_render_vulkan)
        add_dependencies(northstarcpp_provider_selection_smoke northstarcpp_render_vulkan)
        add_test(NAME northstarcpp_provider_select_render_vulkan
            COMMAND "${CMAKE_COMMAND}" -E env
                NORTHSTARCPP_RENDER_PROVIDER=vulkan
                "$<TARGET_FILE:northstarcpp_provider_selection_smoke>" engine.render vulkan)
        add_test(NAME northstarcpp_provider_select_override_conflict
            COMMAND "${CMAKE_COMMAND}" -E env
                NORTHSTARCPP_RENDER_PROVIDER=d3d11
                "NORTHSTARCPP_RENDER_PLUGIN=$<TARGET_FILE:northstarcpp_render_vulkan>"
                "$<TARGET_FILE:northstarcpp_provider_selection_smoke>"
                engine.render !unavailable)
    endif()
    add_executable(northstarcpp_render_feature_contract_smoke
        "${PROJECT_SOURCE_DIR}/tests/render_feature_requirements_smoke.cpp")
    target_include_directories(northstarcpp_render_feature_contract_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_render_feature_contract_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_render_feature_contract
        COMMAND "$<TARGET_FILE:northstarcpp_render_feature_contract_smoke>")
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
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
        add_executable(northstarcpp_drawable_resources_move_smoke
            "${PROJECT_SOURCE_DIR}/tests/drawable_resources_move_smoke.cpp")
        target_link_libraries(northstarcpp_drawable_resources_move_smoke PRIVATE
            northstarcpp_runtime)
        target_link_options(northstarcpp_drawable_resources_move_smoke PRIVATE -static)
        set_target_properties(northstarcpp_drawable_resources_move_smoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_dependencies(northstarcpp_drawable_resources_move_smoke
            northstarcpp_assets_directory)
        add_test(NAME northstarcpp_drawable_resources_move
            COMMAND "$<TARGET_FILE:northstarcpp_drawable_resources_move_smoke>"
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
        set_tests_properties(northstarcpp_drawable_resources_move
            PROPERTIES TIMEOUT 45)
        add_test(NAME northstarcpp_player_asset_zones
            COMMAND northstarcpp_player --warp --check-scene
                --scene "${PROJECT_SOURCE_DIR}/tests/fixtures/static_scene.nscene"
                --shared-assets "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
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
        northstarcpp_logger_multisink northstarcpp_logger_mock northstarcpp_audio_northstar)
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
            "NORTHSTARCPP_LOGGER_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
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
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
        set_tests_properties(northstarcpp_startup_report_runtime
            PROPERTIES TIMEOUT 105)
    endif()

    # CPU frame metrics: optional diagnostics, never part of the provider ABI.
    add_executable(northstarcpp_frame_profiler_smoke
        "${PROJECT_SOURCE_DIR}/tests/frame_profiler_smoke.cpp")
    target_link_libraries(northstarcpp_frame_profiler_smoke PRIVATE northstarcpp_api)
    add_test(NAME northstarcpp_frame_profiler
        COMMAND northstarcpp_frame_profiler_smoke)
    set_tests_properties(northstarcpp_frame_profiler PROPERTIES TIMEOUT 30)
    add_executable(northstarcpp_viewport_character_self_collision_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_character_self_collision_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_character_self_collision_smoke
        PRIVATE northstarcpp_math)
    target_compile_features(northstarcpp_viewport_character_self_collision_smoke
        PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_character_self_collision
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_character_self_collision_smoke>")
    add_executable(northstarcpp_viewport_skeletal_forward_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_skeletal_forward_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_skeletal_forward_smoke
        PRIVATE northstarcpp_math)
    target_compile_features(northstarcpp_viewport_skeletal_forward_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_viewport_skeletal_forward
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_skeletal_forward_smoke>")
    add_executable(northstarcpp_weaponlab_grip_retarget_smoke
        "${PROJECT_SOURCE_DIR}/tests/weaponlab_grip_retarget_smoke.cpp")
    target_link_libraries(northstarcpp_weaponlab_grip_retarget_smoke
        PRIVATE northstarcpp_viewport)
    target_compile_features(northstarcpp_weaponlab_grip_retarget_smoke PRIVATE cxx_std_17)
    add_test(NAME northstarcpp_weaponlab_grip_retarget
        COMMAND "$<TARGET_FILE:northstarcpp_weaponlab_grip_retarget_smoke>"
            "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/sharedAssets")
    set_tests_properties(northstarcpp_weaponlab_grip_retarget PROPERTIES TIMEOUT 90)
    add_executable(northstarcpp_viewport_pistol_ads_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_pistol_ads_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_pistol_ads_smoke
        PRIVATE northstarcpp_viewport)
    add_test(NAME northstarcpp_viewport_pistol_ads
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_pistol_ads_smoke>"
            "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/sharedAssets")
    add_executable(northstarcpp_viewport_skeletal_pipeline_smoke
        "${PROJECT_SOURCE_DIR}/tests/viewport_skeletal_pipeline_smoke.cpp")
    target_link_libraries(northstarcpp_viewport_skeletal_pipeline_smoke PRIVATE northstarcpp_viewport)
    set_target_properties(northstarcpp_viewport_skeletal_pipeline_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME northstarcpp_viewport_skeletal_pipeline
        COMMAND "$<TARGET_FILE:northstarcpp_viewport_skeletal_pipeline_smoke>"
            "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/sharedAssets")
    set_tests_properties(northstarcpp_viewport_skeletal_pipeline PROPERTIES TIMEOUT 90)

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
    add_test(NAME northstarcpp_asset_single_source_architecture
        COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/asset_single_source_audit.py"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(northstarcpp_asset_single_source_architecture
        PROPERTIES TIMEOUT 25)
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
    add_dependencies(northstarcpp_input_smoke northstarcpp_input_win32 northstarcpp_input_mock northstarcpp_audio_northstar)
    add_test(NAME northstarcpp_input_win32 COMMAND northstarcpp_input_smoke default)
    add_test(NAME northstarcpp_input_provider_independent
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_INPUT_PLUGIN=$<TARGET_FILE:northstarcpp_input_mock>"
            "$<TARGET_FILE:northstarcpp_input_smoke>" mock)
    add_test(NAME northstarcpp_input_reject_incompatible
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_INPUT_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
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
        northstarcpp_ecs_flecs ${northstarcpp_physics_provider_targets})
    set_target_properties(northstarcpp_scene_runtime_lifecycle_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
        add_test(NAME northstarcpp_scene_runtime_bullet
            COMMAND "${CMAKE_COMMAND}" -E env
                "NORTHSTARCPP_PHYSICS_PROVIDER=bullet"
                "$<TARGET_FILE:northstarcpp_scene_runtime_lifecycle_smoke>"
                "${PROJECT_BINARY_DIR}/scene-runtime-bullet.nscn")
        set_tests_properties(northstarcpp_scene_runtime_bullet PROPERTIES TIMEOUT 45)
    endif()
    add_test(NAME northstarcpp_scene_runtime_jolt
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
            "$<TARGET_FILE:northstarcpp_scene_runtime_lifecycle_smoke>"
            "${PROJECT_BINARY_DIR}/scene-runtime-jolt.nscn")
    set_tests_properties(northstarcpp_scene_runtime_jolt PROPERTIES TIMEOUT 45)

    # Jolt precision physics-material regression: two surfaces within one mesh.
    add_executable(northstarcpp_physics_surface_smoke
        "${PROJECT_SOURCE_DIR}/tests/physics_surface_material_smoke.cpp")
    target_link_libraries(northstarcpp_physics_surface_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_physics_surface_smoke
        northstarcpp_physics_jolt northstarcpp_ecs_flecs)
    set_target_properties(northstarcpp_physics_surface_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_jolt_triangle_materials
        COMMAND "${CMAKE_COMMAND}" -E env "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
            "$<TARGET_FILE:northstarcpp_physics_surface_smoke>")
    set_tests_properties(northstarcpp_jolt_triangle_materials PROPERTIES TIMEOUT 30)

    add_executable(northstarcpp_physics_smoke "${PROJECT_SOURCE_DIR}/tests/physics_smoke.cpp")
    target_link_libraries(northstarcpp_physics_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_physics_smoke northstarcpp_ecs_flecs
        ${northstarcpp_physics_provider_targets})
    set_target_properties(northstarcpp_physics_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(NORTHSTARCPP_BUILD_BULLET_PROVIDER)
        add_test(NAME northstarcpp_bullet_physics COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PROVIDER=bullet"
            "NORTHSTARCPP_EXPECT_PHYSICS=Bullet"
            "$<TARGET_FILE:northstarcpp_physics_smoke>")
        add_test(NAME northstarcpp_bullet_selected COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PROVIDER=bullet"
            "NORTHSTARCPP_EXPECT_PHYSICS=Bullet"
            "$<TARGET_FILE:northstarcpp_physics_smoke>")
    endif()
    add_test(NAME northstarcpp_jolt_selected COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_PHYSICS_PROVIDER=jolt"
        "NORTHSTARCPP_EXPECT_PHYSICS=Jolt"
        "$<TARGET_FILE:northstarcpp_physics_smoke>")
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
        # Native renderer-neutral Sky model: no GPU or platform library required.
    add_executable(northstarcpp_sky_environment_smoke
        "${PROJECT_SOURCE_DIR}/tests/sky_environment_smoke.cpp")
    target_include_directories(northstarcpp_sky_environment_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules/api/include")
    target_compile_features(northstarcpp_sky_environment_smoke PRIVATE cxx_std_20)
    set_target_properties(northstarcpp_sky_environment_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_sky_environment
        COMMAND "$<TARGET_FILE:northstarcpp_sky_environment_smoke>")
    add_executable(northstarcpp_sky_cloud_temporal_smoke
        "${PROJECT_SOURCE_DIR}/tests/sky_cloud_temporal_smoke.cpp")
    target_include_directories(northstarcpp_sky_cloud_temporal_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_sky_cloud_temporal_smoke PRIVATE cxx_std_17)
    set_target_properties(northstarcpp_sky_cloud_temporal_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_sky_cloud_temporal
        COMMAND "$<TARGET_FILE:northstarcpp_sky_cloud_temporal_smoke>")
    # Provider-neutral, post-physics player action transition and event QA.
    add_executable(northstarcpp_player_action_events_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_action_events_smoke.cpp")
    target_include_directories(northstarcpp_player_action_events_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_player_action_events_smoke PRIVATE cxx_std_20)
    set_target_properties(northstarcpp_player_action_events_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_player_action_events
        COMMAND "$<TARGET_FILE:northstarcpp_player_action_events_smoke>")
    # Extended Sky QA: deterministic sun path, image deltas, cloud flow,
    # authored texture sampling and composition frame-time distribution.
    add_executable(northstarcpp_sky_dynamics_scattering_smoke
        "${PROJECT_SOURCE_DIR}/tests/sky_dynamics_scattering_smoke.cpp")
    target_include_directories(northstarcpp_sky_dynamics_scattering_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
    target_compile_features(northstarcpp_sky_dynamics_scattering_smoke PRIVATE cxx_std_20)
    set_target_properties(northstarcpp_sky_dynamics_scattering_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_sky_dynamics_scattering
        COMMAND "$<TARGET_FILE:northstarcpp_sky_dynamics_scattering_smoke>")
    set_tests_properties(northstarcpp_sky_dynamics_scattering PROPERTIES TIMEOUT 60)

    # Real GPU Sky: D3D12 framebuffer capture verifies sun, coverage,
    # cloud flow pixels. Vulkan now implements the same public Capture ABI
    # using a staging copy and nonblocking fence polling.
    if(WIN32)
        add_executable(northstarcpp_sky_gpu_dynamics_smoke
            "${PROJECT_SOURCE_DIR}/tests/sky_gpu_dynamics_smoke.cpp")
        target_include_directories(northstarcpp_sky_gpu_dynamics_smoke PRIVATE
            "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
        target_compile_features(northstarcpp_sky_gpu_dynamics_smoke PRIVATE cxx_std_20)
        target_link_libraries(northstarcpp_sky_gpu_dynamics_smoke PRIVATE user32 gdi32)
        if(MINGW)
            target_link_options(northstarcpp_sky_gpu_dynamics_smoke PRIVATE -static)
        endif()
        set_target_properties(northstarcpp_sky_gpu_dynamics_smoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        if(TARGET northstarcpp_render_d3d12)
            add_dependencies(northstarcpp_sky_gpu_dynamics_smoke northstarcpp_render_d3d12)
            add_test(NAME northstarcpp_sky_gpu_d3d12_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>")
            add_test(NAME northstarcpp_sky_gpu_d3d12_warp
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --warp)
            set_tests_properties(northstarcpp_sky_gpu_d3d12_hardware
                northstarcpp_sky_gpu_d3d12_warp PROPERTIES TIMEOUT 90)
            add_test(NAME northstarcpp_sky_gpu_uniform_d3d12_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --gpu-uniforms)
            add_test(NAME northstarcpp_sky_gpu_uniform_d3d12_warp
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_d3d12>" --gpu-uniforms --warp)
            set_tests_properties(northstarcpp_sky_gpu_uniform_d3d12_hardware
                northstarcpp_sky_gpu_uniform_d3d12_warp PROPERTIES TIMEOUT 90)
        endif()
        if(TARGET northstarcpp_render_vulkan)
            add_dependencies(northstarcpp_sky_gpu_dynamics_smoke northstarcpp_render_vulkan)
            add_test(NAME northstarcpp_sky_gpu_vulkan_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>")
            set_tests_properties(northstarcpp_sky_gpu_vulkan_hardware PROPERTIES TIMEOUT 90)
            add_test(NAME northstarcpp_sky_gpu_uniform_vulkan_hardware
                COMMAND "$<TARGET_FILE:northstarcpp_sky_gpu_dynamics_smoke>"
                    "$<TARGET_FILE:northstarcpp_render_vulkan>" --gpu-uniforms)
            set_tests_properties(northstarcpp_sky_gpu_uniform_vulkan_hardware
                PROPERTIES TIMEOUT 90)
        endif()
        if(Python3_EXECUTABLE)
            add_test(NAME northstarcpp_sky_cloud_temporal_shader_compilation
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/sky_cloud_temporal_shader_compile_smoke.py")
            set_tests_properties(northstarcpp_sky_cloud_temporal_shader_compilation
                PROPERTIES TIMEOUT 45)
            add_test(NAME northstarcpp_sky_hlsl_compilation
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/sky_hlsl_compile_smoke.py")
            set_tests_properties(northstarcpp_sky_hlsl_compilation PROPERTIES TIMEOUT 45)
        endif()
    endif()
    add_executable(northstarcpp_fps_weapon_effects_smoke "${PROJECT_SOURCE_DIR}/tests/fps_weapon_effects_smoke.cpp")
    target_link_libraries(northstarcpp_fps_weapon_effects_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_fps_weapon_effects_smoke PRIVATE -static)
    set_target_properties(northstarcpp_fps_weapon_effects_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_fps_weapon_effects
        COMMAND "$<TARGET_FILE:northstarcpp_fps_weapon_effects_smoke>")
    set_tests_properties(northstarcpp_fps_weapon_effects PROPERTIES TIMEOUT 45)
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
    add_executable(northstarcpp_camera_cutscene_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_cutscene_smoke.cpp")
    target_link_libraries(northstarcpp_camera_cutscene_smoke PRIVATE
        northstarcpp_runtime)
    add_test(NAME northstarcpp_camera_cutscene
        COMMAND "$<TARGET_FILE:northstarcpp_camera_cutscene_smoke>"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}/cinematics/northstar_intro.camera-cutscene.json")
    set_tests_properties(northstarcpp_camera_cutscene PROPERTIES TIMEOUT 35)
    # Hardware acceptance: authored camera shots and interpolated Filmic EV
    # rendered by the real Vulkan/D3D12 provider, with no software fallback.
    add_executable(northstarcpp_nightcity_npc_gpu_smoke
        "${PROJECT_SOURCE_DIR}/tests/nightcity_npc_gpu_smoke.cpp")
    target_include_directories(northstarcpp_nightcity_npc_gpu_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_link_libraries(northstarcpp_nightcity_npc_gpu_smoke PRIVATE
        northstarcpp_viewport user32 gdi32)
    set_target_properties(northstarcpp_nightcity_npc_gpu_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_nightcity_npc_gpu
        COMMAND "$<TARGET_FILE:northstarcpp_nightcity_npc_gpu_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
            "${PROJECT_BINARY_DIR}/npc_gpu_qa")
    set_tests_properties(northstarcpp_nightcity_npc_gpu PROPERTIES
        TIMEOUT 110 RUN_SERIAL TRUE)

    add_test(NAME northstarcpp_nightcity_isaac_npc_gpu
        COMMAND "$<TARGET_FILE:northstarcpp_nightcity_npc_gpu_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/KaylaNightCity"
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
            "${PROJECT_BINARY_DIR}/npc_gpu_qa"
            "isaac")
    set_tests_properties(northstarcpp_nightcity_isaac_npc_gpu PROPERTIES
        TIMEOUT 110 RUN_SERIAL TRUE)

    add_executable(northstarcpp_camera_view_target_gpu_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_view_target_gpu_smoke.cpp")
    target_include_directories(northstarcpp_camera_view_target_gpu_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_link_libraries(northstarcpp_camera_view_target_gpu_smoke PRIVATE
        northstarcpp_viewport user32 gdi32)
    set_target_properties(northstarcpp_camera_view_target_gpu_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_camera_view_target_gpu
        COMMAND "$<TARGET_FILE:northstarcpp_camera_view_target_gpu_smoke>")
    set_tests_properties(northstarcpp_camera_view_target_gpu PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
    add_executable(northstarcpp_camera_cutscene_gpu_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_cutscene_gpu_smoke.cpp")
    target_include_directories(northstarcpp_camera_cutscene_gpu_smoke PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/viewport")
    target_link_libraries(northstarcpp_camera_cutscene_gpu_smoke PRIVATE
        northstarcpp_viewport user32)
    set_target_properties(northstarcpp_camera_cutscene_gpu_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    foreach(_cinematic_backend IN ITEMS d3d12 vulkan)
        add_test(NAME northstarcpp_camera_cutscene_gpu_${_cinematic_backend}
            COMMAND "$<TARGET_FILE:northstarcpp_camera_cutscene_gpu_smoke>"
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}/cinematics/northstar_intro.camera-cutscene.json")
        add_test(NAME northstarcpp_camera_cutscene_gpu_${_cinematic_backend}_stress
            COMMAND "$<TARGET_FILE:northstarcpp_camera_cutscene_gpu_smoke>"
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}/cinematics/northstar_intro.camera-cutscene.json" "1100")
        set_tests_properties(
            northstarcpp_camera_cutscene_gpu_${_cinematic_backend}
            northstarcpp_camera_cutscene_gpu_${_cinematic_backend}_stress
            PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 RUN_SERIAL TRUE
            ENVIRONMENT "NORTHSTARCPP_RENDER_PROVIDER=${_cinematic_backend};NORTHSTARCPP_ENABLE_POSTFX_LOOK=1;NORTHSTARCPP_ENABLE_VULKAN_DEFERRED=1;NORTHSTARCPP_ENABLE_HDR=1;NORTHSTARCPP_ENABLE_FXAA=1")
    endforeach()
    add_executable(northstarcpp_camera_motion_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_motion_smoke.cpp")
    target_link_libraries(northstarcpp_camera_motion_smoke PRIVATE
        northstarcpp_runtime)
    add_test(NAME northstarcpp_camera_motion
        COMMAND "$<TARGET_FILE:northstarcpp_camera_motion_smoke>")
    set_tests_properties(northstarcpp_camera_motion
        PROPERTIES TIMEOUT 30)
    add_executable(northstarcpp_camera_director_priority_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_director_priority_smoke.cpp")
    target_link_libraries(northstarcpp_camera_director_priority_smoke PRIVATE
        northstarcpp_runtime)
    set_target_properties(northstarcpp_camera_director_priority_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_camera_director_priority
        COMMAND "$<TARGET_FILE:northstarcpp_camera_director_priority_smoke>"
            "${PROJECT_SOURCE_DIR}")
    set_tests_properties(northstarcpp_camera_director_priority
        PROPERTIES TIMEOUT 30)
    add_executable(northstarcpp_camera_physics_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_physics_smoke.cpp")
    target_link_libraries(northstarcpp_camera_physics_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_camera_physics_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_camera_physics_smoke
        northstarcpp_ecs_flecs ${northstarcpp_physics_provider_targets})
    foreach(provider IN LISTS northstarcpp_physics_provider_names)
        add_test(NAME northstarcpp_camera_physics_${provider}
            COMMAND northstarcpp_camera_physics_smoke)
        set_tests_properties(northstarcpp_camera_physics_${provider}
            PROPERTIES ENVIRONMENT "NORTHSTARCPP_PHYSICS_PROVIDER=${provider}" TIMEOUT 35)
    endforeach()
    add_executable(northstarcpp_camera_streaming_physics_smoke
        "${PROJECT_SOURCE_DIR}/tests/camera_streaming_physics_smoke.cpp")
    target_link_libraries(northstarcpp_camera_streaming_physics_smoke PRIVATE
        northstarcpp_runtime)
    set_target_properties(northstarcpp_camera_streaming_physics_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_camera_streaming_physics_smoke
        ${northstarcpp_physics_provider_targets})
    foreach(provider IN LISTS northstarcpp_physics_provider_names)
        add_test(NAME northstarcpp_camera_streaming_physics_${provider}
            COMMAND "$<TARGET_FILE:northstarcpp_camera_streaming_physics_smoke>")
        set_tests_properties(northstarcpp_camera_streaming_physics_${provider}
            PROPERTIES ENVIRONMENT "NORTHSTARCPP_PHYSICS_PROVIDER=${provider}"
            TIMEOUT 40)
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
        COMMAND northstarcpp_camera_render_smoke "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
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
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
        set_tests_properties(northstarcpp_optional_asset_recovery PROPERTIES TIMEOUT 60)
    endif()
    if(EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test/assets/textures/world_surfaces_runtime.ytd")
        add_test(NAME northstarcpp_world_surfaces_gpu
            COMMAND northstarcpp_camera_render_smoke
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test")
        set_tests_properties(northstarcpp_world_surfaces_gpu PROPERTIES TIMEOUT 90)
        add_test(NAME northstarcpp_world_surfaces_fps_scene
            COMMAND northstarcpp_camera_render_smoke
                "${NORTHSTARCPP_SHARED_ASSET_ROOT}"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStar_FPS_Test/main.nscn")
        set_tests_properties(northstarcpp_world_surfaces_fps_scene PROPERTIES
            TIMEOUT 90)
    endif()
    # Minimal first-person physics platform: renderer, sky assets, Jolt.
    add_executable(northstarcpp_exploration_smoke
        "${PROJECT_SOURCE_DIR}/tests/exploration_smoke.cpp")
    target_link_libraries(northstarcpp_exploration_smoke PRIVATE northstarcpp_runtime)
    set_target_properties(northstarcpp_exploration_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_dependencies(northstarcpp_exploration_smoke
        northstarcpp_ecs_flecs ${northstarcpp_physics_provider_targets}
        northstarcpp_assets_directory)
    if(NORTHSTARCPP_BUILD_BULLET_PROVIDER AND
       EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene")
        add_test(NAME northstarcpp_exploration_bullet
            COMMAND "${CMAKE_COMMAND}" -E env NORTHSTARCPP_PHYSICS_PROVIDER=bullet
                "$<TARGET_FILE:northstarcpp_exploration_smoke>"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene"
                "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    endif()
    if(EXISTS "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene")
        add_test(NAME northstarcpp_exploration_jolt
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_PHYSICS_PLUGIN=$<TARGET_FILE:northstarcpp_physics_jolt>"
            "$<TARGET_FILE:northstarcpp_exploration_smoke>"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/main.nscene"
            "${PROJECT_SOURCE_DIR}/Projects/NorthStarFirstPersonPlatform/assets")
    else()
        message(STATUS "Skipping legacy exploration smoke: main.nscene test fixture is absent")
    endif()
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
    # Real Windows DLL lifecycle QA: opt-in ECS reflection is registered
    # only for the selected provider and released before FreeLibrary.
    if(WIN32)
        add_library(northstarcpp_scene_component_lifecycle_fixture MODULE
            "${PROJECT_SOURCE_DIR}/tests/scene_component_lifecycle_fixture.cpp")
        target_compile_definitions(northstarcpp_scene_component_lifecycle_fixture
            PRIVATE NORTHSTARCPP_MODULE_BUILD)
        target_include_directories(northstarcpp_scene_component_lifecycle_fixture
            PRIVATE "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}")
        set_target_properties(northstarcpp_scene_component_lifecycle_fixture
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
        add_executable(northstarcpp_scene_component_lifecycle_smoke
            "${PROJECT_SOURCE_DIR}/tests/scene_component_lifecycle_smoke.cpp"
            modules/host/module_loader.cpp
            modules/host/scene_component_registry.cpp)
        target_compile_definitions(northstarcpp_scene_component_lifecycle_smoke
            PRIVATE NORTHSTARCPP_SCENE_COMPONENT_STANDALONE_TEST)
        target_include_directories(northstarcpp_scene_component_lifecycle_smoke
            PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/modules/host"
                    "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
                    "${PROJECT_SOURCE_DIR}/sdk/vendor/include")
        # Standalone: no unrelated physics/asset-runtime dependency.
        target_link_options(northstarcpp_scene_component_lifecycle_smoke
            PRIVATE -static)
        add_dependencies(northstarcpp_scene_component_lifecycle_smoke
            northstarcpp_scene_component_lifecycle_fixture)
        add_test(NAME northstarcpp_scene_component_dll_lifecycle
            COMMAND "$<TARGET_FILE:northstarcpp_scene_component_lifecycle_smoke>"
                    "$<TARGET_FILE:northstarcpp_scene_component_lifecycle_fixture>")
    endif()
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
    find_program(NORTHSTARCPP_FP_NODE_EXECUTABLE NAMES node node.exe)
    if(NORTHSTARCPP_FP_NODE_EXECUTABLE)
        add_test(NAME northstarcpp_shared_roots_oop
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/shared_root_scripts_oop_smoke.cjs")
        add_test(NAME northstarcpp_prefab_root_oop
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/prefab_root_oop_smoke.cjs")
        add_test(NAME northstarcpp_player_footsteps_events
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/player_footsteps_events_smoke.cjs")
        # scene_script_sdk is registered once with the shared Node test suite above.
        add_test(NAME northstarcpp_embedded_ui_events
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/embedded_ui_events_smoke.cjs")
        add_test(NAME northstarcpp_embedded_api_usability
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/embedded_api_usability_smoke.cjs")
        add_test(NAME northstarcpp_engine_audio_contract
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_audio_contract_smoke.cjs")
        add_test(NAME northstarcpp_engine_api_layout
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_api_layout_smoke.cjs")
        add_test(NAME northstarcpp_project_sdk_migration
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/project_sdk_migration_smoke.cjs")
        add_test(NAME northstarcpp_script_framework
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_script_framework_smoke.cjs")
        add_test(NAME northstarcpp_script_import_registry
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_script_import_contract_smoke.cjs")
        add_test(NAME northstarcpp_engine_api_oop
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_api_oop_smoke.cjs")
        add_test(NAME northstarcpp_engine_api_factory
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_api_factory_smoke.cjs")
        add_test(NAME northstarcpp_engine_api_modularity
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/engine_api_modularity_smoke.cjs")
        add_test(NAME northstarcpp_player_action_script_api
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/player_action_script_api_smoke.cjs")
        add_test(NAME northstarcpp_render_shader_api
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/render_shader_api_smoke.cjs")
        add_test(NAME northstarcpp_ui_root_script
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/ui_root_script_smoke.cjs")
        add_test(NAME northstarcpp_ui_generic_document
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/ui_generic_document_smoke.cjs")
        add_test(NAME northstarcpp_ui_declarative
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/embedded_ui_declarative_smoke.cjs")
        # Pinned compiler; CI and the editor use the same TypeScript version.
        set(NORTHSTARCPP_TS_SDK_ROOT "${PROJECT_SOURCE_DIR}/tools/ts_sdk")
        set(NORTHSTARCPP_TS_COMPILER
            "${NORTHSTARCPP_TS_SDK_ROOT}/node_modules/typescript/bin/tsc")
        if(EXISTS "${NORTHSTARCPP_TS_COMPILER}")
            add_test(NAME northstarcpp_ui_declarative_types
                COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${NORTHSTARCPP_TS_COMPILER}" --project
                    "${NORTHSTARCPP_TS_SDK_ROOT}/tsconfig.json" --pretty false)
            add_test(NAME northstarcpp_ts_language_service
                COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${NORTHSTARCPP_TS_SDK_ROOT}/ts_language_service_smoke.cjs")
        else()
            message(STATUS "TypeScript SDK tooling unavailable; install with npm ci --prefix tools/ts_sdk")
        endif()
        # UI assets are the canonical runtime UI source. No GPU/Qt needed.
        add_test(NAME northstarcpp_nui_composed_runtime
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tools/dev/nui_composed_runtime_smoke.mjs")
        set_tests_properties(northstarcpp_nui_composed_runtime PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 45)
        add_test(NAME northstarcpp_ui_asset_documents
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tools/dev/ui_asset_smoke.mjs")
        set_tests_properties(northstarcpp_ui_asset_documents PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 25)
        add_test(NAME northstarcpp_first_person_protocol_logic
            COMMAND "${NORTHSTARCPP_FP_NODE_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tests/first_person_protocol_logic_smoke.cjs")
    endif()
    # Standalone FPS menu is authored as a native UI/2 asset. This test
    # validates decoding and provider-facing typed properties without V8/GPU.
    # XML v3 assets are validated before they reach the Player/UI provider.
    file(GLOB NORTHSTARCPP_UI_XML_V3_ASSETS CONFIGURE_DEPENDS
        "${NORTHSTARCPP_SHARED_ASSET_ROOT}/ui/*.nui")
    add_executable(northstarcpp_nui_xml_v3_smoke
        "${PROJECT_SOURCE_DIR}/tests/nui_xml_v3_smoke.cpp")
    target_include_directories(northstarcpp_nui_xml_v3_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/sdk/vendor/include")
    add_test(NAME northstarcpp_nui_xml_v3
        COMMAND northstarcpp_nui_xml_v3_smoke
            ${NORTHSTARCPP_UI_XML_V3_ASSETS})
    set_tests_properties(northstarcpp_nui_xml_v3 PROPERTIES TIMEOUT 20)
    if(TARGET Qt6::Core)
        add_executable(northstarcpp_nui_xml_qt_roundtrip_smoke
            "${PROJECT_SOURCE_DIR}/tests/nui_xml_qt_roundtrip_smoke.cpp")
        target_include_directories(northstarcpp_nui_xml_qt_roundtrip_smoke PRIVATE
            "${PROJECT_SOURCE_DIR}/sdk/vendor/include")
        target_link_libraries(northstarcpp_nui_xml_qt_roundtrip_smoke PRIVATE
            Qt6::Core)
        add_test(NAME northstarcpp_nui_xml_qt_roundtrip
            COMMAND northstarcpp_nui_xml_qt_roundtrip_smoke
                ${NORTHSTARCPP_UI_XML_V3_ASSETS})
        set_tests_properties(northstarcpp_nui_xml_qt_roundtrip PROPERTIES TIMEOUT 20)
    endif()
    add_executable(northstarcpp_fps_nui_asset_smoke
        "${PROJECT_SOURCE_DIR}/tests/fps_nui_asset_smoke.cpp")
    target_include_directories(northstarcpp_fps_nui_asset_smoke PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules/api/include"
        "${PROJECT_SOURCE_DIR}/sdk/vendor/include")
    add_test(NAME northstarcpp_fps_nui_asset
        COMMAND northstarcpp_fps_nui_asset_smoke
            "${NORTHSTARCPP_SHARED_ASSET_ROOT}/ui/fps_pause.nui")
    set_tests_properties(northstarcpp_fps_nui_asset PROPERTIES TIMEOUT 15)
    add_executable(northstarcpp_player_framework_runtime_smoke
        "${PROJECT_SOURCE_DIR}/tests/player_framework_runtime_smoke.cpp")
    target_link_libraries(northstarcpp_player_framework_runtime_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_player_framework_runtime_smoke northstarcpp_physics_jolt northstarcpp_ecs_flecs)
    add_executable(northstarcpp_fps_scene_migrator
        "${PROJECT_SOURCE_DIR}/tools/migrate_fps_scene.cpp")
    target_link_libraries(northstarcpp_fps_scene_migrator PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_fps_scene_migrator northstarcpp_ecs_flecs)
    add_executable(northstarcpp_scene_smoke "${PROJECT_SOURCE_DIR}/tests/scene_smoke.cpp")
    target_link_libraries(northstarcpp_scene_smoke PRIVATE northstarcpp_runtime)
    add_dependencies(northstarcpp_scene_smoke northstarcpp_ecs_flecs)
    # The module resolver searches the executable's directory by default.
    # Keep scene tests beside the flat ECS provider, exactly like shipped apps.
    set_target_properties(northstarcpp_scene_smoke PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    add_test(NAME northstarcpp_scene COMMAND northstarcpp_scene_smoke "${PROJECT_BINARY_DIR}/scene_smoke")
    if(TARGET northstarcpp_scripting_v8_sidecar AND Python3_EXECUTABLE)
        add_test(NAME northstarcpp_ui_interaction_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/ui_interaction_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_ui_interaction_runtime
            PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_ui_world_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/ui_world_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_ui_world_runtime
            PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_bundled_scripting
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_bundled_scripting_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_bundled_scripting
            PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_footsteps_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_footsteps_runtime_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_footsteps_runtime
            PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
        add_test(NAME northstarcpp_player_scriptless_root
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tests/player_scriptless_root_smoke.py"
                "$<TARGET_FILE:northstarcpp_player>")
        set_tests_properties(northstarcpp_player_scriptless_root
            PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
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
                --shared "${NORTHSTARCPP_SHARED_ASSET_ROOT}")
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
    add_executable(northstarcpp_nac_container_smoke
        "${PROJECT_SOURCE_DIR}/tests/nac_container_smoke.cpp")
    target_link_libraries(northstarcpp_nac_container_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_nac_container_smoke PRIVATE -static)
    add_test(NAME northstarcpp_nac_container_v1
        COMMAND "$<TARGET_FILE:northstarcpp_nac_container_smoke>")

    add_executable(northstarcpp_audio_mixer_smoke
        "${PROJECT_SOURCE_DIR}/tests/northstar_audio_mixer_smoke.cpp"
        modules/audio/northstar/native_mixer.cpp)
    target_include_directories(northstarcpp_audio_mixer_smoke PRIVATE
        "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/audio/northstar")
    add_test(NAME northstarcpp_audio_mixer
        COMMAND "$<TARGET_FILE:northstarcpp_audio_mixer_smoke>")

    add_executable(northstarcpp_audio_provider_smoke
        "${PROJECT_SOURCE_DIR}/tests/audio_provider_smoke.cpp")
    target_link_libraries(northstarcpp_audio_provider_smoke PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_audio_provider_smoke PRIVATE -static)
    add_dependencies(northstarcpp_audio_provider_smoke
        northstarcpp_capability_fixture northstarcpp_audio_northstar)
    add_test(NAME northstarcpp_audio_provider_extension
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_AUDIO_PLUGIN=$<TARGET_FILE:northstarcpp_capability_fixture>"
        "$<TARGET_FILE:northstarcpp_audio_provider_smoke>" fixture)
    add_executable(northstarcpp_footsteps_nac_playback_smoke
        "${PROJECT_SOURCE_DIR}/tests/footsteps_nac_playback_smoke.cpp")
    target_link_libraries(northstarcpp_footsteps_nac_playback_smoke
        PRIVATE northstarcpp_runtime)
    target_link_options(northstarcpp_footsteps_nac_playback_smoke PRIVATE -static)
    add_dependencies(northstarcpp_footsteps_nac_playback_smoke
        northstarcpp_audio_northstar)
    add_test(NAME northstarcpp_footsteps_nac_audio_native
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_AUDIO_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
            "$<TARGET_FILE:northstarcpp_footsteps_nac_playback_smoke>"
            "${PROJECT_SOURCE_DIR}/sharedAssets/audio/footsteps/wood.nac"
            "NorthStar Audio")
    set_tests_properties(northstarcpp_footsteps_nac_audio_native PROPERTIES TIMEOUT 15)
    add_test(NAME northstarcpp_audio_provider_native
        COMMAND "${CMAKE_COMMAND}" -E env
        "NORTHSTARCPP_AUDIO_PLUGIN=$<TARGET_FILE:northstarcpp_audio_northstar>"
        "$<TARGET_FILE:northstarcpp_audio_provider_smoke>" northstar)
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
        "-DINPUT=$<TARGET_FILE:northstarcpp_input_win32>"
        "-DASSETS=$<TARGET_FILE:northstarcpp_assets_directory>"
        "-DJOLT=$<TARGET_FILE:northstarcpp_physics_jolt>"
        "-DAUDIO=$<TARGET_FILE:northstarcpp_audio_northstar>"
        "-DECS_SMOKE=$<TARGET_FILE:northstarcpp_ecs_smoke>"
        "-DEDITOR=$<TARGET_FILE:northstarcpp_editor_qt>"
        "-DPLAYER=$<TARGET_FILE:northstarcpp_player>"
        "-DAPP_SMOKE=${PROJECT_SOURCE_DIR}/tests/editor_player_smoke.cmake"
        -P "${PROJECT_SOURCE_DIR}/tests/capability_smoke.cmake")
    set_tests_properties(northstarcpp_capability_swap PROPERTIES TIMEOUT 100)
endif()
