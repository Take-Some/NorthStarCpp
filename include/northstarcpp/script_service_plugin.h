// NorthStarCpp C ABI for replaceable engine.* script services.
#pragma once
#include <northstarcpp/scripting_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

// A provider returns a NorthStarModule for the script namespace, using this
// distinct API version alongside any typed native ABI for the same capability.
#define NORTHSTARCPP_SCRIPT_SERVICE_ABI 0x53564301u

typedef struct NorthStarScriptServiceApi {
    // One context per script runtime and capability. The fallback is scoped to
    // this capability and invokes only the runtime's registered host methods.
    // Calls are synchronous on the runtime's thread. Copy any borrowed data
    // from the fallback before calling it again.
    void* (*create)(NorthStarScriptHostCall fallback, void* user);
    void (*destroy)(void* context);
    int (*supports)(void* context, const char* method_utf8);
    // Return 1 with a JSON value on success, or 0 with an optional UTF-8 error.
    // The result belongs to the provider until the next call or context teardown.
    // The host falls back only when supports() returns zero.
    int (*invoke)(void* context, const char* method_utf8,
                  const char* args_json_utf8, uint32_t args_json_bytes,
                  NorthStarScriptResult* result);
} NorthStarScriptServiceApi;

#ifdef __cplusplus
}
#endif
