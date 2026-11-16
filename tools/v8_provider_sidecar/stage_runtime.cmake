# Build-time staging of the pinned developer runtime.
cmake_minimum_required(VERSION 3.21)
set(_version 15.8.62)
set(_sha256 9924fe95ae8a05b35f728da8258bd1d72a8810edc0179d65fd3018c7845489e3)

# Offline option for a trusted V8 runtime already present on the machine.
# The normal path still requires the official archive and pinned SHA256.
if(DEFINED ENV{NORTHSTARCPP_V8_RUNTIME_DIR} AND
   NOT "$ENV{NORTHSTARCPP_V8_RUNTIME_DIR}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{NORTHSTARCPP_V8_RUNTIME_DIR}" _local_runtime)
    foreach(_file IN ITEMS d8.exe icudtl.dat snapshot_blob.bin)
        if(NOT EXISTS "${_local_runtime}/${_file}")
            message(FATAL_ERROR "Incomplete offline V8 runtime: ${_local_runtime}/${_file}")
        endif()
    endforeach()
    execute_process(COMMAND "${_local_runtime}/d8.exe" --version
        RESULT_VARIABLE _v8_rc OUTPUT_VARIABLE _v8_version
        ERROR_VARIABLE _v8_stderr TIMEOUT 10)
    if(NOT _v8_rc EQUAL 0 OR
       NOT _v8_version MATCHES "V8 version 15\\.8\\.62")
        message(FATAL_ERROR "Offline V8 runtime version mismatch: ${_v8_version} ${_v8_stderr}")
    endif()
    file(MAKE_DIRECTORY "${RUNTIME_DIR}")
    foreach(_file IN ITEMS d8.exe icudtl.dat snapshot_blob.bin)
        file(COPY_FILE "${_local_runtime}/${_file}"
            "${RUNTIME_DIR}/${_file}" ONLY_IF_DIFFERENT)
    endforeach()
    message(STATUS "Staged trusted offline V8 15.8.62 runtime")
    return()
endif()

if(NOT EXISTS "${ARCHIVE}")
    get_filename_component(_cache "${ARCHIVE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_cache}")
    file(LOCK "${ARCHIVE}.lock" GUARD PROCESS TIMEOUT 120)
    if(NOT EXISTS "${ARCHIVE}")
        # Keep TLS verification enabled; use a trusted CA bundle on MinGW.
        set(_tls_options "")
        if(DEFINED ENV{CMAKE_TLS_CAINFO} AND EXISTS "$ENV{CMAKE_TLS_CAINFO}")
            list(APPEND _tls_options TLS_CAINFO "$ENV{CMAKE_TLS_CAINFO}")
        elseif(DEFINED ENV{SSL_CERT_FILE} AND EXISTS "$ENV{SSL_CERT_FILE}")
            list(APPEND _tls_options TLS_CAINFO "$ENV{SSL_CERT_FILE}")
        else()
            foreach(_trust IN ITEMS
                "C:/msys64/mingw64/etc/ssl/cert.pem"
                "C:/msys64/usr/ssl/cert.pem"
                "C:/msys64/mingw64/ssl/cert.pem")
                if(EXISTS "${_trust}")
                    file(SIZE "${_trust}" _ca_size)
                    if(_ca_size GREATER 1000)
                        list(APPEND _tls_options TLS_CAINFO "${_trust}")
                        break()
                    endif()
                endif()
            endforeach()
        endif()
        if(DEFINED ENV{NORTHSTARCPP_V8_ARCHIVE} AND EXISTS "$ENV{NORTHSTARCPP_V8_ARCHIVE}")
            file(COPY_FILE "$ENV{NORTHSTARCPP_V8_ARCHIVE}"
                "${ARCHIVE}.tmp" ONLY_IF_DIFFERENT)
            set(_download_status 0 "trusted local candidate")
        else()
            file(DOWNLOAD
                "https://storage.googleapis.com/chromium-v8/official/canary/v8-win64-rel-${_version}.zip"
                "${ARCHIVE}.tmp" EXPECTED_HASH "SHA256=${_sha256}"
                TLS_VERIFY ON ${_tls_options} STATUS _download_status
                TIMEOUT 120 INACTIVITY_TIMEOUT 30)
        endif()
        list(GET _download_status 0 _download_code)
        if(NOT _download_code EQUAL 0)
            file(REMOVE "${ARCHIVE}.tmp")
            message(FATAL_ERROR "V8 download failed: ${_download_status}. CA=${_tls_options}. "
                "For offline builds set NORTHSTARCPP_V8_ARCHIVE or NORTHSTARCPP_V8_RUNTIME_DIR.")
        endif()
        file(SHA256 "${ARCHIVE}.tmp" _candidate_hash)
        if(NOT _candidate_hash STREQUAL _sha256)
            file(REMOVE "${ARCHIVE}.tmp")
            message(FATAL_ERROR "V8 archive SHA-256 mismatch")
        endif()
        file(RENAME "${ARCHIVE}.tmp" "${ARCHIVE}" RESULT _rename_status)
        if(NOT _rename_status STREQUAL "0")
            message(FATAL_ERROR "V8 archive rename failed: ${_rename_status}")
        endif()
    endif()
    file(LOCK "${ARCHIVE}.lock" RELEASE)
endif()
file(SHA256 "${ARCHIVE}" _actual_sha256)
if(NOT _actual_sha256 STREQUAL _sha256)
    message(FATAL_ERROR "V8 sidecar archive SHA-256 mismatch: ${ARCHIVE}")
endif()

# Copy each missing or changed file independently, including while an unchanged
# d8.exe has an open handle. The temporary extraction stays in this build tree.
set(_extract "${RUNTIME_DIR}.extract")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${_extract}"
    PATTERNS d8.exe icudtl.dat snapshot_blob.bin)
file(MAKE_DIRECTORY "${RUNTIME_DIR}")
foreach(_file IN ITEMS d8.exe icudtl.dat snapshot_blob.bin)
    file(COPY_FILE "${_extract}/${_file}" "${RUNTIME_DIR}/${_file}" ONLY_IF_DIFFERENT)
endforeach()
file(REMOVE_RECURSE "${_extract}")
