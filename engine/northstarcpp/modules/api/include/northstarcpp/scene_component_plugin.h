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
// Optional ABI-1 table tail for reflected component fields. Existing plugins
// only use the unchanged prefix; new plugins MUST gate the tail via size.
#define NORTHSTARCPP_SCENE_COMPONENT_FIELD_U32 1u
#define NORTHSTARCPP_SCENE_COMPONENT_FIELD_F32 2u
typedef struct NorthStarSceneComponentFieldC {
    const char* label; // UTF-8, host copies immediately; max 80 bytes
    uint32_t kind;     // NORTHSTARCPP_SCENE_COMPONENT_FIELD_*
    uint32_t offset;   // little-endian, 4-byte aligned, within payload
} NorthStarSceneComponentFieldC;
typedef struct NorthStarSceneComponentHostApi {
    uint32_t size;
    uint32_t abi;
    uint32_t (*register_type)(const char* type, uint32_t min_version,
        uint32_t max_version, NorthStarSceneComponentValidateC validator,
        void* context);
    void (*unregister_type)(const char* type);
    // Optional extension; check API size before accessing this field.
    // The host copies descriptors before returning. Fields are versioned by
    // the registered component's min/max schema version.
    uint32_t (*register_type_reflected)(
        const char* type, uint32_t min_version, uint32_t max_version,
        NorthStarSceneComponentValidateC validator, void* context,
        const NorthStarSceneComponentFieldC* fields, uint32_t field_count);
} NorthStarSceneComponentHostApi;

// Optional exported entrypoint in a native DLL. The engine calls this after
// successfully selecting the provider and before exposing its service API:
//   extern "C" uint32_t northstarcpp_register_scene_components_v1(
//       const NorthStarSceneComponentHostApi* host);
// Return 1 on success. The host removes owned registrations before FreeLibrary.
// Only use the host API synchronously during registration.
typedef uint32_t (*NorthStarSceneComponentsRegisterC)(
    const NorthStarSceneComponentHostApi* host);
#ifdef __cplusplus
}
#endif
