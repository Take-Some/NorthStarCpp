// Engine-owned procedural primitive component for the scene authoring/runtime.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#pragma once
#include <northstarcpp/geometry_plugin.h>
#include <northstarcpp/scene.hpp>
#include <cstdint>

namespace northstarcpp {

// Persistent, engine-owned component schema. Payload is a fixed 24-byte
// little-endian record; no compiler layout or external DLL pointer is saved.
inline constexpr char geometry_primitive_component[] = "engine.geometry.primitive";
inline constexpr std::uint32_t geometry_primitive_schema = 1u;
inline constexpr std::uint32_t geometry_primitive_payload_bytes = 24u;

// Registers schema with the editor's component inspector and the scene
// validator. Safe to call repeatedly from Editor, Player or plugin host.
void register_geometry_primitive_component();

// Throws for malformed requests and returns a value-owned scene component.
SceneComponentBlob make_geometry_primitive_component(
    std::uint64_t entity_id, NorthStarGeometryRequest const& request);

// Returns false for other component types. For this component type, throws
// on a corrupt payload, unsupported version or invalid geometry parameters.
bool decode_geometry_primitive_component(SceneComponentBlob const& component,
                                          NorthStarGeometryRequest& request);

} // namespace northstarcpp
