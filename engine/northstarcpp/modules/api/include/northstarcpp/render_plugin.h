// NorthStarCpp
// Author: Kayla Verner
// Updated: 2026

#pragma once
#include <northstarcpp/module.h>

#define NORTHSTARCPP_RENDER_ABI 5u

typedef struct NorthStarRenderCube {
    float x, y, z;
    float size;
    uint32_t rgb;
    float rotation[9]; // three world-space basis vectors
} NorthStarRenderCube;

typedef struct NorthStarRenderFrame {
    const NorthStarRenderCube* cubes;
    uint32_t count;
    int32_t selected; // -1 for no highlight
    float camera_x, camera_y, camera_z;
    uint32_t verify_pixels; // nonzero requests a diagnostic readback
    uint32_t first_person; // FPS mode and HUD overlay
    float camera_yaw, camera_pitch; // radians; zero yaw faces +Z
    float weapon_recoil;           // visual animation, [0, 1.6]
    uint32_t player_health;        // HUD health points
    uint32_t player_max_health;
    uint32_t ammunition;           // rounds in active magazine
    uint32_t magazine_capacity;
    uint32_t overlay_paused;       // menu is controlled by host game, not renderer
    // ABI v4: optional camera pose/projection override from engine CameraDirector.
    uint32_t camera_override;
    float camera_roll;
    float camera_fov_y_degrees;
    float camera_near_clip;
    float camera_far_clip;
} NorthStarRenderFrame;

// Unlit, camera-centered textured sky geometry supplied by engine.assets.
// The host retains ownership of pointers. The renderer copies them on set_sky.
// The initial sky binding supports native NETD format 1 (RGBA8, mip 0).
typedef struct NorthStarSkyVertex {
    float x, y, z, u, v;
} NorthStarSkyVertex;

typedef struct NorthStarSky {
    const NorthStarSkyVertex* vertices;
    uint32_t vertex_count;
    const uint32_t* indices;
    uint32_t index_count;
    const uint8_t* rgba8;
    uint32_t width, height;
    float tint_r, tint_g, tint_b;
} NorthStarSky;

typedef struct NorthStarRenderApi {
    void* (*create)(void* native_window, uint32_t software); // HWND on Windows
    void (*destroy)(void* renderer);
    int (*draw)(void* renderer, const NorthStarRenderFrame* frame);
    int (*set_sky)(void* renderer, const NorthStarSky* sky); // null sky clears existing sky
} NorthStarRenderApi;

// Optional render memory instrumentation. Existing render ABI 5 is unchanged;
// older providers simply do not export this extension.
#define NORTHSTARCPP_RENDER_MEMORY_ABI 1u
#define NORTHSTARCPP_RENDER_MEMORY_MODULE_ABI 0x52444d01u
typedef struct NorthStarRenderMemoryStats {
    uint64_t vertex_buffer_bytes;
    uint64_t sky_geometry_and_texture_bytes;
    uint32_t vertex_buffer_reallocations;
    uint32_t world_vertex_capacity;
} NorthStarRenderMemoryStats;
typedef struct NorthStarRenderMemoryApi {
    uint32_t abi_version;
    int (*read)(void* renderer,NorthStarRenderMemoryStats* result);
} NorthStarRenderMemoryApi;
typedef const NorthStarRenderMemoryApi*
    (*NorthStarRenderGetMemoryApi)(uint32_t requested_version);
