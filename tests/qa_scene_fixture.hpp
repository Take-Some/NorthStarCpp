// Test-only adapter for V3 scene fixtures authored before component-based Pawn.
// No legacy SceneObject::player field or production ABI compatibility shim.
#pragma once
#include <northstarcpp/scene_pawn.hpp>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>

namespace northstarcpp::qa {
struct Object {
    std::string name;
    Vector3 position;
    float size;
    std::uint32_t color;
    bool local_player;
    float mass;
};

inline void make_local(Scene& scene, std::size_t index) {
    if (scene.version != 3 || index >= scene.objects.size())
        throw std::invalid_argument("QA local Pawn requires valid Scene V3 object");
    if (!scene.objects[index].node.id)
        throw std::invalid_argument("QA local Pawn requires stable entity ID");
    scene_pawn::migrate_imported_local(scene, scene.objects[index].node.id);
}

inline void append(Scene& scene, Object spec) {
    // Fixture-generated objects always use stable IDs, never transient ECS handles.
    if (scene.version != 3) {
        scene.version = 3;
        std::uint64_t id = 1;
        for (auto& object : scene.objects)
            if (!object.node.id) object.node.id = id++;
    }
    std::uint64_t id = 0;
    for (auto const& object : scene.objects)
        id = std::max(id, object.node.id);
    SceneObject object{std::move(spec.name), spec.position, spec.size,
                       spec.color, spec.mass};
    object.node.id = id + 1;
    scene.objects.push_back(std::move(object));
    if (spec.local_player) make_local(scene, scene.objects.size() - 1);
}

inline Scene make(std::initializer_list<Object> objects) {
    Scene scene;
    scene.version = 3;
    for (auto const& spec : objects)
        append(scene, spec);
    return scene;
}
} // namespace northstarcpp::qa
