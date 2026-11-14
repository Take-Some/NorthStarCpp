# NorthStarCpp module registry for packaging and configured capabilities.
include_guard(GLOBAL)

# Compile provider sources into a host archive under a unique query symbol.
# Call from the directory that owns the runtime target.
function(northstarcpp_builtin_module runtime capability provider source)
    if(NOT capability MATCHES "^[a-z][a-z0-9_]*$" OR NOT provider MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "Invalid builtin module: ${capability}.${provider}")
    endif()
    get_filename_component(_source "${source}" ABSOLUTE)
    if(NOT EXISTS "${_source}")
        message(FATAL_ERROR "Builtin module source missing: ${_source}")
    endif()
    set(_id "${capability}_${provider}")
    set(_query "northstar_builtin_${_id}_query")
    get_target_property(_builtins ${runtime} NORTHSTARCPP_BUILTINS)
    if("${capability}|${provider}|${_query}" IN_LIST _builtins)
        message(FATAL_ERROR "Builtin module registered twice: ${capability}.${provider}")
    endif()
    set(_wrapper "${CMAKE_CURRENT_BINARY_DIR}/builtin_${runtime}_${_id}.cpp")
    file(GENERATE OUTPUT "${_wrapper}" CONTENT
        "#define northstar_module_query ${_query}\n#include \"${_source}\"\n")
    target_sources(${runtime} PRIVATE "${_wrapper}")
    set_property(TARGET ${runtime} APPEND PROPERTY NORTHSTARCPP_BUILTINS
        "${capability}|${provider}|${_query}")
endfunction()

function(northstarcpp_builtin_registry runtime)
    get_target_property(_builtins ${runtime} NORTHSTARCPP_BUILTINS)
    set(_declarations "")
    set(_entries "")
    if(_builtins)
        foreach(_builtin IN LISTS _builtins)
            string(REPLACE "|" ";" _fields "${_builtin}")
            list(GET _fields 0 _capability)
            list(GET _fields 2 _query)
            string(APPEND _declarations "extern \"C\" NorthStarModuleQueryFunction ${_query};\n")
            string(APPEND _entries "    {\"engine.${_capability}\", &${_query}},\n")
        endforeach()
    endif()
    set(_file "${CMAKE_CURRENT_BINARY_DIR}/builtin_${runtime}_registry.cpp")
    file(GENERATE OUTPUT "${_file}" CONTENT
        "#include \"module_loader.hpp\"\n${_declarations}namespace northstarcpp {\nBuiltinModule const builtin_modules[] = {\n${_entries}    {nullptr, nullptr}\n};\n}\n")
    target_sources(${runtime} PRIVATE "${_file}")
    if(_builtins AND WIN32)
        set_property(GLOBAL PROPERTY NORTHSTARCPP_BUILTINS "${_builtins}")
    endif()
endfunction()

