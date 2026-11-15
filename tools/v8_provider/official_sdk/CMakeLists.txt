cmake_minimum_required(VERSION 3.24)
project(NorthStarOfficialV8Provider VERSION 2.0.0 LANGUAGES CXX)

# Build with the same Windows clang-cl/MSVC C++ ABI as the official V8
# monolith. Only the stable C engine.scripting ABI crosses into MinGW.
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR
        "Official V8 monolith provider requires MSVC-compatible clang-cl on Windows")
endif()

set(NORTHSTARCPP_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH
    "NorthStarCpp repository root")
set(NORTHSTARCPP_V8_SDK_ROOT "" CACHE PATH "Official-source compiled V8 SDK")
set(NORTHSTARCPP_HOST_SMOKE_EXE "" CACHE FILEPATH
    "MinGW northstarcpp_scripting_smoke for ABI 2 validation")
set(NORTHSTARCPP_HOST_ASSETS_DLL "" CACHE FILEPATH
    "MinGW AssetManager provider DLL")

foreach(required IN ITEMS
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/api/include/northstarcpp/scripting_plugin.h"
    "${NORTHSTARCPP_V8_SDK_ROOT}/include/v8.h"
    "${NORTHSTARCPP_V8_SDK_ROOT}/include/v8-version.h"
    "${NORTHSTARCPP_V8_SDK_ROOT}/lib/v8_monolith.lib"
    "${NORTHSTARCPP_V8_SDK_ROOT}/northstar-v8-sdk.json")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "A source-built V8 SDK is required: ${required}")
    endif()
endforeach()

# Fail closed on altered/missing SDK files and incompatible GN flags.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND "${Python3_EXECUTABLE}"
        "${NORTHSTARCPP_ENGINE_ROOT}/tools/v8_provider/verify_official_sdk.py"
        --sdk "${NORTHSTARCPP_V8_SDK_ROOT}"
    RESULT_VARIABLE _sdk_verified
    OUTPUT_VARIABLE _sdk_audit
    ERROR_VARIABLE _sdk_error)
if(NOT _sdk_verified EQUAL 0)
    message(FATAL_ERROR "Unverified source-built V8 SDK: ${_sdk_error}")
endif()
message(STATUS "${_sdk_audit}")

add_library(northstarcpp_scripting_v8_native SHARED
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/scripting/v8/v8.cpp")
target_compile_features(northstarcpp_scripting_v8_native PRIVATE cxx_std_20)
target_compile_definitions(northstarcpp_scripting_v8_native PRIVATE
    NORTHSTARCPP_MODULE_BUILD V8_COMPRESS_POINTERS V8_ENABLE_SANDBOX
    V8_31BIT_SMIS_ON_64BIT_ARCH V8_INTL_SUPPORT V8_USE_SNAPSHOT)
target_include_directories(northstarcpp_scripting_v8_native PRIVATE
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/api/include"
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/shared")
target_include_directories(northstarcpp_scripting_v8_native SYSTEM PRIVATE
    "${NORTHSTARCPP_V8_SDK_ROOT}/include")
target_link_libraries(northstarcpp_scripting_v8_native PRIVATE
    "${NORTHSTARCPP_V8_SDK_ROOT}/lib/v8_monolith.lib"
    winmm dbghelp shlwapi ws2_32 version advapi32 userenv psapi)
set_target_properties(northstarcpp_scripting_v8_native PROPERTIES
    OUTPUT_NAME scripting_v8 PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_options(northstarcpp_scripting_v8_native PRIVATE /INCREMENTAL:NO)

add_custom_command(TARGET northstarcpp_scripting_v8_native POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${NORTHSTARCPP_V8_SDK_ROOT}/LICENSE"
        "${CMAKE_BINARY_DIR}/bin/V8-LICENSE"
    VERBATIM)
if(EXISTS "${NORTHSTARCPP_V8_SDK_ROOT}/bin/icudtl.dat")
    add_custom_command(TARGET northstarcpp_scripting_v8_native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NORTHSTARCPP_V8_SDK_ROOT}/bin/icudtl.dat"
            "${CMAKE_BINARY_DIR}/bin/icudtl.dat"
        VERBATIM)
endif()

include(CTest)
if(BUILD_TESTING AND NORTHSTARCPP_HOST_SMOKE_EXE AND NORTHSTARCPP_HOST_ASSETS_DLL)
    foreach(f IN ITEMS "${NORTHSTARCPP_HOST_SMOKE_EXE}" "${NORTHSTARCPP_HOST_ASSETS_DLL}")
        if(NOT EXISTS "${f}")
            message(FATAL_ERROR "Missing host ABI fixture: ${f}")
        endif()
    endforeach()
    add_custom_command(TARGET northstarcpp_scripting_v8_native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NORTHSTARCPP_HOST_SMOKE_EXE}" "${NORTHSTARCPP_HOST_ASSETS_DLL}"
            "$<TARGET_FILE_DIR:northstarcpp_scripting_v8_native>"
        VERBATIM)
    add_test(NAME northstarcpp_official_v8_abi2
        COMMAND "${CMAKE_COMMAND}" -E env
            "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_v8_native>"
            "${CMAKE_BINARY_DIR}/bin/northstarcpp_scripting_smoke.exe"
            v8 "$<TARGET_FILE:northstarcpp_scripting_v8_native>"
            "${NORTHSTARCPP_ENGINE_ROOT}/tests/fixtures")
    set_tests_properties(northstarcpp_official_v8_abi2 PROPERTIES TIMEOUT 60)
endif()
