# Test both discovery in ./ (next to the EXE) and a bootstrap.json override.
file(REMOVE_RECURSE "${TEST_ROOT}")
get_filename_component(name "${PROBE}" NAME)
configure_file("${PROBE}" "${TEST_ROOT}/${name}" COPYONLY)
get_filename_component(ecs_dll "${ECS}" NAME)
configure_file("${ECS}" "${TEST_ROOT}/${ecs_dll}" COPYONLY)
configure_file("${PHYSICS}" "${TEST_ROOT}/jolt.dll" COPYONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    --unset=NORTHSTARCPP_MODULES --unset=NORTHSTARCPP_ECS_PLUGIN
    --unset=NORTHSTARCPP_PHYSICS_PROVIDER
    NORTHSTARCPP_EXPECT_PHYSICS=Jolt "${TEST_ROOT}/${name}"
    RESULT_VARIABLE result TIMEOUT 35)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Jolt discovery in ./ failed: ${result}")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}/custom")
file(RENAME "${TEST_ROOT}/jolt.dll" "${TEST_ROOT}/custom/jolt.dll")
file(RENAME "${TEST_ROOT}/${ecs_dll}"
            "${TEST_ROOT}/custom/${ecs_dll}")
file(WRITE "${TEST_ROOT}/bootstrap.json" "{\"plugins\":{\"path\":\"custom\"}}\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    NORTHSTARCPP_EXPECT_PHYSICS=Jolt "${TEST_ROOT}/${name}"
    RESULT_VARIABLE result TIMEOUT 35)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Jolt discovery through bootstrap.json failed: ${result}")
endif()

file(WRITE "${TEST_ROOT}/bootstrap.json" "{\"plugins\":{\"path\":\"does-not-exist\"}}\n")
execute_process(COMMAND "${TEST_ROOT}/${name}"
    RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 15)
if(result STREQUAL "0" OR NOT error MATCHES "Plugin directory from bootstrap.json")
    message(FATAL_ERROR "Missing bootstrap plugin path was not rejected: ${result}: ${error}")
endif()
