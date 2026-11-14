# NorthStarCpp source checkout paths (not the installed package layout).
# This is the authoritative registry of first-party / SDK / vendor roots.
include_guard(GLOBAL)
if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "NorthStarPaths must be included after project()")
endif()

set(NORTHSTARCPP_ENGINE_ROOT "${PROJECT_SOURCE_DIR}/engine/northstarcpp")
set(NORTHSTARCPP_ENGINE_MODULES_DIR "${NORTHSTARCPP_ENGINE_ROOT}/modules")
set(NORTHSTARCPP_ENGINE_RESOURCES_DIR "${NORTHSTARCPP_ENGINE_ROOT}/resources")
set(NORTHSTARCPP_ENGINE_ICONS_DIR "${NORTHSTARCPP_ENGINE_RESOURCES_DIR}/icons")
set(NORTHSTARCPP_SDK_ROOT "${PROJECT_SOURCE_DIR}/sdk")
set(NORTHSTARCPP_PUBLIC_INCLUDE_DIR "${NORTHSTARCPP_ENGINE_MODULES_DIR}/api/include")
set(NORTHSTARCPP_VENDOR_ROOT "${NORTHSTARCPP_SDK_ROOT}/vendor")
set(NORTHSTARCPP_VENDOR_INCLUDE_DIR "${NORTHSTARCPP_VENDOR_ROOT}/include")
set(NORTHSTARCPP_VENDOR_SOURCE_DIR "${NORTHSTARCPP_VENDOR_ROOT}/sources")
set(NORTHSTARCPP_VENDOR_LIBRARY_DIR "${NORTHSTARCPP_VENDOR_ROOT}/libraries")

# Versioned V8 SDK vendor root, separate from NorthStarCpp public ABI headers.
# Do not automatically select the 15.7 binary for a 15.8 provider.
set(NORTHSTARCPP_VENDOR_V8_SDK_DIR "${NORTHSTARCPP_VENDOR_ROOT}/v8")

foreach(_path IN ITEMS
    NORTHSTARCPP_ENGINE_MODULES_DIR
    NORTHSTARCPP_ENGINE_RESOURCES_DIR
    NORTHSTARCPP_ENGINE_ICONS_DIR
    NORTHSTARCPP_PUBLIC_INCLUDE_DIR
    NORTHSTARCPP_VENDOR_ROOT
    NORTHSTARCPP_VENDOR_INCLUDE_DIR
    NORTHSTARCPP_VENDOR_SOURCE_DIR
    NORTHSTARCPP_VENDOR_LIBRARY_DIR)
    if(NOT IS_DIRECTORY "${${_path}}")
        message(FATAL_ERROR "NorthStarCpp source layout: ${_path} is missing: ${${_path}}")
    endif()
endforeach()
