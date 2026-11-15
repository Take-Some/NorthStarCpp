// NorthStarCpp
// Author: Kayla Verner
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace northstarcpp {
class AssetStore;

namespace shared_assets {

struct TextureBinding {
    std::string slot;
    std::string reference;
};

struct MaterialParameter {
    std::string name;
    std::string type;
    std::vector<float> values;
};

struct Material {
    std::string name;
    std::string shader;
    std::string blend;
    bool two_sided = false;
    float alpha_cutoff = 0.f;
    std::vector<MaterialParameter> parameters;
    std::vector<TextureBinding> textures;
    // Authoring graph is an optional native YMAT kind-30 payload field.
    // This is a C++ DTO member, not a change to the engine plugin C ABI.
    std::string graph_source;
};

struct Vertex {
    std::array<float, 3> position;
    std::array<float, 3> normal;
    std::array<float, 2> uv;
};

struct MeshPart {
    std::string name;
    std::string material;
    bool material_required = true;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct Drawable {
    std::string name;
    std::array<float, 3> bounds_min{};
    std::array<float, 3> bounds_max{};
    std::vector<MeshPart> meshes;
};

struct Mip {
    std::uint32_t width, height;
    std::size_t offset, size;
};

struct Texture {
    std::string name;
    std::uint64_t stable_hash;
    std::uint16_t format_id; // Native NETD format. BCn payloads remain compressed.
    bool srgb;
    std::vector<Mip> mips;
    std::vector<std::uint8_t> data;
};

template<class Asset>
struct ResolvedAsset {
    std::string reference;
    Asset asset;
};

struct DrawableResources {
    Drawable drawable;
    std::vector<ResolvedAsset<Material>> materials;
    std::vector<ResolvedAsset<Texture>> textures;
};

std::vector<Material> load_materials(AssetStore const& assets, std::string const& path);
std::vector<Material> load_materials_from_bytes(
    std::vector<std::uint8_t> const& bytes, std::string const& path);
std::vector<Drawable> load_drawables(AssetStore const& assets, std::string const& path);
std::vector<Texture> load_textures(AssetStore const& assets, std::string const& path);
// Read-only editor decoding of a single NEF8 ListFile without requiring
// a dynamically loaded engine.assets provider. Both routes share validators.
std::vector<Drawable> load_drawables_from_bytes(
    std::vector<std::uint8_t> const& nef8_bytes, std::string const& path);
std::vector<Texture> load_textures_from_bytes(
    std::vector<std::uint8_t> const& nef8_bytes, std::string const& path);
// Lightweight directory listing for NEF8 YTD/YDD ListFiles. This enumerates
// every named record without copying texture payloads or mesh vertex buffers.
struct DictionaryEntry {
    std::string name;
    std::uintmax_t size=0; // Byte size when stored explicitly (YTD payload).
};
std::vector<DictionaryEntry> list_dictionary_entries(
    AssetStore const& assets, std::string const& path);
std::vector<DictionaryEntry> list_dictionary_entries(
    std::vector<std::uint8_t> const& bytes, std::string const& path);
std::string load_source(AssetStore const& assets, std::string const& path);

struct Inventory {
    std::size_t materials = 0, drawables = 0, textures = 0, scripts = 0, shaders = 0;
    std::size_t resolved_materials = 0, resolved_textures = 0;
    std::size_t unsupported_skeletons = 0;
    std::vector<std::string> missing_references;
};

// Scan a physical shared asset directory and resolve its contents through VFS.
Inventory inspect_directory(std::filesystem::path const& root);

} // namespace shared_assets
} // namespace northstarcpp
