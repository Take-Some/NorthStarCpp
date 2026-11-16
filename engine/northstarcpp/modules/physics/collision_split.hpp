// Deterministic bounded triangle-bucket convex decomposition.
// Each bucket is converted to its convex hull by the active physics provider.
// The splitsNum count is a maximum, not an exact guarantee.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace northstarcpp::physics_split {
struct Group {
    std::vector<std::uint32_t> vertices;
};
inline std::vector<Group> groups(const float* xyz,std::uint32_t verts,
                                  const std::uint32_t* indices,std::uint32_t count,
                                  std::uint32_t splits) {
    if(!xyz||!indices||verts<3||verts>18000||count<3||
       count>54000||count%3||splits<1||splits>16)
        throw std::invalid_argument("Invalid collision decomposition topology");
    std::array<float,3> lo{100000.f,100000.f,100000.f};
    std::array<float,3> hi{-100000.f,-100000.f,-100000.f};
    for(std::uint32_t i=0;i<verts;++i)
        for(int axis=0;axis<3;++axis) {
            float const v=xyz[3*i+axis];
            if(!std::isfinite(v)||std::abs(v)>100000.f)
                throw std::invalid_argument("Nonfinite collision vertex");
            lo[axis]=std::min(lo[axis],v);
            hi[axis]=std::max(hi[axis],v);
        }
    for(std::uint32_t i=0;i<count;++i)
        if(indices[i]>=verts)
            throw std::invalid_argument("Collision triangle index out of range");
    int axis=0;
    for(int i=1;i<3;++i)
        if(hi[i]-lo[i]>hi[axis]-lo[axis])axis=i;
    auto const extent=hi[axis]-lo[axis];
    std::vector<Group> bins(std::min(splits,count/3));
    std::vector<std::vector<unsigned char>> marks(
        bins.size(),std::vector<unsigned char>(verts));
    for(std::uint32_t t=0;t<count;t+=3) {
        float const centroid=(xyz[3*indices[t]+axis]+
           xyz[3*indices[t+1]+axis]+xyz[3*indices[t+2]+axis])/3.f;
        auto const f=extent>1.e-6f?(centroid-lo[axis])/extent:0.f;
        std::size_t bin=std::min(std::size_t(std::max(0.f,f)*bins.size()),
                                 bins.size()-1);
        for(int j=0;j<3;++j) {
            auto const vertex=indices[t+j];
            if(!marks[bin][vertex]) {
                marks[bin][vertex]=1;
                bins[bin].vertices.push_back(vertex);
            }
        }
    }
    // Never discard triangles that landed in a tiny/degenerate bucket.
    // Fall back to one full hull instead of silently leaving holes.
    for(auto const& bin:bins)
        if(!bin.vertices.empty()&&bin.vertices.size()<4) {
            Group whole;
            for(std::uint32_t i=0;i<verts;++i)whole.vertices.push_back(i);
            return {std::move(whole)};
        }
    bins.erase(std::remove_if(bins.begin(),bins.end(),
               [](Group const& g){return g.vertices.empty();}),bins.end());
    if(bins.empty()) {
        Group all;
        for(std::uint32_t i=0;i<verts;++i)all.vertices.push_back(i);
        bins.push_back(std::move(all));
    }
    return bins;
}
} // namespace northstarcpp::physics_split
