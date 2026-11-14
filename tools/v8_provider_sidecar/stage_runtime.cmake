# Build-time staging of the pinned developer runtime.
cmake_minimum_required(VERSION 3.21)
set(_version 15.8.62)
set(_sha256 9924fe95ae8a05b35f728da8258bd1d72a8810edc0179d65fd3018c7845489e3)

if(NOT EXISTS "${ARCHIVE}")
    get_filename_component(_cache "${ARCHIVE}" DIRECTORY)
    file(MAKE_DIRECTORY "${_cache}")
    file(LOCK "${ARCHIVE}.lock" GUARD PROCESS TIMEOUT 120)
    if(NOT EXISTS "${ARCHIVE}")
        file(DOWNLOAD
            "https://storage.googleapis.com/chromium-v8/official/canary/v8-win64-rel-${_version}.zip"
            "${ARCHIVE}.tmp" EXPECTED_HASH "SHA256=${_sha256}" TLS_VERIFY ON
            TIMEOUT 120 INACTIVITY_TIMEOUT 30)
        file(RENAME "${ARCHIVE}.tmp" "${ARCHIVE}")
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
