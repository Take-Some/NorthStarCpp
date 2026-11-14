#pragma once
#include <northstarcpp/skeleton.hpp>
#include <northstarcpp/shared_assets.hpp>

namespace northstarcpp {
struct SkinWeights {
    std::array<std::uint16_t, 8> joints;
    std::array<float, 8> weights;
};
struct CharacterMesh {
    shared_assets::MeshPart mesh;
    std::vector<SkinWeights> skin;
};
struct CharacterModel {
    std::string name;
    math::float4x4 mesh_to_skeleton;
    std::vector<CharacterMesh> parts;
};
CharacterModel load_character_model(AssetStore const&, std::string const&, Skeleton const&);
struct CharacterAppearance {
    shared_assets::Texture texture;
    float cutoff = 0;
};
std::vector<CharacterAppearance> load_character_appearance(AssetStore const&,
    CharacterModel const&, std::string const& material, std::string const& textures);
}
