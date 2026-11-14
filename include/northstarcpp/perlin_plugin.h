// NorthStarCpp engine.perlin provider ABI 1.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_PERLIN_ABI 1u
#define NORTHSTAR_PERLIN_DEFAULT_PERIOD 256u
#define NORTHSTAR_PERLIN_MAX_OCTAVES 16u

typedef enum NorthStarPerlinMode {
    NORTHSTAR_PERLIN_FBM = 0,
    NORTHSTAR_PERLIN_TURBULENCE = 1,
    NORTHSTAR_PERLIN_RIDGED = 2
} NorthStarPerlinMode;

typedef struct NorthStarPerlinConfig {
    uint32_t size;        // sizeof(NorthStarPerlinConfig)
    uint32_t period_x;    // 0 defaults to 256; otherwise 1..256 lattice cells
    uint32_t period_y;    // 0 defaults to 256
    uint32_t period_z;    // 0 defaults to 256
    uint64_t seed;        // deterministic, independent of process RNG
} NorthStarPerlinConfig;

typedef struct NorthStarPerlinFractal {
    uint32_t size;        // sizeof(NorthStarPerlinFractal)
    uint32_t octaves;     // 1..16
    uint32_t mode;        // NorthStarPerlinMode
    uint32_t reserved;    // must be zero
    double frequency;     // finite > 0
    double lacunarity;    // finite >= 1
    double persistence;   // finite in [0, 1]
} NorthStarPerlinFractal;

typedef struct NorthStarPerlinApi {
    void* (*create)(const NorthStarPerlinConfig* config);
    void (*destroy)(void* context);
    int (*sample1)(void* context, double x, float* value);
    int (*sample2)(void* context, double x, double y, float* value);
    int (*sample3)(void* context, double x, double y, double z, float* value);
    int (*fractal1)(void* context, double x,
                    const NorthStarPerlinFractal* config, float* value);
    int (*fractal2)(void* context, double x, double y,
                    const NorthStarPerlinFractal* config, float* value);
    int (*fractal3)(void* context, double x, double y, double z,
                    const NorthStarPerlinFractal* config, float* value);
    // Writes height rows of width floats. Row stride is in float elements.
    // No allocations or writes to padding; total cells capped by provider.
    int (*fill2)(void* context, double x, double y, double dx, double dy,
                 uint32_t width, uint32_t height, uint32_t row_stride,
                 const NorthStarPerlinFractal* config, float* output);
} NorthStarPerlinApi;

#ifdef __cplusplus
}
#endif
