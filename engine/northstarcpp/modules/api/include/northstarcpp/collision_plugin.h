// NorthStarCpp native collision provider ABI v1.
// Engine-owned foundation spatial queries, replaceable by external plugins.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NORTHSTARCPP_COLLISION_ABI 1u
typedef struct NorthStarCollisionRay {
    uint32_t size;
    float origin[3];
    float direction[3]; // normalized for world-distance semantics
    float maximum_distance;
} NorthStarCollisionRay;
typedef struct NorthStarCollisionBounds {
    uint32_t size;
    float minimum[3];
    float maximum[3];
} NorthStarCollisionBounds;
typedef struct NorthStarCollisionHit {
    uint32_t size;
    uint32_t hit;           // 0 = no hit, 1 = hit
    float distance;         // parameter along normalized ray
    uint32_t triangle;      // triangle-list index, 0 for AABB hits
} NorthStarCollisionHit;
// Every function returns 1 for successful evaluation (even for misses),
// 0 for API/validation failure. An invalid input never writes a partial hit.
// xyz is an array of vertex_count tightly packed float[3] coordinates;
// indices are a triangle list with index_count divisible by three.
// All API structures, arrays, and output memory belong to the caller.
typedef struct NorthStarCollisionApi {
    int (*ray_aabb)(NorthStarCollisionRay const* ray,
                    NorthStarCollisionBounds const* bounds,
                    NorthStarCollisionHit* hit);
    int (*ray_triangles)(NorthStarCollisionRay const* ray,
                         float const* xyz, uint32_t vertex_count,
                         uint16_t const* indices, uint32_t index_count,
                         NorthStarCollisionHit* hit);
} NorthStarCollisionApi;
#ifdef __cplusplus
}
#endif
