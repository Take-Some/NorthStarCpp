# Native Player and its provider share the engine binary directory.
set(NORTHSTARCPP_V8_SIDECAR_ARCHIVE
    "${PROJECT_SOURCE_DIR}/out/v8-win64-rel-15.8.62.zip"
    CACHE FILEPATH "Cached official Windows V8 sidecar archive")

get_property(_v8_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(_v8_runtime "${PROJECT_BINARY_DIR}/scripting/v8")
if(_v8_multi_config)
    set(_v8_runtime "${PROJECT_BINARY_DIR}/$<CONFIG>/scripting/v8")
endif()

set(_v8_files
    "${_v8_runtime}/d8.exe"
    "${_v8_runtime}/icudtl.dat"
    "${_v8_runtime}/snapshot_blob.bin")
set(_v8_stage_script "${CMAKE_CURRENT_LIST_DIR}/stage_runtime.cmake")
add_custom_command(OUTPUT ${_v8_files}
    COMMAND "${CMAKE_COMMAND}"
        "-DARCHIVE=${NORTHSTARCPP_V8_SIDECAR_ARCHIVE}"
        "-DRUNTIME_DIR=${_v8_runtime}"
        -P "${_v8_stage_script}"
    DEPENDS "${_v8_stage_script}"
    COMMENT "Staging V8 15.8.62 runtime beside NorthStarCpp"
    VERBATIM)

set(_v8_worker "${CMAKE_CURRENT_LIST_DIR}/v8_worker.js")
set(_v8_worker_output "${_v8_runtime}/v8_worker.js")
add_custom_command(OUTPUT "${_v8_worker_output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_v8_runtime}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_v8_worker}" "${_v8_worker_output}"
    DEPENDS "${_v8_worker}"
    VERBATIM)

add_custom_target(northstarcpp_v8_sidecar_runtime
    DEPENDS ${_v8_files} "${_v8_worker_output}")
install(FILES ${_v8_files} "${_v8_worker_output}"
    DESTINATION bin/scripting/v8 COMPONENT Runtime)

# Upstream notices pinned to the V8 tag and its ICU DEPS revision.
install(FILES "${CMAKE_CURRENT_LIST_DIR}/licenses/V8-LICENSE"
              "${CMAKE_CURRENT_LIST_DIR}/licenses/ICU-LICENSE"
    DESTINATION licenses/v8-sidecar COMPONENT Licenses)
