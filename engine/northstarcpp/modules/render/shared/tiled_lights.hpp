#pragma once
// Conservative CPU tile classification for the native D3D12 deferred resolve.
// Each screen-space tile stores its candidate lights. No depth-based rejection:
// a partially visible sphere must never disappear behind a depth discontinuity.
#include <northstarcpp/render_lights_api.h>
#include <northstarcpp/render_plugin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace northstarcpp::render {
inline constexpr std::uint32_t light_tile_width = 32;
inline constexpr std::uint32_t max_lights_per_tile = 64;
inline constexpr std::uint32_t light_tile_stride = 1 + max_lights_per_tile;
inline constexpr std::uint32_t overflow_light_tile = 0xffffffffu;
struct TileLightGrid {
    std::uint32_t columns = 0, rows = 0, fallback_tiles = 0;
    // [count, light index * 64] for every tile.
    // overflow_light_tile means evaluate all dynamic lights, no missed light.
    std::vector<std::uint32_t> indices;
};
struct TileVec3 { double x=0,y=0,z=0; };
inline TileVec3 operator-(TileVec3 a,TileVec3 b) {return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline TileVec3 cross(TileVec3 a,TileVec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
inline double dot(TileVec3 a,TileVec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline TileVec3 normalized(TileVec3 v) {
    double const len=std::sqrt(dot(v,v));
    if(!(len>1.e-12)||!std::isfinite(len))
        throw std::invalid_argument("Invalid tile frustum ray");
    return {v.x/len,v.y/len,v.z/len};
}
inline TileVec3 unproject_ray(const std::array<float,16>& inverse_vp,
                             TileVec3 eye,double sx,double sy) {
    // Row-vector convention: mul(float4(NDC,1), inverse_vp).
    double const p[4]{sx,sy,1.,1.};
    double result[4]{};
    for(int axis=0;axis<4;++axis)
        for(int k=0;k<4;++k)
            result[axis]+=p[k]*static_cast<double>(inverse_vp[k*4+axis]);
    if(std::abs(result[3])<1.e-12||!std::isfinite(result[3]))
        throw std::invalid_argument("Cannot unproject tile");
    return normalized(TileVec3{result[0]/result[3],result[1]/result[3],
                               result[2]/result[3]}-eye);
}
inline TileLightGrid build_tile_lights(
    std::uint32_t width,std::uint32_t height,
    std::array<float,16> const& inverse_vp,
    std::array<float,3> const& camera_position,
    const NorthStarRenderLight* lights,std::uint32_t count) {
    if(!width||!height||width>16384||height>16384 ||
       count>NORTHSTARCPP_RENDER_MAX_DYNAMIC_LIGHTS ||
       (count&&!lights))
        throw std::invalid_argument("Invalid tiled lighting inputs");
    TileLightGrid grid;
    grid.columns=(width+light_tile_width-1)/light_tile_width;
    grid.rows=(height+light_tile_width-1)/light_tile_width;
    auto const total=std::uint64_t(grid.columns)*grid.rows*light_tile_stride;
    if(total>static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
        throw std::overflow_error("Tiled light index buffer overflow");
    grid.indices.resize(static_cast<std::size_t>(total),0u);
    if(!count)return grid;
    TileVec3 const eye{camera_position[0],camera_position[1],camera_position[2]};
    // Cache rays at the tile grid corners: avoid redundant inverse matrix
    // multiplication for every tile/light pair.
    std::vector<TileVec3> rays(static_cast<std::size_t>(grid.columns+1)*(grid.rows+1));
    for(std::uint32_t ty=0;ty<=grid.rows;++ty) {
        double y=1.-2.*std::min(ty*light_tile_width,height)/double(height);
        for(std::uint32_t tx=0;tx<=grid.columns;++tx) {
            double x=2.*std::min(tx*light_tile_width,width)/double(width)-1.;
            rays[static_cast<std::size_t>(ty)*(grid.columns+1)+tx]=
                unproject_ray(inverse_vp,eye,x,y);
        }
    }
    for(std::uint32_t ty=0;ty<grid.rows;++ty)
        for(std::uint32_t tx=0;tx<grid.columns;++tx) {
            std::size_t i=static_cast<std::size_t>(ty)*(grid.columns+1)+tx;
            auto const tl=rays[i],tr=rays[i+1];
            auto const bl=rays[i+grid.columns+1],br=rays[i+grid.columns+2];
            TileVec3 const center=normalized({tl.x+tr.x+br.x+bl.x,
                                              tl.y+tr.y+br.y+bl.y,
                                              tl.z+tr.z+br.z+bl.z});
            TileVec3 const sides[4]{
                normalized(cross(tl,tr)),normalized(cross(tr,br)),
                normalized(cross(br,bl)),normalized(cross(bl,tl))};
            auto const offset=(static_cast<std::size_t>(ty)*grid.columns+tx)*light_tile_stride;
            auto* slot=grid.indices.data()+offset;
            for(std::uint32_t l=0;l<count;++l) {
                auto const& light=lights[l];
                TileVec3 const center_to_light{
                    light.position_range[0]-eye.x,light.position_range[1]-eye.y,
                    light.position_range[2]-eye.z};
                double const radius=light.position_range[3];
                if(dot(center,center_to_light)<-radius)continue;
                bool outside=false;
                for(auto side:sides) {
                    // Side normals are oriented to the center ray.
                    if(dot(side,center)<0)side={-side.x,-side.y,-side.z};
                    if(dot(side,center_to_light)<-radius) {
                        outside=true;break;
                    }
                }
                if(outside)continue;
                if(slot[0]==max_lights_per_tile) {
                    slot[0]=overflow_light_tile;
                    ++grid.fallback_tiles;
                    break;
                }
                slot[1+slot[0]++]=l;
            }
        }
    return grid;
}
}
