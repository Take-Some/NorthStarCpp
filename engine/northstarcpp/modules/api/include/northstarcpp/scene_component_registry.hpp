// Versioned ECS component provider registration, independent of ECS backend.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
#include <variant>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_component_plugin.h>

namespace northstarcpp {
using SceneComponentValidator = bool (*)(void* context, std::uint32_t version,
    const void* payload, std::size_t payload_size);
// Value-owned editor metadata. Binary fields are little-endian, 4-byte scalars.
// The plugin C ABI remains unchanged; unknown payloads stay opaque.
enum class SceneComponentFieldKind : std::uint8_t { UInt32=1, Float32=2 };
struct SceneComponentField {
    std::string label;
    SceneComponentFieldKind kind=SceneComponentFieldKind::UInt32;
    std::uint32_t offset=0;
};
using SceneComponentFieldValue=std::variant<std::uint32_t,float>;
std::optional<SceneComponentFieldValue> read_scene_component_field(
    SceneComponentBlob const& component,SceneComponentField const& field);
bool write_scene_component_field(
    SceneComponentBlob& component,SceneComponentField const& field,
    SceneComponentFieldValue value);
struct SceneComponentType {
    std::string type;                // stable reverse-DNS identifier
    std::uint32_t min_version=1;
    std::uint32_t max_version=1;
    SceneComponentValidator validate=nullptr;
    void* context=nullptr;
    std::vector<SceneComponentField> editor_fields{};
};
// Explicitly register/unregister as plugin loads/unloads. Providers must
// unregister before unloading their DLL; calls are serialized by the registry.
bool register_scene_component_type(SceneComponentType value);
void unregister_scene_component_type(std::string const& type);
bool validate_scene_component_blob(SceneComponentBlob const& value);
// Editor-safe, value-only schema metadata; no plugin callback pointers escape
// the registration lock. Unknown component records remain fully opaque.
struct SceneComponentSchemaInfo {
    std::string type;
    std::uint32_t min_version=1;
    std::uint32_t max_version=1;
    std::vector<SceneComponentField> editor_fields{};
};
std::optional<SceneComponentSchemaInfo> scene_component_schema(
    std::string const& type);
std::vector<SceneComponentSchemaInfo> list_scene_component_schemas();
// Pass this table to native plugins; no dependency on C++ layout is required.
NorthStarSceneComponentHostApi const* scene_component_host_api_v1() noexcept;
// Track schemas registered by an external DLL; keep callbacks valid until unload.
bool acquire_scene_components_for_module(void* library,
    NorthStarSceneComponentsRegisterC callback) noexcept;
void release_scene_components_for_module(void* library) noexcept;
} // namespace northstarcpp
