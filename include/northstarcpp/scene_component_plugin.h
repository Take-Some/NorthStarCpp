// Stable C ABI for registering scene component schemas from engine plugins.
// The engine passes this table to a plugin during its lifecycle; plugins must
// unregister all types before their code is unloaded.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_SCENE_COMPONENT_HOST_ABI 1u
typedef uint32_t (*NorthStarSceneComponentValidateC)(
    void* context, uint32_t version, const void* payload, size_t bytes);
typedef struct NorthStarSceneComponentHostApi {
    uint32_t size;
    uint32_t abi;
    uint32_t (*register_type)(const char* type, uint32_t min_version,
        uint32_t max_version, NorthStarSceneComponentValidateC validator,
        void* context);
    void (*unregister_type)(const char* type);
} NorthStarSceneComponentHostApi;
#ifdef __cplusplus
}
#endif
