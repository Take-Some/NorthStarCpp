// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_ASSETS_ABI 1u
// All asset keys use UTF-8 with '/' separators.
// no absolute paths, parent traversal, or drive paths are valid asset keys.
// Assets are read-only in this ABI. Data buffer is owned by the caller.
typedef struct NorthStarAssetsApi {
    void* (*create)(void);
    void (*destroy)(void* context);
    int (*mount_directory)(void* context, const char* virtual_prefix,
                           const char* physical_directory, int32_t priority);
    int (*unmount_directory)(void* context, const char* virtual_prefix,
                             const char* physical_directory);
    int (*file_size)(void* context, const char* virtual_path, uint64_t* size);
    int (*read)(void* context, const char* virtual_path, uint8_t* buffer,
                uint64_t capacity, uint64_t* size);
} NorthStarAssetsApi;

// Optional ABI 2 table on the same module. ABI 1 remains the discovery contract.
#define NORTHSTARCPP_ASSETS_ARCHIVE_ABI 2u
typedef struct NorthStarAssetsArchiveApi {
    int (*mount_archive)(void* context, const char* virtual_prefix,
                         const char* index_file, const char* data_file, int32_t priority);
    int (*unmount_archive)(void* context, const char* virtual_prefix,
                           const char* index_file, const char* data_file);
} NorthStarAssetsArchiveApi;

// Optional ABI 3 extension: exact positioned reads for NEF8 indexed cells.
 // ABI 1 and archive ABI 2 are unchanged. A provider may omit ABI 3.
#define NORTHSTARCPP_ASSETS_RANGE_ABI 3u
typedef struct NorthStarAssetsRangeApi {
    // Returns success only when the whole requested range has been read.
    // offset and length are validated against the logical uncompressed file.
    int (*read_range)(void* context, const char* virtual_path,
                      uint64_t offset, uint8_t* buffer,
                      uint64_t length, uint64_t* received);
} NorthStarAssetsRangeApi;

#ifdef __cplusplus
}
#endif
