// NorthStarCpp
// Author: Kayla Verner
#pragma once

#include <northstarcpp/fps_assets.hpp>
#include <northstarcpp/scene.hpp>
#include <northstarcpp/shared_assets.hpp>

namespace northstarcpp::detail {

// Account for decoded payloads and the storage held by their containers.
inline std::size_t memory_size(std::string const& value) { return value.capacity() + 1; }
template<class T> std::size_t memory_size(std::vector<T> const& values)
{
    return values.capacity() * sizeof(T);
}
inline std::size_t memory_size(shared_assets::Material const& material)
{
    auto bytes = memory_size(material.name) + memory_size(material.shader) +
        memory_size(material.blend) + memory_size(material.parameters) + memory_size(material.textures);
    for (auto const& parameter : material.parameters)
        bytes += memory_size(parameter.name) + memory_size(parameter.type) + memory_size(parameter.values);
    for (auto const& texture : material.textures)
        bytes += memory_size(texture.slot) + memory_size(texture.reference);
    return bytes;
}
inline std::size_t memory_size(shared_assets::Drawable const& drawable)
{
    auto bytes = memory_size(drawable.name) + memory_size(drawable.meshes);
    for (auto const& mesh : drawable.meshes)
        bytes += memory_size(mesh.name) + memory_size(mesh.material) +
            memory_size(mesh.vertices) + memory_size(mesh.indices);
    return bytes;
}
inline std::size_t memory_size(shared_assets::Texture const& texture)
{
    return memory_size(texture.name) + memory_size(texture.mips) + memory_size(texture.data);
}
inline std::size_t memory_size(shared_assets::DrawableResources const& graph)
{
    auto bytes = memory_size(graph.drawable) + memory_size(graph.materials) + memory_size(graph.textures);
    for (auto const& item : graph.materials)
        bytes += memory_size(item.reference) + memory_size(item.asset);
    for (auto const& item : graph.textures)
        bytes += memory_size(item.reference) + memory_size(item.asset);
    return bytes;
}
inline std::size_t memory_size(Scene const& scene)
{
    auto bytes = memory_size(scene.objects);
    for (auto const& object : scene.objects)
        bytes += memory_size(object.name);
    return bytes;
}
inline std::size_t memory_size(FpsAudioClip const& clip) { return memory_size(clip.samples); }
inline std::size_t memory_size(FpsTuning const&) { return 0; }

} // namespace northstarcpp::detail
