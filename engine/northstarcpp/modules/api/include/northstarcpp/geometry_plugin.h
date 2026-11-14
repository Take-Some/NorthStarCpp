// NorthStarCpp engine.geometry provider ABI 1.
// Adapts retained foundation geometry algorithms without leaking C++ types.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_GEOMETRY_ABI 1u
#define NORTHSTARCPP_GEOMETRY_MAX_VERTICES 65536u
#define NORTHSTARCPP_GEOMETRY_MAX_INDICES 393216u

typedef enum NorthStarGeometryKind {
    NORTHSTAR_GEOMETRY_SOLID = 0,
    NORTHSTAR_GEOMETRY_WIRE = 1,
    NORTHSTAR_GEOMETRY_RING = 2,
    NORTHSTAR_GEOMETRY_TORUS = 3
} NorthStarGeometryKind;

// Solids: cube=0, rectangle=1, cone=2, cylinder=3, ellipsoid=4.
// Wires: box=0, rectangle=1, cone=2, ellipsoid=3, ellipse=4.
// For ring, a/b are inner radius / width, u is segment count.
// For torus, a/b are major/minor radius, u/v are segment counts.
// All dimensions are in local-space units, with no transform applied.
typedef struct NorthStarGeometryRequest {
    uint32_t size;      // sizeof(NorthStarGeometryRequest)
    uint32_t kind;      // NorthStarGeometryKind
    uint32_t shape;     // [0,4] for SOLID/WIRE; 0 otherwise
    uint32_t flags;     // reserved, must be 0
    float a;            // ring inner radius or torus major radius
    float b;            // ring width or torus minor radius
    uint32_t u;         // segments along ring or major torus axis
    uint32_t v;         // torus minor segments, 0 for ring/primitive
} NorthStarGeometryRequest;

typedef struct NorthStarGeometryCounts {
    uint32_t size;      // sizeof(NorthStarGeometryCounts)
    uint32_t vertices;  // number of vertex triples
    uint32_t indices;   // number of uint16 triangle or line indices
    uint32_t stride;    // vertices are tightly packed xyz float triples (12)
} NorthStarGeometryCounts;

// Query sizes by passing both output pointers NULL with capacities zero.
// When writing, both buffers must be supplied with capacities in float
// elements and uint16 elements respectively. The caller owns all memory.
// Failed calls return 0 and never partially write output buffers.
// Valid results have indices < vertices and bounded counts.
typedef struct NorthStarGeometryApi {
    int (*generate)(const NorthStarGeometryRequest* request,
                    NorthStarGeometryCounts* counts,
                    float* xyz, uint32_t xyz_capacity,
                    uint16_t* indices, uint32_t index_capacity);
} NorthStarGeometryApi;

#ifdef __cplusplus
}
#endif
