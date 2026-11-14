if(NOT DEFINED SHADER_GENERATOR OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "Expected SHADER_GENERATOR and TEST_ROOT")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/one" "${TEST_ROOT}/two")

function(run_generator expected expected_output)
    execute_process(
        COMMAND "${SHADER_GENERATOR}" ${ARGN}
        WORKING_DIRECTORY "${TEST_ROOT}"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        TIMEOUT 30
    )
    if(NOT status STREQUAL "${expected}" OR NOT "${output}${error}" MATCHES "${expected_output}")
        message(FATAL_ERROR "Command ${ARGN} returned ${status}, expected ${expected} and ${expected_output}\n${output}\n${error}")
    endif()
endfunction()

run_generator(0 "Usage:" --help)
run_generator(2 "Usage:")
run_generator(1 "Shader path not found" missing.vs -r4)
run_generator(1 "No shaders found" . -r4)

file(WRITE "${TEST_ROOT}/shared.hlsli" [=[
#ifdef USE_MSAA
#define SHIFT 0.5
#else
#define SHIFT 0.0
#endif
]=])
set(source [=[
#include "shared.hlsli"
float4 main(float4 position : POSITION) : SV_Position { return position + SHIFT; }
]=])
file(WRITE "${TEST_ROOT}/one/basic.vs" "${source}")
file(WRITE "${TEST_ROOT}/two/basic.vs" "${source}")
run_generator(0 "shared[.]hlsli" . -r4 -verbose -mp)

foreach(folder IN ITEMS one two)
    file(GLOB variants LIST_DIRECTORIES FALSE "${TEST_ROOT}/objects/r4/${folder}/basic.vs/*")
    list(LENGTH variants count)
    if(NOT count EQUAL 2)
        message(FATAL_ERROR "Expected two variants in ${folder}, found ${count}")
    endif()
    foreach(variant IN LISTS variants)
        file(READ "${variant}" magic OFFSET 4 LIMIT 4 HEX)
        if(NOT magic STREQUAL "44584243")
            message(FATAL_ERROR "Invalid bytecode header in ${variant}")
        endif()
    endforeach()
endforeach()
run_generator(0 "skipped!" -c . -r4)

file(WRITE "${TEST_ROOT}/single.ps" "float4 main() : SV_Target { return float4(1, 0, 0, 1); }\n")
run_generator(0 "OK!" single.ps -r4)

file(WRITE "${TEST_ROOT}/bad.vs" "not valid HLSL;\n")
run_generator(1 "failed!" bad.vs -r4 -mp)

file(WRITE "${TEST_ROOT}/missing_include.vs" "#include \"missing.hlsli\"\n")
run_generator(1 "missing[.]hlsli" missing_include.vs -r4)

file(WRITE "${TEST_ROOT}/optional_include.vs" [=[
#ifdef OPTIONAL_INCLUDE
#include "not_installed.hlsli"
#endif
float4 main(float4 position : POSITION) : SV_Position { return position; }
]=])
run_generator(0 "OK!" optional_include.vs -r4)

foreach(profile IN ITEMS r1 r2)
    file(WRITE "${TEST_ROOT}/${profile}.ps" "float4 main() : COLOR0 { return float4(1, 0, 0, 1); }\n")
    run_generator(0 "OK!" "${profile}.ps" "-${profile}")
endforeach()
run_generator(0 "OK!" single.ps -r3)
file(WRITE "${TEST_ROOT}/reflection.ps"
    "cbuffer Data { float4 color; }; float4 main() : SV_Target { return color; }\n")
run_generator(0 "float color" reflection.ps -r4)
