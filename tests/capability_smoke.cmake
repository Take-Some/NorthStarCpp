# NorthStarCpp capability interchange regression: all provider DLLs share one folder.
function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
        OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 40)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Command ${ARGN} returned ${result}\n${output}\n${error}")
    endif()
endfunction()

foreach(key IN ITEMS NORTHSTARCPP_MODULES NORTHSTARCPP_ECS_PLUGIN NORTHSTARCPP_AUDIO_PLUGIN
        NORTHSTARCPP_INPUT_PLUGIN NORTHSTARCPP_RENDER_PLUGIN NORTHSTARCPP_PHYSICS_PLUGIN
        NORTHSTARCPP_PHYSICS_PROVIDER)
    unset(ENV{${key}})
endforeach()

# Normal build: EXE + every DLL in one directory, bootstrap.json absent.
run("${PROBE}" default)
run("${CMAKE_COMMAND}" "-DEDITOR=${EDITOR}" "-DPLAYER=${PLAYER}"
    "-DTEST_ROOT=${TEST_ROOT}/defaults" -P "${APP_SMOKE}")

# Customized plugin location, still a single FLAT directory for every capability.
set(host "${TEST_ROOT}/discovery")
file(REMOVE_RECURSE "${host}")
file(MAKE_DIRECTORY "${host}/providers")
# Qt Editor requires its Qt platform bundle; do not stage it as a bare EXE.
foreach(app IN ITEMS PLAYER PROBE)
    get_filename_component(name "${${app}}" NAME)
    configure_file("${${app}}" "${host}/${name}" COPYONLY)
    set(staged_${app} "${host}/${name}")
endforeach()
file(SHA256 "${EDITOR}" editor_before)
file(SHA256 "${staged_PLAYER}" player_before)
file(WRITE "${host}/bootstrap.json" "{\"plugins\":{\"path\":\"./providers\"}}\n")

set(bullet "${host}/providers/zz_bullet.dll")
set(alternative "${host}/providers/02_fixture.DLL")
configure_file("${BULLET}" "${bullet}" COPYONLY)
configure_file("${FIXTURE}" "${alternative}" COPYONLY)
file(WRITE "${host}/providers/00_broken.dll" "Invalid DLL")
configure_file("${WINMM}" "${host}/providers/zz_winmm.dll" COPYONLY)
configure_file("${ECS_FIXTURE}" "${host}/providers/ecs.dll" COPYONLY)
configure_file("${RENDER}" "${host}/providers/viewport.dll" COPYONLY)
configure_file("${INPUT}" "${host}/providers/input.dll" COPYONLY)
configure_file("${ASSETS}" "${host}/providers/assets.dll" COPYONLY)

run("${staged_PROBE}" fixture)
set(ENV{NORTHSTARCPP_ECS_PLUGIN} "${host}/providers/ecs.dll")
run("${ECS_SMOKE}")
unset(ENV{NORTHSTARCPP_ECS_PLUGIN})
run("${staged_PLAYER}" --warp --check-scene --scene
    "${TEST_ROOT}/defaults/editor created.nscn")

file(RENAME "${alternative}" "${host}/inactive.disabled")
run("${staged_PROBE}" bullet)
run("${staged_PLAYER}" --warp --check-scene --scene
    "${TEST_ROOT}/defaults/editor created.nscn")

file(REMOVE "${bullet}")
set(jolt "${host}/providers/jolt.dll")
configure_file("${JOLT}" "${jolt}" COPYONLY)
run("${staged_PROBE}" jolt)
run("${staged_PLAYER}" --warp --check-scene --scene
    "${TEST_ROOT}/defaults/editor created.nscn")

file(SHA256 "${EDITOR}" editor_after)
file(SHA256 "${staged_PLAYER}" player_after)
if(NOT editor_before STREQUAL editor_after OR NOT player_before STREQUAL player_after)
    message(FATAL_ERROR "Editor or player changed between provider configurations")
endif()

file(REMOVE "${jolt}")
run("${staged_PROBE}" reject)
