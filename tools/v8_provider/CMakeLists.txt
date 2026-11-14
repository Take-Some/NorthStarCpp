cmake_minimum_required(VERSION 3.21)
project(NorthStarV8Provider VERSION 2.0.0 LANGUAGES CXX)

# Separate UCRT64 build: do not link the MSVCRT engine against the UCRT V8 C++ SDK.
if(NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "The V8 provider must use a compatible MSYS2 UCRT64 MinGW-w64 GCC compiler")
endif()

# Verify the CRT rather than assuming that every MinGW64 compiler is UCRT.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles(
    "#include <_mingw.h>\n#ifndef _UCRT\n#error Requires UCRT64\n#endif\nint main(){return 0;}"
    NORTHSTARCPP_IS_UCRT64)
if(NOT NORTHSTARCPP_IS_UCRT64)
    message(FATAL_ERROR "Use MSYS2 UCRT64 GCC (not the engine's MINGW64/MSVCRT GCC)")
endif()

set(NORTHSTARCPP_ENGINE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "NorthStarCpp source root")
set(NORTHSTARCPP_V8_ROOT "" CACHE PATH "Root of an MSYS2 UCRT64 V8 SDK")
option(NORTHSTARCPP_V8_POINTER_COMPRESSION "Match MSYS2 V8 pointer compression" ON)
option(NORTHSTARCPP_V8_SANDBOX "Match MSYS2 V8 sandbox" ON)
if(NOT IS_DIRECTORY "${NORTHSTARCPP_ENGINE_ROOT}/include/northstarcpp"
   OR NOT EXISTS "${NORTHSTARCPP_V8_ROOT}/include/v8.h"
   OR NOT EXISTS "${NORTHSTARCPP_V8_ROOT}/include/libplatform/libplatform.h")
    message(FATAL_ERROR "Specify valid NORTHSTARCPP_ENGINE_ROOT and NORTHSTARCPP_V8_ROOT (include/v8.h)")
endif()

