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
if(NOT EXISTS "${ROOT}/engine/northstarcpp/modules/foundation/northstarcpp/compat/core/math_standalone.h")
    message(FATAL_ERROR "Missing relocated math compatibility headers")
endif()
file(GLOB _root_source "${ROOT}/engine/northstarcpp/*.cpp")
if(_root_source)
    message(FATAL_ERROR "Orphan sources in engine root: ${_root_source}")
endif()
message(STATUS "NorthStarCpp module source layout: verified")
