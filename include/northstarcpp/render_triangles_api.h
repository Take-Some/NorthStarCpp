// NorthStarCpp - optional geometry extension to the versioned render ABI.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/render_plugin.h>

// Keeps render ABI 5 unchanged; older providers need not implement this API.
#define NORTHSTARCPP_RENDER_TRIANGLES_ABI 1u
#define NORTHSTARCPP_RENDER_TRIANGLES_MODULE_ABI 0x52445401u
typedef struct NorthStarRenderTriangleVertex {
    float x, y, z;
    uint32_t rgb;
} NorthStarRenderTriangleVertex;
typedef struct NorthStarRenderTrianglesApi {
    uint32_t abi_version;
    // The provider MUST COPY supplied vertices before returning.
    // count == 0 and vertices == nullptr clears geometry for the next frame.
    // Vertices are world-space, already triangulated, and colored per vertex.
    int (*set_triangles)(void* renderer, const NorthStarRenderTriangleVertex* vertices,
                         uint32_t count);
} NorthStarRenderTrianglesApi;
typedef const NorthStarRenderTrianglesApi*
    (*NorthStarRenderGetTrianglesApi)(uint32_t requested_version);
