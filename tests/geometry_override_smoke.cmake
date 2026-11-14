function(check_geometry mode)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        --unset=NORTHSTARCPP_GEOMETRY_PLUGIN
         "${program}" "${mode}"
        RESULT_VARIABLE result)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Geometry ${mode} probe exited with ${result}")
    endif()
endfunction()
file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}/plugins")
get_filename_component(executable_name "${PROBE}" NAME)
set(program "${ROOT}/${executable_name}")
file(COPY "${PROBE}" DESTINATION "${ROOT}")
get_filename_component(suffix "${EXTERNAL}" EXT)
file(WRITE "${ROOT}/bootstrap.json" "{\"plugins\":{\"path\":\"plugins\"}}\n")
check_geometry(builtin)
configure_file("${INVALID}" "${ROOT}/plugins/00_bad${suffix}" COPYONLY)
check_geometry(builtin)
set(_args "NORTHSTARCPP_GEOMETRY_PLUGIN=${INVALID}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "${_args}" "${program}" reject RESULT_VARIABLE result)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Geometry incompatible override was not rejected")
endif()
configure_file("${EXTERNAL}" "${ROOT}/plugins/01_good${suffix}" COPYONLY)
check_geometry(external)
file(REMOVE "${ROOT}/plugins/01_good${suffix}")
file(WRITE "${ROOT}/bootstrap.json"
    "{\"plugins\":{\"path\":\"plugins\",\"overrides\":{\"engine.geometry\":\"configured${suffix}\"}}}\n")
configure_file("${EXTERNAL}" "${ROOT}/configured${suffix}" COPYONLY)
check_geometry(external)
# Explicitly selected compatible DLL overrides bootstrap; env is priority 1.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "NORTHSTARCPP_GEOMETRY_PLUGIN=${EXTERNAL}"
    "${program}" external RESULT_VARIABLE explicit_result)
if(NOT explicit_result STREQUAL "0")
    message(FATAL_ERROR "Explicit author DLL override did not win")
endif()
check_geometry(external)
file(REMOVE "${ROOT}/bootstrap.json" "${ROOT}/configured${suffix}")
check_geometry(builtin)
