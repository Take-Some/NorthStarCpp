function(run_app)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 25
    )
    if(NOT status STREQUAL "0")
        message(FATAL_ERROR "Command ${ARGN} returned ${status}\n${output}\n${error}")
    endif()
endfunction()

file(MAKE_DIRECTORY "${TEST_ROOT}")
set(scene "${TEST_ROOT}/editor created.nscn")
file(REMOVE "${scene}")
run_app("${EDITOR}" --warp --smoke --smoke-output "${scene}")
file(READ "${scene}" header LIMIT 4 HEX)
string(TOUPPER "${header}" header)
if(NOT header STREQUAL "4E53434E" AND NOT header STREQUAL "4E454638")
    message(FATAL_ERROR "Editor did not write a binary scene container")
endif()
run_app("${PLAYER}" --warp --smoke --scene "${scene}")
