#pragma once

#include "renderer_private.hpp"
#include "../../shared/frame_memory_counters.hpp"
#include <cstring>
#include <utility>

namespace northstarcpp::render::vulkan {

inline bool stage_material_texture(MaterialSource& target,const MaterialSource* previous,
                                   unsigned slot,NorthStarMaterialTexture const& image)
{
    if(!image.rgba8)return !previous || previous->textures[slot].empty();
    target.widths[slot]=image.width;
    target.heights[slot]=image.height;
    if(previous && previous->widths[slot]==image.width &&
       previous->heights[slot]==image.height &&
       previous->textures[slot].size()==image.data_bytes &&
       std::memcmp(previous->textures[slot].data(),image.rgba8,image.data_bytes)==0)
        return true;

    diagnostics::frame_memory::assign(target.textures[slot],image.rgba8,
        image.rgba8+image.data_bytes,&NorthStarFrameMemoryCounters::texture_copy_bytes);
    return false;
}

inline void adopt_retained_textures(MaterialSource& target,MaterialSource& previous) noexcept
{
    // A validated nonempty slot with no staged bytes exactly matched
    // previous content. Transfer ownership only after the whole submission
    // validates, so later invalid batches cannot remove the active textures.
    for(unsigned slot=0;slot<target.textures.size();++slot)
        if(target.widths[slot] && target.textures[slot].empty())
            target.textures[slot].swap(previous.textures[slot]);
}

template<class Vertex>
bool matches_geometry(std::vector<Vertex> const& previous,Vertex const* data,
                      std::size_t count) noexcept
{
    return previous.size()==count &&
        std::memcmp(previous.data(),data,count*sizeof(Vertex))==0;
}

} // namespace northstarcpp::render::vulkan
