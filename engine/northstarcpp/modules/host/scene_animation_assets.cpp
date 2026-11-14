// NorthStarCpp - AssetManager-backed animation clip loading
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#include <northstarcpp/scene_animation.hpp>
#include <northstarcpp/assets.hpp>
#include <stdexcept>

namespace northstarcpp {
SceneAnimationClip load_scene_animation_clip(AssetStore const& store,
                                            std::string const& virtual_path) {
    if (virtual_path.rfind("animations/",0)!=0 ||
        virtual_path.size()<=16 ||
        virtual_path.compare(virtual_path.size()-10,10,".anim.json")!=0)
        throw std::invalid_argument("Animation must use animations/*.anim.json");
    if (store.size(virtual_path)>1024*1024)
        throw std::invalid_argument("Scene animation source exceeds 1 MiB");
    auto const bytes=store.read(virtual_path);
    return parse_scene_animation_clip(std::string(bytes.begin(),bytes.end()));
}
} // namespace northstarcpp
