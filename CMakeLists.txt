cmake_minimum_required(VERSION 3.21)
# NorthStarCpp GNU GCC project; staged modernization of the engine modules.
project(NorthStarCpp VERSION 0.4.0 LANGUAGES C CXX)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "Use GNU GCC/G++ (MinGW-w64 on Windows) for this port.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
include(tools/cmake/NorthStarModules.cmake)
# Compiles an unmodified original engine header; not the entire engine.
add_executable(northstarcpp_portability_smoke tests/northstarcpp_portability_smoke.cpp)
target_include_directories(northstarcpp_portability_smoke PRIVATE engine/northstarcpp/modules/foundation include)
if(BUILD_TESTING)
    add_test(NAME northstarcpp_legacy_random32 COMMAND northstarcpp_portability_smoke)
    add_test(NAME northstarcpp_engine_layout
        COMMAND "${CMAKE_COMMAND}" "-DROOT=${PROJECT_SOURCE_DIR}"
        -P "${PROJECT_SOURCE_DIR}/tests/engine_layout_smoke.cmake")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME northstarcpp_game_distribution
            COMMAND "${Python3_EXECUTABLE}" -m unittest tests.test_game_dist -v)
        set_tests_properties(northstarcpp_game_distribution PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 60)
        add_test(NAME northstarcpp_qt_distribution
            COMMAND "${Python3_EXECUTABLE}" -m unittest tests.test_qt_runtime -v)
        set_tests_properties(northstarcpp_qt_distribution PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 60)
        add_test(NAME northstarcpp_qa_reporter
            COMMAND "${Python3_EXECUTABLE}" -m unittest tests.test_qa_runner -v)
        set_tests_properties(northstarcpp_qa_reporter PROPERTIES
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" TIMEOUT 45)
        # Explicit QA report target. Build the engine first (or use
        # tools.qa_runner --build) to avoid recursive Ninja invocations.
        add_custom_target(northstarcpp_qa
            COMMAND "${Python3_EXECUTABLE}" -m tools.qa_runner
                --profile full
                --build-dir "${PROJECT_BINARY_DIR}"
                --ctest "${CMAKE_CTEST_COMMAND}"
                --cmake "${CMAKE_COMMAND}"
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMENT "Running NorthStar Engine QA and publishing JSON/JUnit/HTML reports"
            USES_TERMINAL VERBATIM)
    endif()
endif()

if(WIN32)
    if(NOT MINGW)
        message(FATAL_ERROR "The Windows targets require GCC from MinGW-w64.")
    endif()
    set(ZLIB_USE_STATIC_LIBS ON)
    get_filename_component(northstarcpp_compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(northstarcpp_toolchain_root "${northstarcpp_compiler_bin}" DIRECTORY)
    if(NOT DEFINED ZLIB_ROOT)
        set(ZLIB_ROOT "${northstarcpp_toolchain_root}")
    endif()
    find_package(ZLIB REQUIRED)
    if(NOT ZLIB_LIBRARY_RELEASE OR ZLIB_LIBRARY_RELEASE MATCHES "[.]dll[.]a$")
        message(FATAL_ERROR "Install the static MinGW-w64 zlib library")
    endif()
    set(blake3_dir "${PROJECT_SOURCE_DIR}/external/sdk/sources/blake3-1.8.7")
    add_library(northstarcpp_blake3 STATIC
        "${blake3_dir}/blake3.c" "${blake3_dir}/blake3_dispatch.c" "${blake3_dir}/blake3_portable.c")
    target_include_directories(northstarcpp_blake3 PUBLIC "$<BUILD_INTERFACE:${blake3_dir}>")
    target_compile_definitions(northstarcpp_blake3 PRIVATE BLAKE3_NO_SSE2 BLAKE3_NO_SSE41
        BLAKE3_NO_AVX2 BLAKE3_NO_AVX512 BLAKE3_USE_NEON=0)
    northstarcpp_unprefix_module_output(northstarcpp_blake3)
    # Build Windows icon resources into native applications.
    enable_language(RC)
    set(northstarcpp_math_dir engine/northstarcpp/modules/foundation/northstarcpp/compat/core/sources)
    add_library(northstarcpp_math STATIC
        "${northstarcpp_math_dir}/geometry_primitives.cpp"
        "engine/northstarcpp/modules/foundation/math_types.cpp"
        "engine/northstarcpp/modules/foundation/geometry.cpp"
        "engine/northstarcpp/modules/foundation/collision_queries.cpp"
        "${northstarcpp_math_dir}/math_aabb.cpp"
        "${northstarcpp_math_dir}/math_aabb_plane.cpp"
        "${northstarcpp_math_dir}/math_constants.cpp"
        "${northstarcpp_math_dir}/math_cuboid.cpp"
        "${northstarcpp_math_dir}/math_cylinder.cpp"
        "${northstarcpp_math_dir}/math_float3.cpp"
        "${northstarcpp_math_dir}/math_float4x4.cpp"
        "${northstarcpp_math_dir}/math_frustum.cpp"
        "${northstarcpp_math_dir}/math_obb.cpp"
        "${northstarcpp_math_dir}/math_quaternion.cpp"
        "${northstarcpp_math_dir}/math_sphere.cpp"
    )
    northstarcpp_unprefix_module_output(northstarcpp_math)
    set(northstarcpp_foundation "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules/foundation")
    target_include_directories(northstarcpp_math
        PRIVATE "${northstarcpp_foundation}"
        PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
               "$<INSTALL_INTERFACE:sdk/include>")
    target_compile_features(northstarcpp_math PUBLIC cxx_std_17)
    target_compile_definitions(northstarcpp_math PRIVATE NORTHSTARCPP_STATIC_LIBRARIES NORTHSTARCPP_STANDALONE_MATH)
    if(BUILD_TESTING)
        add_executable(northstarcpp_math_smoke tests/math_smoke.cpp)
        target_link_libraries(northstarcpp_math_smoke PRIVATE northstarcpp_math)
        target_include_directories(northstarcpp_math_smoke PRIVATE "${northstarcpp_foundation}")
        target_compile_definitions(northstarcpp_math_smoke PRIVATE NORTHSTARCPP_STATIC_LIBRARIES)
        add_test(NAME northstarcpp_math COMMAND northstarcpp_math_smoke)
    endif()

    set(FLECS_STATIC ON CACHE BOOL "" FORCE)
    set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
    set(FLECS_TESTS OFF CACHE BOOL "" FORCE)
    set(FLECS_PIC OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/flecs EXCLUDE_FROM_ALL)
    add_subdirectory(engine/northstarcpp)
    northstarcpp_finalize_modules()

    set(shader_generator_dir engine/northstarcpp/modules/foundation/northstarcpp/compat/shader_generator/sources)
    add_executable(shader_generator
        "${shader_generator_dir}/shader_generator_entry_point.cpp"
        "${shader_generator_dir}/shader_compiler.cpp"
    )

    # Embed the dedicated Shader Generator icon and Windows VERSIONINFO in the EXE.
    set(NORTHSTARCPP_SHADER_ICON_FILE "${PROJECT_SOURCE_DIR}/icons/shader_generator.ico")
    if(NOT EXISTS "${NORTHSTARCPP_SHADER_ICON_FILE}")
        message(FATAL_ERROR "Required Shader Generator icon missing: ${NORTHSTARCPP_SHADER_ICON_FILE}")
    endif()
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
    configure_file(
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources/shader_generator.rc.in"
        "${PROJECT_BINARY_DIR}/generated/shader_generator.rc" @ONLY)
    target_sources(shader_generator PRIVATE "${PROJECT_BINARY_DIR}/generated/shader_generator.rc")
    target_include_directories(shader_generator PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources")
    target_include_directories(shader_generator SYSTEM PRIVATE external/sdk/include)
    target_link_libraries(shader_generator PRIVATE d3dcompiler_47 d3dx9_43 dxguid winmm)
    target_link_options(shader_generator PRIVATE -static)

    if(BUILD_TESTING)
        add_test(NAME northstarcpp_shader_generator
            COMMAND "${CMAKE_COMMAND}"
                "-DSHADER_GENERATOR=$<TARGET_FILE:shader_generator>"
                "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shader_generator_smoke"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shader_generator_smoke.cmake"
        )
    endif()


    if(BUILD_TESTING)
        # Assert the compiled PE embeds the dedicated ICO and Windows VERSIONINFO.
        add_executable(northstarcpp_shader_generator_resources_smoke
            tests/shader_generator_resources_smoke.cpp)
        target_include_directories(northstarcpp_shader_generator_resources_smoke PRIVATE
            "${PROJECT_SOURCE_DIR}/engine/northstarcpp/resources")
        target_link_libraries(northstarcpp_shader_generator_resources_smoke PRIVATE version)
        target_link_options(northstarcpp_shader_generator_resources_smoke PRIVATE -static)
        add_test(NAME northstarcpp_shader_generator_resources
            COMMAND "$<TARGET_FILE:northstarcpp_shader_generator_resources_smoke>"
                "$<TARGET_FILE:shader_generator>" "${PROJECT_VERSION}.0")
    endif()

    include(CMakePackageConfigHelpers)
    configure_package_config_file(
        "${PROJECT_SOURCE_DIR}/tools/cmake/NorthStarCppConfig.cmake.in"
        "${PROJECT_BINARY_DIR}/NorthStarCppConfig.cmake"
        INSTALL_DESTINATION "sdk/lib/cmake/NorthStarCpp")
    write_basic_package_version_file(
        "${PROJECT_BINARY_DIR}/NorthStarCppConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
    install(EXPORT NorthStarCppTargets NAMESPACE NorthStarCpp::
        FILE NorthStarCppTargets.cmake
        DESTINATION sdk/lib/cmake/NorthStarCpp COMPONENT SDK)
    install(FILES "${PROJECT_BINARY_DIR}/NorthStarCppConfig.cmake"
                  "${PROJECT_BINARY_DIR}/NorthStarCppConfigVersion.cmake"
        DESTINATION sdk/lib/cmake/NorthStarCpp COMPONENT SDK)

    # Installable build artifacts have explicit roles; packaging never searches for random DLLs.
    install(TARGETS northstarcpp_math northstarcpp_blake3 EXPORT NorthStarCppTargets
        ARCHIVE DESTINATION sdk/lib COMPONENT SDK)
    install(FILES "${ZLIB_LIBRARY_RELEASE}" DESTINATION sdk/lib RENAME libz.a COMPONENT SDK)
    install(FILES "${blake3_dir}/LICENSE_CC0" DESTINATION licenses RENAME blake3.txt COMPONENT Licenses)
    install(FILES "${PROJECT_SOURCE_DIR}/external/sdk/sources/zlib-license.txt"
        DESTINATION licenses RENAME zlib.txt COMPONENT Licenses)
    install(TARGETS shader_generator RUNTIME DESTINATION tools COMPONENT Tools)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/northstarcpp"
        DESTINATION sdk/include COMPONENT SDK
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")
    # Remove the obsolete private header tree when upgrading an existing SDK.
    install(CODE [[
        file(REMOVE_RECURSE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/sdk/include/northstarcpp/compat")
    ]] COMPONENT SDK)

    # Runtime defaults: editable bootstrap.json beside all installed EXEs.
    # The config is optional at runtime; if deleted the native safe defaults
    # still keep the logging console hidden.
    install(FILES "${PROJECT_SOURCE_DIR}/config/bootstrap.example.json"
        DESTINATION bin RENAME bootstrap.json COMPONENT Runtime)

    if(IS_DIRECTORY "${PROJECT_SOURCE_DIR}/sharedAssets")
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/sharedAssets" DESTINATION . COMPONENT Runtime
            FILES_MATCHING PATTERN "*.ydd" PATTERN "*.ytd" PATTERN "*.ymat"
                           PATTERN "*.ysc" PATTERN "*.js" PATTERN "*.mjs" PATTERN "*.vert" PATTERN "*.frag")
    endif()

    install(FILES "${PROJECT_SOURCE_DIR}/README.md"
        DESTINATION documentation COMPONENT Documentation)
    install(FILES "${PROJECT_SOURCE_DIR}/docs/START_HERE.md"
                  "${PROJECT_SOURCE_DIR}/docs/GCC_DEVELOPMENT_SHELL.md"
                  "${PROJECT_SOURCE_DIR}/docs/DISTRIBUTION.md"
                  "${PROJECT_SOURCE_DIR}/docs/MODULES.md"
                  "${PROJECT_SOURCE_DIR}/docs/JOLT_PHYSICS.md"
                  "${PROJECT_SOURCE_DIR}/docs/PROJECT_BROWSER.md"
                  "${PROJECT_SOURCE_DIR}/docs/PROJECT_BROWSER_QT.md"
                  "${PROJECT_SOURCE_DIR}/docs/BRANDING.md"
                  "${PROJECT_SOURCE_DIR}/docs/SCENE_V3_STREAMING.md"
                  "${PROJECT_SOURCE_DIR}/docs/ENGINE_LAYOUT.md"
                  "${PROJECT_SOURCE_DIR}/docs/GAME_DISTRIBUTION.md"
                  "${PROJECT_SOURCE_DIR}/docs/CRASH_REPORTING.md"
                  "${PROJECT_SOURCE_DIR}/docs/CAMERA_SYSTEM.md"
                  "${PROJECT_SOURCE_DIR}/docs/LOGGER.md"
                  "${PROJECT_SOURCE_DIR}/docs/ENGINE_MIGRATION_STATUS.md"
                  "${PROJECT_SOURCE_DIR}/docs/FLECS_ECS.md"
                  "${PROJECT_SOURCE_DIR}/docs/PERLIN_API.md"
                  "${PROJECT_SOURCE_DIR}/docs/QT_CONTENT_BROWSER.md"
                  "${PROJECT_SOURCE_DIR}/docs/EDITOR_WORKSPACE.md"
                  "${PROJECT_SOURCE_DIR}/docs/PERLIN_INTEGRATION.md"
                  "${PROJECT_SOURCE_DIR}/docs/SCRIPTING_API.md"
                  "${PROJECT_SOURCE_DIR}/docs/SAVE_GAME.md"
                  "${PROJECT_SOURCE_DIR}/docs/PREFABS.md"
                  "${PROJECT_SOURCE_DIR}/docs/BINARY_SCENES.md"
                  "${PROJECT_SOURCE_DIR}/docs/SERIOUS_ENGINE_PARITY_2026-11-14.md"
                  "${PROJECT_SOURCE_DIR}/docs/V8_PROVIDER_UCRT64.md"
                  "${PROJECT_SOURCE_DIR}/docs/PROJECT_SCRIPTING_BRIDGE.md"
                  "${PROJECT_SOURCE_DIR}/docs/SCRIPT_FIRST_GAMEPLAY_MIGRATION.md"
                  "${PROJECT_SOURCE_DIR}/docs/SCENE_ANIMATION_RUNTIME.md"
                  "${PROJECT_SOURCE_DIR}/docs/STATIC_MESH_OBJ_PIPELINE.md"
                  "${PROJECT_SOURCE_DIR}/docs/FULL_ENGINE_READINESS_AUDIT_2026-11-14.md"
                  "${PROJECT_SOURCE_DIR}/config/bootstrap.example.json"
        DESTINATION documentation COMPONENT Documentation)
    install(FILES "${PROJECT_SOURCE_DIR}/external/flecs/LICENSE"
        DESTINATION licenses RENAME flecs.txt COMPONENT Licenses)
    install(FILES "${PROJECT_SOURCE_DIR}/external/sdk/sources/bullet-3.25/LICENSE.txt"
        DESTINATION licenses RENAME bullet.txt COMPONENT Licenses)
    install(FILES "${PROJECT_SOURCE_DIR}/external/sdk/sources/JoltPhysics/LICENSE"
        DESTINATION licenses RENAME jolt.txt COMPONENT Licenses)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_engine "${PROJECT_SOURCE_DIR}/engine/northstarcpp/modules")
    add_library(northstarcpp_perlin_runtime STATIC
        "${_engine}/host/module_loader.cpp" "${_engine}/host/perlin.cpp")
    target_include_directories(northstarcpp_perlin_runtime
        PUBLIC "${PROJECT_SOURCE_DIR}/include"
        PRIVATE "${_engine}/host" "${_engine}/shared")
    target_include_directories(northstarcpp_perlin_runtime SYSTEM PRIVATE
        "${PROJECT_SOURCE_DIR}/external/sdk/include")
    target_link_libraries(northstarcpp_perlin_runtime PRIVATE ${CMAKE_DL_LIBS})
    northstarcpp_builtin_module(northstarcpp_perlin_runtime perlin improved
        "${_engine}/perlin/improved/improved.cpp")
    northstarcpp_builtin_registry(northstarcpp_perlin_runtime)

    if(BUILD_TESTING)
        northstarcpp_test_perlin_override(northstarcpp_perlin_runtime)
    endif()
endif()
