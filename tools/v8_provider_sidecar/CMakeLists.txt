cmake_minimum_required(VERSION 3.21)
project(NorthStarV8Sidecar VERSION 15.8.62 LANGUAGES CXX)

# Deliberately a separate opt-in developer build. The release-approved
# in-process V8 provider still requires an audited embeddable SDK.
if(NOT WIN32)
    message(FATAL_ERROR "The V8 d8 development worker currently supports Windows only")
endif()
set(NORTHSTARCPP_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH
    "Path to NorthStarCpp source")
add_library(scripting_v8_sidecar SHARED
    "${NORTHSTARCPP_SOURCE_ROOT}/engine/northstarcpp/modules/scripting/v8_sidecar/v8_sidecar.cpp")
target_compile_features(scripting_v8_sidecar PRIVATE cxx_std_20)
target_compile_definitions(scripting_v8_sidecar PRIVATE NORTHSTARCPP_MODULE_BUILD)
target_include_directories(scripting_v8_sidecar PRIVATE
    "${NORTHSTARCPP_SOURCE_ROOT}/engine/northstarcpp/modules/api/include"
    "${NORTHSTARCPP_SOURCE_ROOT}/engine/northstarcpp/modules/shared")
set_target_properties(scripting_v8_sidecar PROPERTIES
    PREFIX "" OUTPUT_NAME "scripting_v8_sidecar")
if(MINGW)
    target_link_options(scripting_v8_sidecar PRIVATE
        -static -static-libgcc -static-libstdc++)
endif()
