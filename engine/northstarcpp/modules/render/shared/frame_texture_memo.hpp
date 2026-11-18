#pragma once
// Reuse a GPU texture lookup within ONE material submission only.
// Across submissions, content is revalidated; a producer can legally
// reuse the same memory address with edited pixels on the next frame.
#include <northstarcpp/render_materials_api.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>

namespace northstarcpp::render {
struct FrameTextureKey {
    const std::uint8_t* bytes=nullptr;
    std::uint32_t width=0,height=0,byte_count=0,color_space=0;
    bool operator==(FrameTextureKey const& rhs) const noexcept {
        return bytes==rhs.bytes && width==rhs.width &&
            height==rhs.height && byte_count==rhs.byte_count &&
            color_space==rhs.color_space;
    }
};
struct FrameTextureKeyHash {
    std::size_t operator()(FrameTextureKey const& key) const noexcept {
        auto seed=std::hash<const void*>{}(key.bytes);
        auto mix=[&](std::size_t x){
            seed^=x+std::size_t{0x9e3779b9}+(seed<<6)+(seed>>2);
        };
        mix(key.width);mix(key.height);mix(key.byte_count);
        mix(key.color_space);
        return seed;
    }
};
template<class GpuTexture>
class FrameTextureMemo {
    std::unordered_map<FrameTextureKey,GpuTexture,FrameTextureKeyHash> entries_;
public:
    template<class Resolve>
    GpuTexture get(NorthStarMaterialTexture const& image,
                   std::uint32_t color_space,Resolve&& resolve) {
        auto const key=FrameTextureKey{image.rgba8,image.width,image.height,
                                      image.data_bytes,color_space};
        if(auto it=entries_.find(key);it!=entries_.end())return it->second;
        auto result=std::forward<Resolve>(resolve)(); // may throw, no cache mutation
        entries_.emplace(key,result);
        return result;
    }
    std::size_t size() const noexcept {return entries_.size();}
};
} // namespace northstarcpp::render
