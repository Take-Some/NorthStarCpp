// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <stdint.h>

#if defined(_WIN32) && defined(NORTHSTARCPP_MODULE_BUILD)
# define NORTHSTARCPP_MODULE_EXPORT __declspec(dllexport)
#elif defined(NORTHSTARCPP_MODULE_BUILD) && defined(__GNUC__)
# define NORTHSTARCPP_MODULE_EXPORT __attribute__((visibility("default")))
#else
# define NORTHSTARCPP_MODULE_EXPORT
#endif

#define NORTHSTARCPP_MODULE_ABI 1u
#define NORTHSTARCPP_CAPABILITY_ECS "engine.ecs"
#define NORTHSTARCPP_CAPABILITY_PHYSICS "engine.physics"
#define NORTHSTARCPP_CAPABILITY_RENDER "engine.render"
#define NORTHSTARCPP_CAPABILITY_AUDIO "engine.audio"
#define NORTHSTARCPP_CAPABILITY_INPUT "engine.input"
#define NORTHSTARCPP_CAPABILITY_ASSETS "engine.assets"
#define NORTHSTARCPP_CAPABILITY_LOGGER "engine.logger"
#define NORTHSTARCPP_CAPABILITY_PERLIN "engine.perlin"
#define NORTHSTARCPP_CAPABILITY_UI "engine.ui"
#define NORTHSTARCPP_CAPABILITY_GEOMETRY "engine.geometry"
#define NORTHSTARCPP_CAPABILITY_COLLISION "engine.collision"
#define NORTHSTARCPP_CAPABILITY_TIME "engine.time"
#define NORTHSTARCPP_CAPABILITY_SCRIPTING "engine.scripting"
#define NORTHSTARCPP_CAPABILITY_VEHICLE "engine.vehicle"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NorthStarModule {
    uint32_t size;
    uint32_t host_abi;
    const char* capability;
    const char* provider;
    const char* version;
    uint32_t api_version;
    uint32_t api_size;
    const void* api;
    const char* (*last_error)(void);
} NorthStarModule;

// Return a table with static lifetime, or NULL when the request is unsupported.
// All API functions use the C calling convention. Providers catch all C++ exceptions.
typedef const NorthStarModule* NorthStarModuleQueryFunction(uint32_t host_abi,
    const char* capability, uint32_t api_version);
typedef NorthStarModuleQueryFunction* NorthStarModuleQuery;

NORTHSTARCPP_MODULE_EXPORT const NorthStarModule* northstar_module_query(uint32_t host_abi,
    const char* capability, uint32_t api_version);

#ifdef __cplusplus
}
#endif
