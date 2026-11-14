// NorthStarCpp engine.scripting provider ABI 2.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_SCRIPTING_ABI 2u
#define NORTHSTAR_SCRIPTING_MAX_SOURCE_BYTES (1024u * 1024u)

typedef struct NorthStarScriptingConfig {
    uint32_t size;              // sizeof(NorthStarScriptingConfig)
    uint32_t heap_limit_mb;     // 16..512 MB; soft V8 heap configuration
    uint32_t timeout_ms;        // 1..60000; synchronous evaluation deadline
    uint32_t max_source_bytes;  // 1..1048576
} NorthStarScriptingConfig;

// The provider owns json_utf8. It remains valid until the next evaluate/reset
// on this context or its destruction. The caller must copy before then.
// JSON.stringify semantics: undefined becomes JSON "null"; serialization
// failures (including circular references and BigInt) are reported as errors.
typedef struct NorthStarScriptResult {
    uint32_t size;             // sizeof(NorthStarScriptResult)
    const char* json_utf8;     // UTF-8, not necessarily NUL-terminated
    uint32_t json_bytes;
} NorthStarScriptResult;

// Host returns a borrowed JSON result on success or UTF-8 error on failure.
// The provider copies/parses it synchronously before the callback returns.
// Callback must not re-enter the same runtime.
typedef int (*NorthStarScriptHostCall)(
    void* user, const char* capability_utf8, const char* method_utf8,
    const char* args_json_utf8, uint32_t args_json_bytes,
    const char** response_utf8, uint32_t* response_bytes);

typedef struct NorthStarScriptingApi {
    void* (*create)(const NorthStarScriptingConfig* config);
    void (*destroy)(void* context);
    // Executes in a persistent JS global context. Returns 1 on success.
    // Caller supplies source_bytes; source may contain embedded NUL (rejected).
    // origin_utf8 is optional (null means "<script>").
    int (*evaluate)(void* context, const char* source_utf8,
                    uint32_t source_bytes, const char* origin_utf8,
                    NorthStarScriptResult* result);
    // Discards variables/functions and starts with a fresh global context.
    int (*reset)(void* context);
    // Installs an allowlisted host dispatcher (null to revoke). The callback
    // is never called from another thread during a synchronous evaluation.
    int (*set_host_call)(void* context, NorthStarScriptHostCall callback, void* user);
} NorthStarScriptingApi;

#ifdef __cplusplus
}
#endif
