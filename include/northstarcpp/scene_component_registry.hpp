// Versioned ECS component provider registration, independent of ECS backend.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_component_plugin.h>

namespace northstarcpp {
using SceneComponentValidator = bool (*)(void* context, std::uint32_t version,
    const void* payload, std::size_t payload_size);
struct SceneComponentType {
    std::string type;                // stable reverse-DNS identifier
    std::uint32_t min_version=1;
    std::uint32_t max_version=1;
    SceneComponentValidator validate=nullptr;
    void* context=nullptr;
};
// Explicitly register/unregister as plugin loads/unloads. Providers must
// unregister before unloading their DLL; calls are serialized by the registry.
bool register_scene_component_type(SceneComponentType value);
void unregister_scene_component_type(std::string const& type);
bool validate_scene_component_blob(SceneComponentBlob const& value);
// Pass this table to native plugins; no dependency on C++ layout is required.
NorthStarSceneComponentHostApi const* scene_component_host_api_v1() noexcept;
} // namespace northstarcpp