# All C++ V8 symbols remain inside this DLL, preserving the engine's neutral C ABI.
foreach(_part IN ITEMS v8 v8_libbase v8_libplatform)
    string(TOUPPER "${_part}" _key)
    find_library(NORTHSTARCPP_${_key}_LIBRARY NAMES ${_part}
        PATHS "${NORTHSTARCPP_V8_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    list(APPEND _v8_libraries "${NORTHSTARCPP_${_key}_LIBRARY}")
endforeach()

add_library(northstarcpp_scripting_v8 SHARED
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/scripting/v8/v8.cpp")
target_compile_features(northstarcpp_scripting_v8 PRIVATE cxx_std_20)
target_compile_definitions(northstarcpp_scripting_v8 PRIVATE NORTHSTARCPP_MODULE_BUILD)
if(NORTHSTARCPP_V8_POINTER_COMPRESSION)
    target_compile_definitions(northstarcpp_scripting_v8 PRIVATE V8_COMPRESS_POINTERS)
endif()
if(NORTHSTARCPP_V8_SANDBOX)
    target_compile_definitions(northstarcpp_scripting_v8 PRIVATE V8_ENABLE_SANDBOX)
endif()
target_include_directories(northstarcpp_scripting_v8 PRIVATE
    "${NORTHSTARCPP_ENGINE_ROOT}/include"
    "${NORTHSTARCPP_ENGINE_ROOT}/engine/northstarcpp/modules/shared")
target_include_directories(northstarcpp_scripting_v8 SYSTEM PRIVATE
    "${NORTHSTARCPP_V8_ROOT}/include")
target_link_libraries(northstarcpp_scripting_v8 PRIVATE
    ${_v8_libraries} winmm dbghelp shlwapi ws2_32 version)
target_link_options(northstarcpp_scripting_v8 PRIVATE
    -static-libgcc -static-libstdc++)
set_target_properties(northstarcpp_scripting_v8 PROPERTIES
    OUTPUT_NAME scripting_v8 PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Stage only the V8 runtime files beside the provider. The engine executable
# remains built with the original MinGW64 toolchain; its C ABI is unchanged.
file(GLOB _runtime_files
    "${NORTHSTARCPP_V8_ROOT}/bin/libv8*.dll"
    "${NORTHSTARCPP_V8_ROOT}/bin/libicu*.dll"
    "${NORTHSTARCPP_V8_ROOT}/bin/libstdc++-6.dll"
    "${NORTHSTARCPP_V8_ROOT}/bin/libgcc_s_seh-1.dll"
    "${NORTHSTARCPP_V8_ROOT}/bin/libwinpthread-1.dll"
    "${NORTHSTARCPP_V8_ROOT}/bin/zlib1.dll")
if(NOT _runtime_files)
    message(FATAL_ERROR "V8 runtime DLLs not found under NORTHSTARCPP_V8_ROOT/bin")
endif()
add_custom_command(TARGET northstarcpp_scripting_v8 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        ${_runtime_files} "$<TARGET_FILE_DIR:northstarcpp_scripting_v8>"
    COMMENT "Stage V8 and UCRT runtime DLLs for isolated engine.scripting testing"
    COMMAND_EXPAND_LISTS VERBATIM)

# Include third-party redistribution notices in the staged provider.
foreach(_license IN ITEMS v8 gcc-libs libwinpthread winpthreads zlib)
    if(NOT EXISTS "${NORTHSTARCPP_V8_ROOT}/share/licenses/${_license}")
        message(FATAL_ERROR "Missing ${_license} redistribution notices")
    endif()
    add_custom_command(TARGET northstarcpp_scripting_v8 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${NORTHSTARCPP_V8_ROOT}/share/licenses/${_license}"
            "${CMAKE_BINARY_DIR}/licenses/${_license}"
        COMMENT "Stage ${_license} license" VERBATIM)
endforeach()
file(GLOB _icu_license_paths "${NORTHSTARCPP_V8_ROOT}/share/icu/*/LICENSE")
list(LENGTH _icu_license_paths _icu_count)
if(_icu_count EQUAL 0)
    message(FATAL_ERROR "ICU dependency license is missing")
endif()
list(GET _icu_license_paths 0 _icu_license)
add_custom_command(TARGET northstarcpp_scripting_v8 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${_icu_license}" "${CMAKE_BINARY_DIR}/licenses/ICU-LICENSE"
    COMMENT "Stage ICU dependency license" VERBATIM)

# Optional real V8 integration test against the existing C ABI host executable.
include(CTest)
set(NORTHSTARCPP_HOST_SMOKE_EXE "" CACHE FILEPATH "Built northstarcpp_scripting_smoke.exe")
set(NORTHSTARCPP_HOST_ASSETS_DLL "" CACHE FILEPATH "Built assets_directory.dll")
if(BUILD_TESTING AND NORTHSTARCPP_HOST_SMOKE_EXE AND NORTHSTARCPP_HOST_ASSETS_DLL)
    if(NOT EXISTS "${NORTHSTARCPP_HOST_SMOKE_EXE}" OR
       NOT EXISTS "${NORTHSTARCPP_HOST_ASSETS_DLL}")
        message(FATAL_ERROR "Cannot find the host smoke EXE / assets_directory DLL")
    endif()
    add_custom_command(TARGET northstarcpp_scripting_v8 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NORTHSTARCPP_HOST_SMOKE_EXE}"
            "${NORTHSTARCPP_HOST_ASSETS_DLL}"
            "$<TARGET_FILE_DIR:northstarcpp_scripting_v8>"
        VERBATIM)
    add_test(NAME northstarcpp_scripting_real_v8
        COMMAND "${CMAKE_BINARY_DIR}/bin/northstarcpp_scripting_smoke.exe"
            v8 "$<TARGET_FILE:northstarcpp_scripting_v8>"
            "${NORTHSTARCPP_ENGINE_ROOT}/tests/fixtures")
    set_tests_properties(northstarcpp_scripting_real_v8 PROPERTIES
        ENVIRONMENT "NORTHSTARCPP_SCRIPTING_PLUGIN=$<TARGET_FILE:northstarcpp_scripting_v8>"
        TIMEOUT 35)
endif()
