# Single generated-output location for every NorthStarCpp target.
# Source = engine/northstarcpp/modules; build = out/build; packages = out/package.
include_guard(GLOBAL)
get_filename_component(_northstar_canonical_binary
    "${PROJECT_SOURCE_DIR}/out/build" ABSOLUTE)
get_filename_component(_northstar_actual_binary
    "${CMAKE_BINARY_DIR}" ABSOLUTE)
file(TO_CMAKE_PATH "${_northstar_canonical_binary}" _northstar_canonical_binary)
file(TO_CMAKE_PATH "${_northstar_actual_binary}" _northstar_actual_binary)
if(WIN32)
    string(TOLOWER "${_northstar_canonical_binary}" _northstar_canonical_compare)
    string(TOLOWER "${_northstar_actual_binary}" _northstar_actual_compare)
else()
    set(_northstar_canonical_compare "${_northstar_canonical_binary}")
    set(_northstar_actual_compare "${_northstar_actual_binary}")
endif()
if("${_northstar_actual_compare}" STREQUAL "${PROJECT_SOURCE_DIR}")
    message(FATAL_ERROR "In-source builds are forbidden")
endif()
option(NORTHSTARCPP_ALLOW_NONCANONICAL_BUILD
    "Allow a noncanonical CMake binary directory for exceptional external CI builds" OFF)
if(NOT NORTHSTARCPP_ALLOW_NONCANONICAL_BUILD AND
   NOT _northstar_actual_compare STREQUAL _northstar_canonical_compare)
    message(FATAL_ERROR
        "NorthStarCpp has exactly one build directory: ${_northstar_canonical_binary}\n"
        "Given: ${CMAKE_BINARY_DIR}\n"
        "Use: cmake --preset release (or debug)\n"
        "Do not generate extra gcc-*, project-manager-qt, or distribution CMake trees.")
endif()
# Third-party targets may keep their own OBJECT output below out/build,
# but no generated file is placed in the engine source tree.
set(NORTHSTARCPP_BUILD_OUTPUT_ROOT "${CMAKE_BINARY_DIR}")
set(NORTHSTARCPP_PACKAGE_ROOT "${PROJECT_SOURCE_DIR}/out/package")
set(NORTHSTARCPP_QA_OUTPUT_ROOT "${PROJECT_SOURCE_DIR}/out/qa")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${NORTHSTARCPP_BUILD_OUTPUT_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${NORTHSTARCPP_BUILD_OUTPUT_ROOT}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${NORTHSTARCPP_BUILD_OUTPUT_ROOT}/lib")
# Do not ever install into MinGW/Program Files when no explicit prefix is set.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${NORTHSTARCPP_PACKAGE_ROOT}/install"
        CACHE PATH "NorthStarCpp staged install prefix" FORCE)
endif()
message(STATUS "NorthStarCpp single build root: ${NORTHSTARCPP_BUILD_OUTPUT_ROOT}")
message(STATUS "NorthStarCpp package root: ${NORTHSTARCPP_PACKAGE_ROOT}")
