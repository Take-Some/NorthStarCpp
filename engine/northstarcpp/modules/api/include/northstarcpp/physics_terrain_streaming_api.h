// NorthStarCpp optional terrain-streaming Jolt ABI, independent of Physics ABI 4.
// Installed terrain chunks are static triangle meshes with exact world coordinates.
// Providers copy the supplied buffers and must allow replacement/removal by key.
#pragma once
#include <northstarcpp/physics_plugin.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_PHYSICS_TERRAIN_ABI 1u
#define NORTHSTARCPP_PHYSICS_TERRAIN_MODULE_ABI 0x50545201u
typedef struct NorthStarPhysicsTerrainApi {
    uint32_t abi_version;
    int (*upsert)(void* world,int32_t chunk_x,int32_t chunk_z,
                  const float* xyz,uint32_t vertex_count,
                  const uint32_t* triangles,uint32_t index_count);
    int (*remove)(void* world,int32_t chunk_x,int32_t chunk_z);
    uint32_t (*resident_count)(void* world);
    // Optimize broadphase once after all chunk residency changes.
    int (*commit)(void* world);
} NorthStarPhysicsTerrainApi;
typedef const NorthStarPhysicsTerrainApi*
    (*NorthStarPhysicsGetTerrainApi)(uint32_t requested_version);
#ifdef __cplusplus
}
#endif
