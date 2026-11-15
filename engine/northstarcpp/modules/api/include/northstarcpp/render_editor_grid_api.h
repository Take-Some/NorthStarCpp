// NorthStarCpp — optional analytic editor grid renderer capability.
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// The extension is replaceable with the owning engine.render provider.
// Core NorthStarRenderApi ABI 5 is unchanged; older providers can omit it.
#pragma once
#include <northstarcpp/module.h>
#include <stdint.h>

#define NORTHSTARCPP_RENDER_EDITOR_GRID_ABI 1u
#define NORTHSTARCPP_RENDER_EDITOR_GRID_MODULE_ABI 0x52454701u

typedef struct NorthStarRenderEditorGridSettings {
    uint32_t abi_version;
    uint32_t enabled;
    float minor_step;          // Positive world-space spacing in metres
    float opacity;             // [0, 1]
    float fade_start;          // World-space distance, >= 0
    float fade_end;            // World-space distance, > fade_start
    uint32_t frame_index;      // Reserved for history-based temporal AA
    uint32_t reserved;         // Must be zero
} NorthStarRenderEditorGridSettings;

typedef struct NorthStarRenderEditorGridApi {
    uint32_t abi_version;
    uint32_t struct_size;
    // set(nullptr) disables the overlay; no other renderer state changed.
    int (*set_grid)(void* renderer,
                    const NorthStarRenderEditorGridSettings* settings);
    // 1=analytical derivative AA, 2=history-reprojected temporal AA
    uint32_t anti_aliasing_modes;
} NorthStarRenderEditorGridApi;
typedef const NorthStarRenderEditorGridApi*
    (*NorthStarRenderGetEditorGridApi)(uint32_t requested_version);
