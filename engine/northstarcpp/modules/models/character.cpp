#include <northstarcpp/character.hpp>
#include "asset_list_file.hpp"
#include "character_asset_namespace.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace northstarcpp {
namespace {
using namespace shared_assets::detail;
std::string name_at(Bytes const& bytes, std::uint64_t base, std::uint64_t end, std::uint64_t index)
{
    require(base <= end && index < end - base, "Invalid skeletal YDD name offset");
    auto const size = std::min<std::uint64_t>(end - base - index, 1025);
    auto const* begin = bytes.at(base + index, size);
    auto const* last = std::find(begin, begin + size, 0);
    require(last != begin + size && last != begin, "Invalid skeletal YDD name");
    return {begin, last};
}
}

CharacterModel load_character_model(AssetStore const& assets, std::string const& path,
                                    Skeleton const& rig)
{
    using namespace shared_assets::detail;
    auto file = read_list_file(assets, path);
    Bytes const bytes(file.body);
    require(file.kind == 2 && file.entries == 1 && bytes.integer(0,4) == 4 &&
        bytes.integer(4,4) == 1 && bytes.integer(8,8) == 40, "Expected a skeletal YDD v4");
    auto const strings = bytes.integer(16,8);
    auto const start = bytes.integer(32,8);
    // Offset 52 stores the drawable LOD metadata; mesh count is at 56.
    auto const count = bytes.integer(56,4);
    require(strings == 120 && start >= strings && start <= bytes.size() &&
        bytes.integer(104,8) == start && bytes.integer(112,8) == bytes.size() - start &&
        count > 0 && count <= 128 && bytes.integer(start,4) == count &&
        bytes.integer(start+4,4) == 1, "Invalid skeletal YDD tables");
    CharacterModel model;
    model.name = name_at(bytes, strings, start, bytes.integer(40,4));
    for (unsigned i = 0; i < 16; ++i) {
        auto const value = bytes.real(start + 8 + 4*i);
        require(std::isfinite(value), "Invalid skeletal YDD basis");
        model.mesh_to_skeleton.elements[i/4][i%4] = value;
    }
    auto cursor = start + 72;
    std::uint64_t total_vertices = 0, total_indices = 0;
    for (std::uint64_t i = 0; i < count; ++i) {
        CharacterMesh part;
        part.mesh.name = name_at(bytes, strings, start, bytes.integer(cursor,4));
        part.mesh.material = name_at(bytes, strings, start, bytes.integer(cursor+4,4));
        validate_reference(part.mesh.material);
        // Legacy YDD imports embed materials/character_<id>.ymat@entry.
        // Canonical characters/<id>/models/<id>.ydd stores its YMAT alongside
        // the character. Rebase only when both paths match this convention.
        part.mesh.material=detail::rebase_embedded_character_material(
            path,part.mesh.material);
        auto const vertices = bytes.integer(cursor+8,4);
        auto const indices = bytes.integer(cursor+12,4);
        total_vertices += vertices;
        total_indices += indices;
        require(vertices > 0 && total_vertices <= 200000 && indices > 0 &&
            indices % 3 == 0 && total_indices <= 1000000 &&
            bytes.integer(cursor+40,4) == 1 && bytes.integer(cursor+44,4) == 48,
            "Unsupported skeletal YDD mesh part");
        cursor += 48;
        bytes.at(cursor, 80 * vertices + 4 * indices);
        auto const skin_start = cursor + 32 * vertices;
        for (std::uint64_t v = 0; v < vertices; ++v) {
            float values[8];
            for (unsigned c = 0; c < 8; ++c) {
                values[c] = bytes.real(cursor + v*32 + c*4);
                require(std::isfinite(values[c]) && std::abs(values[c]) <= 10000.f,
                    "Invalid skeletal mesh vertex");
            }
            part.mesh.vertices.push_back({{values[0],values[1],values[2]},
                {values[3],values[4],values[5]}, {values[6],values[7]}});
            SkinWeights skin{};
            float sum = 0;
            for (unsigned j = 0; j < 8; ++j) {
                auto const group = skin_start + v*48 + (j/4)*24;
                auto const joint = bytes.integer(group + (j%4)*2,2);
                auto const weight = bytes.real(group + 8 + (j%4)*4);
                require(joint < rig.joints.size() && weight >= 0 && weight <= 1,
                    "Invalid skeletal mesh influence");
                skin.joints[j] = static_cast<std::uint16_t>(joint);
                skin.weights[j] = weight;
                sum += weight;
            }
            require(std::abs(sum-1) < 0.01f, "Invalid skeletal mesh weight sum");
            for(auto& weight : skin.weights) weight /= sum;
            part.skin.push_back(skin);
        }
        cursor += vertices * 80;
        for (std::uint64_t j = 0; j < indices; ++j) {
            auto const index = bytes.integer(cursor + j*4,4);
            require(index < vertices, "Invalid skeletal mesh index");
            part.mesh.indices.push_back(static_cast<std::uint32_t>(index));
        }
        cursor += indices * 4;
        model.parts.push_back(std::move(part));
    }
    require(cursor == bytes.size() && total_vertices == bytes.integer(60,4) &&
        total_indices == bytes.integer(64,4), "Invalid skeletal YDD totals");
    return model;
}

std::vector<CharacterAppearance> load_character_appearance(AssetStore const& assets,
    CharacterModel const& model, std::string const& material, std::string const& textures)
{
    using namespace shared_assets::detail;
    auto const file = read_list_file(assets, material);
    require(file.kind == 30, "Expected a NorthStar v1 YMAT library");
    auto const json = parse_json(std::string(file.body.begin(), file.body.end()));
    auto const& materials = json.get_child("materials");
    require(json.get<std::string>("schema") == "northstar.ymat.v1" &&
        materials.size() == file.entries, "Invalid character YMAT library");
    struct Binding {std::string texture; float cutoff;};
    std::map<std::string, Binding> bindings;
    for (auto const& item : materials) {
        auto const& value = item.second;
        auto const name = value.get<std::string>("name");
        auto const blend = value.get<std::string>("blend");
        for (auto const& node : value.get_child("textures")) {
            if (node.second.get<std::string>("slot") != "base_color") continue;
            auto const reference = node.second.get<std::string>("ref");
            validate_reference(reference);
            auto const cutoff = value.get<float>("alpha_cutoff", blend == "masked" ? 0.5f :
                blend == "alpha" ? 0.15f : 0.f);
            require(cutoff >= 0.f && cutoff <= 1.f &&
                bindings.emplace(name,Binding{reference.substr(reference.find('@')+1),cutoff}).second,
                "Invalid character YMAT binding");
        }
    }
    auto dictionary = shared_assets::load_textures(assets, textures);
    std::vector<CharacterAppearance> result;
    for (auto const& part : model.parts) {
        auto const selector = part.mesh.material.find('@');
        auto const binding = bindings.find(part.mesh.material.substr(selector+1));
        require(material.size() >= selector && material.compare(material.size()-selector,selector,part.mesh.material,0,selector) == 0 && binding != bindings.end(),
            "Character material is missing");
        auto const found = std::find_if(dictionary.begin(), dictionary.end(), [&](auto const& image) {
            return image.name == binding->second.texture;
        });
        require(found != dictionary.end(), "Character albedo texture is missing");
        result.push_back({*found,binding->second.cutoff});
    }
    return result;
}
}
