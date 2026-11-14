// NorthStarCpp engine.logger ABI 1. Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// C ABI: providers keep no pointers supplied to write() after it returns.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_LOGGER_ABI 1u

typedef enum NorthStarLogLevel {
    NORTHSTAR_LOG_TRACE = 0,
    NORTHSTAR_LOG_DEBUG = 1,
    NORTHSTAR_LOG_INFO  = 2,
    NORTHSTAR_LOG_WARN  = 3,
    NORTHSTAR_LOG_ERROR = 4,
    NORTHSTAR_LOG_FATAL = 5
} NorthStarLogLevel;

enum {
    NORTHSTAR_LOG_SINK_CONSOLE = 1u,
    NORTHSTAR_LOG_SINK_TEXT    = 2u,
    NORTHSTAR_LOG_SINK_STRUCTURED = 4u,
    NORTHSTAR_LOG_SINK_DEBUGGER = 8u
};

typedef struct NorthStarLoggerConfig {
    uint32_t size;                 // sizeof(NorthStarLoggerConfig)
    const char* directory_utf8;   // null/empty = %LOCALAPPDATA%/.../Logs
    const char* session_utf8;     // ASCII basename, no path separators
    uint32_t sinks;               // bitwise NorthStarLogSink values
    uint32_t minimum_level;       // NorthStarLogLevel, inclusive
    uint64_t rotate_bytes;        // 0 disables rotation
    uint32_t backup_count;        // 0..16
} NorthStarLoggerConfig;

typedef struct NorthStarLogRecord {
    uint32_t size;                 // sizeof(NorthStarLogRecord)
    uint32_t level;                // NorthStarLogLevel
    const char* channel_utf8;      // may be empty, not null
    uint32_t channel_bytes;
    const char* message_utf8;      // may be empty, not null
    uint32_t message_bytes;
} NorthStarLogRecord;

typedef struct NorthStarLoggerApi {
    void* (*create)(const NorthStarLoggerConfig* config);
    void (*destroy)(void* context);
    int (*write)(void* context, const NorthStarLogRecord* record);
    int (*flush)(void* context);
    int (*set_minimum_level)(void* context, uint32_t level);
} NorthStarLoggerApi;

#ifdef __cplusplus
}
#endif
