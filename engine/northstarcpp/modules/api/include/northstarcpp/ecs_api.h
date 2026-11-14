// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
/* Provider-independent NorthStarCpp ECS ABI. No C++ or engine-specific types. */
#include <stddef.h>
#include <stdint.h>
#define NORTHSTARCPP_ECS_API_VERSION 2u
#define NORTHSTARCPP_ECS_NAME_BYTES 97u
#if defined(_WIN32) && defined(NORTHSTARCPP_ECS_BUILD)
# define NORTHSTARCPP_ECS_API_EXPORT __declspec(dllexport)
#else
# define NORTHSTARCPP_ECS_API_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef uint64_t NsEntity;
typedef struct NsName { char text[NORTHSTARCPP_ECS_NAME_BYTES]; } NsName;
typedef struct NsPosition { float x,y,z; } NsPosition;
typedef struct NsVisual { float size; uint32_t color; } NsVisual;
typedef struct NsBody { float mass; } NsBody;
typedef struct NsController { uint8_t enabled; } NsController;
typedef struct NsMotion { float x,z; } NsMotion;
// Optional serialized V3 script component (UTF-8 virtual script path).
typedef struct NsScriptComponent { char path[513]; } NsScriptComponent;

/* Component identifiers are stable across providers. */
#define NS_COMPONENT_NAME "northstar.name"
#define NS_COMPONENT_POSITION "northstar.position"
#define NS_COMPONENT_VISUAL "northstar.visual"
#define NS_COMPONENT_BODY "northstar.body"
#define NS_COMPONENT_CONTROLLER "northstar.controller"
#define NS_COMPONENT_MOTION "northstar.motion"
#define NS_COMPONENT_SCRIPT "northstar.script"

typedef struct NsEcsApi {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* (*provider)(void);
    const char* (*last_error)(void);
    void* (*create_world)(void);
    void (*destroy_world)(void*);
    /* Register a POD component type. Equal registrations are idempotent. */
    int (*register_component)(void*, const char* key, size_t size, size_t alignment);
    NsEntity (*create_entity)(void*);
    int (*destroy_entity)(void*, NsEntity);
    int (*is_alive)(void*, NsEntity);
    size_t (*count)(void*);
    NsEntity (*at)(void*, size_t index);
    int (*set)(void*, NsEntity, const char* key, const void* bytes, size_t size);
    int (*get)(void*, NsEntity, const char* key, void* out, size_t capacity);
    int (*has)(void*, NsEntity, const char* key);
    int (*remove)(void*, NsEntity, const char* key);
    /* Number of matching entities, even if capacity is insufficient.
       Only nonzero entity IDs are written. Null filters means all. */
    size_t (*query)(void*, const char* const* required, size_t required_count,
                    NsEntity* output, size_t capacity);
    int (*progress)(void*, float seconds, float input_x, float input_z);
} NsEcsApi;

/* Legacy ECS-only entry point. New modules export northstar_module_query. */
NORTHSTARCPP_ECS_API_EXPORT const NsEcsApi* northstar_ecs_get_api(uint32_t required_version);
#ifdef __cplusplus
}
#endif
