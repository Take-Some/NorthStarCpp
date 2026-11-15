// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/character.hpp>
#include "asset_list_file.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace northstarcpp::shared_assets {
namespace {

using detail::extension;
using detail::require;
namespace fs = std::filesystem;

bool valid_utf8(std::string const& text)
{
    unsigned remaining = 0;
    unsigned minimum = 0x80, maximum = 0xbf;
    for (unsigned char ch : text) {
        if (remaining != 0) {
            if (ch < minimum || ch > maximum)
                return false;
            --remaining;
            minimum = 0x80;
            maximum = 0xbf;
        } else if (ch >= 0xc2 && ch <= 0xdf) {
            remaining = 1;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            remaining = 2;
            minimum = ch == 0xe0 ? 0xa0 : 0x80;
            maximum = ch == 0xed ? 0x9f : 0xbf;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            remaining = 3;
            minimum = ch == 0xf0 ? 0x90 : 0x80;
            maximum = ch == 0xf4 ? 0x8f : 0xbf;
        } else if (ch >= 0x80 || (ch < 32 && ch != 9 && ch != 10 && ch != 13)) {
            return false;
        }
    }
    return remaining == 0;
}

template<class Asset>
Asset const* find(std::map<std::string, std::vector<Asset>> const& cache, std::string const& reference)
{
    auto const at = reference.find('@');
    auto const dictionary = cache.find(reference.substr(0, at));
    if (dictionary == cache.end())
        return nullptr;
    auto const name = reference.substr(at + 1);
    auto const& list = dictionary->second;
    auto const selected = std::find_if(list.begin(), list.end(),
        [&](Asset const& asset) { return asset.name == name; });
    return selected == list.end() ? nullptr : &*selected;
}

template<class Asset, class Loader>
Asset const* resolve(AssetStore const& assets, std::map<std::string, std::vector<Asset>>& cache,
                     std::string const& reference, Loader loader)
{
    detail::validate_reference(reference);
    auto const path = reference.substr(0, reference.find('@'));
    if (cache.find(path) == cache.end())
        cache.emplace(path, loader(assets, path));
    return find(cache, reference);
}

template<class Asset, class Loader>
Asset selected(AssetStore const& assets, std::string const& reference, Loader loader)
{
    detail::validate_reference(reference);
    auto const separator = reference.find('@');
    auto list = loader(assets, reference.substr(0, separator));
    auto const name = reference.substr(separator + 1);
    auto const item = std::find_if(list.begin(), list.end(),
        [&](Asset const& asset) { return asset.name == name; });
    if (item == list.end())
        throw std::runtime_error("Asset entry not found: " + reference);
    return std::move(*item);
}

} // namespace

std::string load_source(AssetStore const& assets, std::string const& path)
{
    auto const ext = extension(path);
    require(ext == ".ysc" || ext == ".js" || ext == ".mjs" || ext == ".vert" || ext == ".frag", "Expected a script or shader source");
    require(assets.size(path) <= 4u * 1024u * 1024u, "Asset source exceeds 4 MiB");
    auto const bytes = assets.read(path);
    require(bytes.size() <= 4u * 1024u * 1024u, "Asset source exceeds 4 MiB");
    std::string result(bytes.begin(), bytes.end());
    require(!result.empty() && valid_utf8(result), "Asset source must be nonempty UTF-8 text");
    return result;
}