# Stable CMake target names with short, distributable library filenames.
# Applies to runtime provider DLLs, static modules and test provider DLLs.
function(northstarcpp_test_perlin_override runtime)
    foreach(_variant IN ITEMS override invalid)
        add_library(northstarcpp_perlin_${_variant} MODULE
            "${PROJECT_SOURCE_DIR}/tests/perlin_override_provider.cpp")
        target_compile_definitions(northstarcpp_perlin_${_variant} PRIVATE NORTHSTARCPP_MODULE_BUILD)
        target_include_directories(northstarcpp_perlin_${_variant} PRIVATE "${PROJECT_SOURCE_DIR}/include")
        set_target_properties(northstarcpp_perlin_${_variant} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules"
            LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test_modules")
        if(WIN32)
            northstarcpp_unprefix_module_output(northstarcpp_perlin_${_variant})
            set_target_properties(northstarcpp_perlin_${_variant} PROPERTIES PREFIX "")
            target_link_options(northstarcpp_perlin_${_variant} PRIVATE -static)
        endif()
    endforeach()
    target_compile_definitions(northstarcpp_perlin_invalid PRIVATE NORTHSTARCPP_INVALID_PERLIN_FIXTURE)
    add_executable(northstarcpp_perlin_override_smoke
        "${PROJECT_SOURCE_DIR}/tests/perlin_override_smoke.cpp")
    target_link_libraries(northstarcpp_perlin_override_smoke PRIVATE ${runtime})
    if(WIN32)
        target_link_options(northstarcpp_perlin_override_smoke PRIVATE -static)
    endif()
    add_dependencies(northstarcpp_perlin_override_smoke northstarcpp_perlin_override northstarcpp_perlin_invalid)
    add_test(NAME northstarcpp_perlin_provider_precedence COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${PROJECT_BINARY_DIR}/perlin_provider_precedence"
        "-DPROBE=$<TARGET_FILE:northstarcpp_perlin_override_smoke>"
        "-DEXTERNAL=$<TARGET_FILE:northstarcpp_perlin_override>"
        "-DINVALID=$<TARGET_FILE:northstarcpp_perlin_invalid>"
        -P "${PROJECT_SOURCE_DIR}/tests/perlin_override_smoke.cmake")
    set_tests_properties(northstarcpp_perlin_provider_precedence PROPERTIES TIMEOUT 30)
endfunction()

function(northstarcpp_unprefix_module_output target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Unknown NorthStarCpp module target: ${target}")
    endif()
    get_target_property(_kind "${target}" TYPE)
    if(NOT _kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)$")
        message(FATAL_ERROR "Module filename policy requires a library: ${target}")
    endif()
    if(NOT target MATCHES "^northstarcpp_[a-z0-9_]+$")
        message(FATAL_ERROR "Module target does not follow northstarcpp_* naming: ${target}")
    endif()
    string(REGEX REPLACE "^northstarcpp_" "" _short "${target}")
    if(NOT _short MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "Invalid short module output filename: ${_short}")
    endif()
    set_target_properties("${target}" PROPERTIES OUTPUT_NAME "${_short}")
endfunction()

function(northstarcpp_register_plugin)
    cmake_parse_arguments(NS "DEFAULT" "TARGET;CAPABILITY;PROVIDER" "" ${ARGN})
    if(NS_UNPARSED_ARGUMENTS OR NS_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR "Invalid NorthStarCpp module registration: ${NS_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT NS_TARGET OR NOT TARGET "${NS_TARGET}")
        message(FATAL_ERROR "Registered module target does not exist: ${NS_TARGET}")
    endif()
    if(NOT NS_CAPABILITY MATCHES "^[a-z][a-z0-9_]*$"
       OR NOT NS_PROVIDER MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR "Module capability/provider must be lowercase identifiers")
    endif()
    get_target_property(_type "${NS_TARGET}" TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY" AND NOT _type STREQUAL "MODULE_LIBRARY")
        message(FATAL_ERROR "Module ${NS_TARGET} must be a SHARED or MODULE library")
    endif()

    get_property(_existing GLOBAL PROPERTY NORTHSTARCPP_MODULE_REGISTRY)
    foreach(_item IN LISTS _existing)
        string(REPLACE "|" ";" _fields "${_item}")
        list(GET _fields 2 _prior_target)
        if(_prior_target STREQUAL NS_TARGET)
            message(FATAL_ERROR "Module registered twice: ${NS_TARGET}")
        endif()
        list(GET _fields 0 _prior_capability)
        list(GET _fields 1 _prior_provider)
        if(_prior_capability STREQUAL NS_CAPABILITY AND _prior_provider STREQUAL NS_PROVIDER)
            message(FATAL_ERROR "Duplicate capability/provider output: ${NS_CAPABILITY}_${NS_PROVIDER}")
        endif()
    endforeach()

    northstarcpp_unprefix_module_output("${NS_TARGET}")
    set_target_properties("${NS_TARGET}" PROPERTIES
        PREFIX ""
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/sdk/lib")
    set_property(TARGET "${NS_TARGET}" PROPERTY NORTHSTARCPP_MODULE_CAPABILITY "${NS_CAPABILITY}")
    set_property(TARGET "${NS_TARGET}" PROPERTY NORTHSTARCPP_MODULE_PROVIDER "${NS_PROVIDER}")
    set(_default "no")
    if(NS_DEFAULT AND NOT NS_CAPABILITY STREQUAL "physics")
        set(_default "yes")
        get_property(_defaults GLOBAL PROPERTY NORTHSTARCPP_MODULE_DEFAULTS)
        if(NS_CAPABILITY IN_LIST _defaults)
            message(FATAL_ERROR "Default provider already defined for capability ${NS_CAPABILITY}")
        endif()
        set_property(GLOBAL APPEND PROPERTY NORTHSTARCPP_MODULE_DEFAULTS "${NS_CAPABILITY}")
    endif()
    set_property(GLOBAL APPEND PROPERTY NORTHSTARCPP_MODULE_REGISTRY
        "${NS_CAPABILITY}|${NS_PROVIDER}|${NS_TARGET}|${_default}")
    install(TARGETS "${NS_TARGET}"
        RUNTIME DESTINATION "bin" COMPONENT Runtime
        LIBRARY DESTINATION "bin" COMPONENT Runtime)
endfunction()

function(northstarcpp_finalize_modules)
    get_property(_modules GLOBAL PROPERTY NORTHSTARCPP_MODULE_REGISTRY)
    if(NOT _modules)
        message(FATAL_ERROR "No NorthStarCpp modules registered")
    endif()
    get_property(_builtins GLOBAL PROPERTY NORTHSTARCPP_BUILTINS)
    set(_json "{\n  \"schema\": 1,\n  \"engine\": \"NorthStarCpp\",\n")
    string(APPEND _json "  \"engine_version\": \"${PROJECT_VERSION}\",\n")
    string(APPEND _json "  \"host_abi\": 1,\n  \"plugins\": [\n")
    set(_first TRUE)
    foreach(_entry IN LISTS _modules)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _capability)
        list(GET _parts 1 _provider)
        list(GET _parts 2 _target)
        list(GET _parts 3 _default)
        set(_relative "$<TARGET_FILE_NAME:${_target}>")
        if(NOT _first)
            string(APPEND _json ",\n")
        endif()
        set(_first FALSE)
        string(APPEND _json
            "    {\"id\":\"${_capability}.${_provider}\","
            "\"capability\":\"engine.${_capability}\","
            "\"provider\":\"${_provider}\","
            "\"host_abi\":1,"
            "\"default\":")
        if(_default STREQUAL "yes")
            string(APPEND _json "true")
        else()
            string(APPEND _json "false")
        endif()
        string(APPEND _json ",\"kind\":\"external\",\"path\":\"bin/${_relative}\"}")
    endforeach()
    get_property(_defaults GLOBAL PROPERTY NORTHSTARCPP_MODULE_DEFAULTS)
    foreach(_entry IN LISTS _builtins)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _capability)
        list(GET _parts 1 _provider)
        set(_default false)
        if(NOT _capability IN_LIST _defaults)
            set(_default true)
            list(APPEND _defaults "${_capability}")
        endif()
        string(APPEND _json ",\n"
            "    {\"id\":\"${_capability}.${_provider}.builtin\","
            "\"capability\":\"engine.${_capability}\",\"provider\":\"${_provider}\","
            "\"host_abi\":1,\"default\":${_default},\"kind\":\"builtin\"}")
    endforeach()
    string(APPEND _json "\n  ]\n}\n")
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/northstarcpp.plugins.json" CONTENT "${_json}")
    install(FILES "${PROJECT_BINARY_DIR}/northstarcpp.plugins.json"
        DESTINATION . COMPONENT Runtime)
endfunction()
