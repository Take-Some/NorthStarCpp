# Verify active NorthStarCpp sources do not drift out of the module catalog.
foreach(_directory IN ITEMS
    host editor viewport shared ecs/flecs physics/bullet physics/jolt
    audio/winmm render/d3d11 input/win32 assets/directory)
    if(NOT IS_DIRECTORY "${ROOT}/engine/northstarcpp/modules/${_directory}")
        message(FATAL_ERROR "Missing engine module directory: ${_directory}")
    endif()
endforeach()
foreach(_file IN ITEMS
    host/scene.cpp host/physics.cpp host/ecs_world.cpp host/audio.cpp
    host/input.cpp host/module_loader.cpp host/module_loader.hpp
    editor/window.cpp viewport/viewport.cpp viewport/viewport.hpp
    shared/module_provider.hpp
    ecs/flecs/flecs.cpp
    physics/bullet/bullet.cpp physics/jolt/jolt.cpp
    audio/winmm/winmm.cpp render/d3d11/d3d11.cpp
    input/win32/win32_xinput.cpp assets/directory/directory.cpp host/assets.cpp)
    if(NOT EXISTS "${ROOT}/engine/northstarcpp/modules/${_file}")
        message(FATAL_ERROR "Missing registered engine source: ${_file}")
    endif()
endforeach()
foreach(_name IN ITEMS core apps renderer plugins compat)
    if(EXISTS "${ROOT}/engine/northstarcpp/${_name}")
        message(FATAL_ERROR "Legacy engine directory has reappeared: ${_name}")
    endif()
endforeach()
foreach(_legacy IN ITEMS
    "NorthStarCpp.Source.sln" "NorthStarCpp.Source.suo")
    if(EXISTS "${ROOT}/engine/${_legacy}")
        message(FATAL_ERROR "Legacy IDE file in engine root: ${_legacy}")
    endif()
endforeach()
if(NOT EXISTS "${ROOT}/engine/northstarcpp/modules/core/northstarcpp/core/math_standalone.h")
    message(FATAL_ERROR "Missing relocated math compatibility headers")
endif()
file(GLOB _root_source "${ROOT}/engine/northstarcpp/*.cpp")
if(_root_source)
    message(FATAL_ERROR "Orphan sources in engine root: ${_root_source}")
endif()

# Public SDK and third-party ownership must never drift back to ambiguous roots.
foreach(_required IN ITEMS
    "engine/northstarcpp/modules/api/include/northstarcpp/module.h"
    "engine/northstarcpp/modules/api/include/northstarcpp/scene.hpp"
    "sdk/vendor/flecs/CMakeLists.txt"
    "sdk/vendor/sources/JoltPhysics/Build/CMakeLists.txt"
    "sdk/vendor/sources/bullet-3.25"
    "sdk/vendor/sources/blake3-1.8.7"
    "sdk/vendor/bcdec"
    "engine/northstarcpp/resources/icons/logoStar.ico"
    "engine/northstarcpp/resources/icons/assetBrowser/folder.png"
    "architecture/module_catalog.json"
    "tools/cmake/NorthStarPaths.cmake")
    if(NOT EXISTS "${ROOT}/${_required}")
        message(FATAL_ERROR "Missing declared architecture resource: ${_required}")
    endif()
endforeach()
foreach(_obsolete IN ITEMS "include" "external" "icons")
    if(EXISTS "${ROOT}/${_obsolete}")
        message(FATAL_ERROR "Ambiguous legacy root must be absent: ${_obsolete}")
    endif()
endforeach()

if(EXISTS "${ROOT}/sdk/include" OR
   EXISTS "${ROOT}/engine/northstarcpp/modules/foundation")
    message(FATAL_ERROR "First-party headers and sources must live in engine/northstarcpp/modules")
endif()
if(NOT EXISTS "${ROOT}/engine/northstarcpp/modules/api/include/northstarcpp/module.h")
    message(FATAL_ERROR "First-party API source is missing from modules/api")
endif()
message(STATUS "NorthStarCpp source / SDK layout: verified")

# Direct original foundation runtime integration is mandatory.
# Every source must remain available even as platform-specific translation
# units are migrated into their native engine subsystem targets.
set(_foundation_root "${ROOT}/engine/northstarcpp/modules/core/northstarcpp")
if(NOT IS_DIRECTORY "${_foundation_root}")
    message(FATAL_ERROR "Original foundation source tree missing")
endif()
file(GLOB_RECURSE _foundation_sources LIST_DIRECTORIES FALSE
    "${_foundation_root}/*.cpp")
list(LENGTH _foundation_sources _foundation_count)
if(_foundation_count LESS 1100)
    message(FATAL_ERROR "Original foundation sources were discarded: ${_foundation_count} cpp")
endif()
file(READ "${ROOT}/CMakeLists.txt" _foundation_cmake)
file(READ "${ROOT}/engine/northstarcpp/CMakeLists.txt" _runtime_cmake)
foreach(_compiled IN ITEMS
    "geometry_primitives.cpp" "geometry_utils.cpp"
    "strings_functions.cpp" "buffer_string.cpp"
    "fs_path.cpp" "fs_path_win.cpp")
    string(FIND "${_foundation_cmake}" "/${_compiled}\"" _compiled_offset)
    if(_compiled_offset EQUAL -1)
        message(FATAL_ERROR "Original source is no longer compiled: ${_compiled}")
    endif()
endforeach()
foreach(_required IN ITEMS
    "add_library(northstarcpp_core STATIC"
    "target_link_libraries(northstarcpp_core PUBLIC northstarcpp_math)"
    "DESTINATION sdk/include/northstarcpp"
    "DESTINATION sdk/src/northstarcpp")
    string(FIND "${_foundation_cmake}" "${_required}" _required_offset)
    if(_required_offset EQUAL -1)
        message(FATAL_ERROR "Original foundation integration missing: ${_required}")
    endif()
endforeach()
string(FIND "${_runtime_cmake}" "northstarcpp_core" _runtime_dependency)
if(_runtime_dependency EQUAL -1)
    message(FATAL_ERROR "Original foundation is not linked into engine runtime")
endif()