Inventory inspect_directory(fs::path const& root)
{
    AssetManager assets;
    assets.mount_shared_assets(root);
    Inventory result;
    std::map<std::string, std::vector<Material>> materials;
    std::map<std::string, std::vector<Drawable>> models;
    std::map<std::string, std::vector<Texture>> textures;
    for (auto const& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_symlink() || !entry.is_regular_file())
            continue;
        auto const path = entry.path().lexically_relative(root).generic_u8string();
        auto const ext = extension(path);
        try {
            if (ext == ".ymat") {
                auto const source=detail::read_list_file(assets,path);
                if(source.kind==30) {
                    // Authored character materials are JSON YMAT, not the
                    // world material XML YMAT variant. Validate separately.
                    auto const data=detail::parse_json(
                        std::string(source.body.begin(),source.body.end()));
                    auto const& records=data.get_child("materials");
                    detail::require(data.get<std::string>("schema")=="northstar.ymat.v1" &&
                        records.size()==source.entries,"Invalid character material library");
                    std::set<std::string> names;
                    for(auto const& record:records) {
                        detail::require(record.first.empty(),"Invalid character material array");
                        auto name=record.second.get<std::string>("name");
                        detail::validate_name(name);
                        detail::require(names.insert(name).second,
                                        "Duplicate character material name");
                    }
                    result.materials+=records.size();
                } else {
                    auto list=detail::parse_materials(source,path);
                    result.materials+=list.size();
                    materials.emplace(path,std::move(list));
                }
            } else if (ext == ".ymt") {
                auto const file = detail::read_list_file(assets, path);
                if (detail::legacy_material_library(file))
                    throw std::runtime_error("Legacy .ymt material: convert to .ymat");
                if(file.kind==10) {
                    (void)load_skeleton(assets,path);
                } else ++result.unsupported_skeletons;
            } else if (ext == ".ydd" || ext == ".ydr") {
                auto const file=detail::read_list_file(assets,path);
                if (file.kind==2 && file.body.size()>=40 &&
                    detail::Bytes(file.body).integer(0,4)==5) {
                    auto const geometry=detail::Bytes(file.body).integer(32,8);
                    if (geometry<=file.body.size()-8 &&
                        detail::Bytes(file.body).integer(geometry+4,4)!=1) {
                        // Opaque attachment/shell records are not mesh
                        // dictionaries; keep them accounted for, not rendered
                        // as a fabricated cube or rejected from the catalog.
                        ++result.unsupported_skeletons;
                        continue;
                    }
                }
                // Binary YDD is a first-class drawable. A matching .ymt
                // skeleton enables the optional skeletal path; it must not
                // be required for static rendering of an otherwise valid YDD.
                auto skeleton_path=fs::path(path).replace_extension(".ymt").generic_u8string();
                bool has_skeleton=false;
                try { (void)assets.size(skeleton_path); has_skeleton=true; }
                catch (std::runtime_error const&) {}
                if(file.kind==2 && has_skeleton && file.body.size()>=4 &&
                   detail::Bytes(file.body).integer(0,4)==4) {
                    auto rig=load_skeleton(assets,skeleton_path);
                    auto model=load_character_model(assets,path,rig);
                    result.drawables+=model.parts.size();
                } else {
                    auto list=load_drawables(assets,path);
                    result.drawables+=list.size();
                    models.emplace(path,std::move(list));
                }
            } else if (ext == ".ytd") {
                auto list = load_textures(assets, path);
                result.textures += list.size();
                textures.emplace(path, std::move(list));
            } else if (ext == ".ysc" || ext == ".js" || ext == ".mjs" || ext == ".vert" || ext == ".frag") {
                load_source(assets, path);
                if (ext == ".ysc" || ext == ".js" || ext == ".mjs")
                    ++result.scripts;
                else
                    ++result.shaders;
            }
        } catch (std::exception const& error) {
            throw std::runtime_error(path + ": " + error.what());
        }
    }
    for (auto const& dictionary : models)
        for (auto const& model : dictionary.second)
            for (auto const& mesh : model.meshes)
                if (!mesh.material.empty()) {
                    if (find(materials, mesh.material))
                        ++result.resolved_materials;
                    else if (mesh.material_required)
                        result.missing_references.push_back(mesh.material);
                }
    for (auto const& library : materials)
        for (auto const& material : library.second)
            for (auto const& texture : material.textures) {
                if (find(textures, texture.reference))
                    ++result.resolved_textures;
                else
                    result.missing_references.push_back(texture.reference);
            }
    std::sort(result.missing_references.begin(), result.missing_references.end());
    result.missing_references.erase(std::unique(result.missing_references.begin(),
        result.missing_references.end()), result.missing_references.end());
    return result;
}

} // namespace northstarcpp::shared_assets

namespace northstarcpp {

void AssetStore::mount_shared_assets(std::filesystem::path const& root)
{
    mount_directory("", root, -100);
}

shared_assets::Material AssetStore::load_material(std::string const& reference) const
{
    return shared_assets::selected<shared_assets::Material>(*this, reference, shared_assets::load_materials);
}

shared_assets::Drawable AssetStore::load_drawable(std::string const& reference) const
{
    return shared_assets::selected<shared_assets::Drawable>(*this, reference, shared_assets::load_drawables);
}

shared_assets::Texture AssetStore::load_texture(std::string const& reference) const
{
    return shared_assets::selected<shared_assets::Texture>(*this, reference, shared_assets::load_textures);
}

shared_assets::DrawableResources AssetStore::load_drawable_resources(std::string const& reference) const
{
    using namespace shared_assets;
    DrawableResources result;
    result.drawable = load_drawable(reference);
    std::map<std::string, std::vector<Material>> materials;
    std::map<std::string, std::vector<Texture>> textures;
    std::set<std::string> included_materials, included_textures;
    for (auto const& mesh : result.drawable.meshes) {
        if (mesh.material.empty() || included_materials.count(mesh.material))
            continue;
        auto const path = mesh.material.substr(0, mesh.material.find('@'));
        if (!mesh.material_required) {
            try {
                size(path);
            } catch (std::runtime_error const&) {
                continue;
            }
        }
        auto const* material = resolve(*this, materials, mesh.material, load_materials);
        if (!material && !mesh.material_required)
            continue;
        if (!material)
            throw std::runtime_error("Material entry not found: " + mesh.material);
        included_materials.insert(mesh.material);
        result.materials.push_back({mesh.material, *material});
        for (auto const& binding : material->textures) {
            if (!included_textures.insert(binding.reference).second)
                continue;
            auto const* texture = resolve(*this, textures, binding.reference, load_textures);
            if (!texture)
                throw std::runtime_error("Texture entry not found: " + binding.reference);
            result.textures.push_back({binding.reference, *texture});
        }
    }
    return result;
}

} // namespace northstarcpp
