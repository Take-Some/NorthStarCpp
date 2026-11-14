// NorthStar Editor native asset previews, independent of render plugin ABI.
#include "editor_asset_thumbnails.hpp"
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
namespace northstarcpp::editor_tools {
namespace {
constexpr int N=Thumbnail::size;
std::uint32_t pixel(int r,int g,int b) {
    return 0xff000000u | (std::uint32_t(std::clamp(r,0,255))<<16) |
        (std::uint32_t(std::clamp(g,0,255))<<8) | std::uint32_t(std::clamp(b,0,255));
}
Thumbnail background() {
    Thumbnail result;
    result.bgra.resize(N*N);
    for(int y=0;y<N;++y)for(int x=0;x<N;++x) {
        int const tile=((x/12)^(y/12))&1;
        result.bgra[y*N+x]=tile?0xff303843u:0xff242a34u;
    }
    return result;
}
// Decode only compressed blocks actually sampled by thumbnail pixels.
class TextureSampler {
public:
    explicit TextureSampler(shared_assets::Texture const& texture,
                            shared_assets::Mip const& mip)
        : texture_(texture), mip_(mip) {
        blocks_.reserve(256);
    }
    std::uint32_t at(int x,int y) {
        x=std::clamp(x,0,int(mip_.width)-1);
        y=std::clamp(y,0,int(mip_.height)-1);
        auto const* src=texture_.data.data()+mip_.offset;
        if(texture_.format_id==1) {
            auto const index=(std::size_t(y)*mip_.width+std::size_t(x))*4u;
            return pack(src+index);
        }
        std::size_t const blocks_per_row=(mip_.width+3u)/4u;
        auto const key=std::size_t(y/4)*blocks_per_row+std::size_t(x/4);
        auto it=blocks_.find(key);
        if(it==blocks_.end()) {
            std::array<std::uint8_t,64> decoded{};
            auto const stride=texture_.format_id==101 ||
                              texture_.format_id==102 ?8u:16u;
            auto const* source=src+key*stride;
            switch(texture_.format_id) {
            case 101: bcdec_bc1(source,decoded.data(),16);break;
            case 102: {
                // NETD 102 is single-channel BC4 (8 bytes/block), not BC2.
                std::array<std::uint8_t,16> channel{};
                bcdec_bc4(source,channel.data(),4);
                for(std::size_t i=0;i<channel.size();++i) {
                    decoded[i*4]=channel[i];
                    decoded[i*4+1]=channel[i];
                    decoded[i*4+2]=channel[i];
                    decoded[i*4+3]=255;
                }
                break;
            }
            case 103: bcdec_bc3(source,decoded.data(),16);break;
            case 104: bcdec_bc7(source,decoded.data(),16);break;
            default:return 0xff354253u;
            }
            it=blocks_.emplace(key,std::move(decoded)).first;
        }
        auto const offset=std::size_t((y%4)*4+(x%4))*4;
        return pack(it->second.data()+offset);
    }
private:
    static std::uint32_t pack(std::uint8_t const* rgba) {
        return (std::uint32_t(rgba[3])<<24) |
               (std::uint32_t(rgba[0])<<16) |
               (std::uint32_t(rgba[1])<<8) | rgba[2];
    }
    shared_assets::Texture const& texture_;
    shared_assets::Mip const& mip_;
    std::unordered_map<std::size_t,std::array<std::uint8_t,64>> blocks_;
};
void blend(Thumbnail& image,int x,int y,std::uint32_t p) {
    if(x<0||x>=N||y<0||y>=N)return;
    unsigned const alpha=(p>>24)&255;
    auto& dest=image.bgra[y*N+x];
    auto sample=[&](unsigned shift) {
        return ((p>>shift&255)*alpha+(dest>>shift&255)*(255-alpha))/255;
    };
    dest=pixel(int(sample(16)),int(sample(8)),int(sample(0)));
}
void line(Thumbnail& t,int x0,int y0,int x1,int y1,std::uint32_t color) {
    // Cap projected coordinates to avoid pathological work from corrupt geometry.
    x0=std::clamp(x0,-200,300);x1=std::clamp(x1,-200,300);
    y0=std::clamp(y0,-200,300);y1=std::clamp(y1,-200,300);
    int const dx=std::abs(x1-x0),sx=x0<x1?1:-1;
    int const dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    for(int steps=0;steps<1000;++steps) {
        blend(t,x0,y0,color);
        if(x0==x1&&y0==y1)break;
        int const twice=2*error;
        if(twice>=dy){error+=dy;x0+=sx;}
        if(twice<=dx){error+=dx;y0+=sy;}
    }
}
}
Thumbnail thumbnail_texture(shared_assets::Texture const& t) {
    if(t.mips.empty())return {};
    auto const& mip=t.mips.front();
    if(mip.width==0 || mip.height==0 || mip.width>16384 || mip.height>16384 ||
        mip.offset>t.data.size() || mip.size>t.data.size()-mip.offset)
        return {};
    std::size_t block_size=0;
    switch(t.format_id) {
    case 1: block_size=0;break;
    case 101: case 102:block_size=8;break;
    case 103: case 104:block_size=16;break;
    default:return {};
    }
    // Validate first mip before any block access.
    auto const expected=block_size ?
        std::size_t((mip.width+3u)/4u)*((mip.height+3u)/4u)*block_size :
        std::size_t(mip.width)*mip.height*4u;
    if(expected!=mip.size)return {};
    auto result=background();
    TextureSampler sampler(t,mip);
    float const scale=std::min(float(N-8)/float(mip.width),
                               float(N-8)/float(mip.height));
    float const w=float(mip.width)*scale,h=float(mip.height)*scale;
    float const left=(float(N)-w)*.5f,top=(float(N)-h)*.5f;
    for(int y=0;y<N;++y)for(int x=0;x<N;++x) {
        float const u=(float(x)-left)/scale;
        float const v=(float(y)-top)/scale;
        if(u>=0.f&&v>=0.f&&u<float(mip.width)&&v<float(mip.height))
            blend(result,x,y,sampler.at(int(u),int(v)));
    }
    return result;
}

// The modal uses real decoded pixels rather than scaling up the 96px thumbnail.
// Minification selects a lower native mip when supplied by the dictionary.
AssetPreviewImage preview_texture(shared_assets::Texture const& texture,
                                  int max_dimension,std::size_t requested_mip) {
    AssetPreviewImage output;
    if(requested_mip>=texture.mips.size() || max_dimension<64 ||
       max_dimension>4096)return output;
    std::size_t mip_index=requested_mip;
    // The requested mip is respected; resolution is capped by sampling
    // rather than inventing a nonexistent mip level.
    auto const& mip=texture.mips[mip_index];
    if(mip.width==0 || mip.height==0 ||
       mip.width>16384 || mip.height>16384 ||
       mip.offset>texture.data.size() ||
       mip.size>texture.data.size()-mip.offset)return output;
    std::size_t stride=0;
    switch(texture.format_id) {
    case 1: break;
    case 101: case 102:stride=8;break;
    case 103: case 104:stride=16;break;
    default:return output;
    }
    auto const expected=stride ?
        std::size_t((mip.width+3u)/4u)*((mip.height+3u)/4u)*stride :
        std::size_t(mip.width)*mip.height*4u;
    if(expected!=mip.size)return output;
    float const fit=std::min(1.f,float(max_dimension)/
                           float(std::max(mip.width,mip.height)));
    output.width=std::max(1,int(std::round(mip.width*fit)));
    output.height=std::max(1,int(std::round(mip.height*fit)));
    output.argb.resize(std::size_t(output.width)*output.height);
    TextureSampler sampler(texture,mip);
    // Smooth bilinear filtering in premultiplied-alpha space. This avoids
    // black halos around transparent sprites and jagged downscaled details.
    for(int y=0;y<output.height;++y)for(int x=0;x<output.width;++x) {
        float const u=(float(x)+.5f)*float(mip.width)/output.width-.5f;
        float const v=(float(y)+.5f)*float(mip.height)/output.height-.5f;
        int const x0=int(std::floor(u)), y0=int(std::floor(v));
        float const fx=std::clamp(u-float(x0),0.f,1.f);
        float const fy=std::clamp(v-float(y0),0.f,1.f);
        auto const samples=std::array<std::uint32_t,4>{
            sampler.at(x0,y0),sampler.at(x0+1,y0),
            sampler.at(x0,y0+1),sampler.at(x0+1,y0+1)};
        auto const weights=std::array<float,4>{
            (1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy};
        float alpha=0.f,red=0.f,green=0.f,blue=0.f;
        for(std::size_t k=0;k<4;++k) {
            auto const p=samples[k];
            float const a=float((p>>24)&255)/255.f;
            float const factor=weights[k]*a;
            alpha+=factor;
            red+=float((p>>16)&255)*factor;
            green+=float((p>>8)&255)*factor;
            blue+=float(p&255)*factor;
        }
        auto const clamped=std::clamp(alpha,0.f,1.f);
        if(alpha>1.e-5f) {
            red/=alpha;green/=alpha;blue/=alpha;
        }
        output.argb[std::size_t(y)*output.width+x]=
            (std::uint32_t(std::lround(clamped*255.f))<<24) |
            (std::uint32_t(std::clamp(int(std::lround(red)),0,255))<<16) |
            (std::uint32_t(std::clamp(int(std::lround(green)),0,255))<<8) |
            std::uint32_t(std::clamp(int(std::lround(blue)),0,255));
    }
    return output;
}
Thumbnail thumbnail_material(shared_assets::Material const& m) {
    auto const high=preview_material(m,{},N);
    if(!high.valid() || high.width!=N || high.height!=N)return {};
    Thumbnail result;
    result.bgra=high.argb;
    return result;
}
Thumbnail thumbnail_drawable(shared_assets::Drawable const& d) {
    auto result=background();
    float minx=std::numeric_limits<float>::max(),miny=minx,minz=minx;
    float maxx=-minx,maxy=-minx,maxz=-minx;
    std::size_t vertices=0;
    for(auto const& mesh:d.meshes)for(auto const& v:mesh.vertices) {
        if(vertices++>250000)break;
        auto const& p=v.position;
        minx=std::min(minx,p[0]);maxx=std::max(maxx,p[0]);
        miny=std::min(miny,p[1]);maxy=std::max(maxy,p[1]);
        minz=std::min(minz,p[2]);maxz=std::max(maxz,p[2]);
    }
    if(vertices==0 || !std::isfinite(minx) || !std::isfinite(maxx))return {};
    float const size=std::max({maxx-minx,maxy-miny,maxz-minz,.001f});
    float const cx=(maxx+minx)*.5f,cy=(maxy+miny)*.5f,cz=(maxz+minz)*.5f;
    auto project=[&](shared_assets::Vertex const& v) {
        float const x=(v.position[0]-cx)/size;
        float const y=(v.position[1]-cy)/size;
        float const z=(v.position[2]-cz)/size;
        return std::array<int,2>{int(48.f+67.f*(x-z)*.7071f),
                                  int(48.f+67.f*((x+z)*.4082f-y*.8165f))};
    };
    // Flat-shaded orthographic geometry preview with depth testing.
    // No GPU context or material shaders are required by the asset browser.
    struct ScreenVertex {float x,y,z;};
    auto projected=[&](shared_assets::Vertex const& vertex) {
        float const x=(vertex.position[0]-cx)/size;
        float const y=(vertex.position[1]-cy)/size;
        float const z=(vertex.position[2]-cz)/size;
        return ScreenVertex{48.f+67.f*(x-z)*.70710678f,
                            48.f+67.f*((x+z)*.4082483f-y*.8164966f),
                            (x+y+z)*.5773503f};
    };
    auto edge=[](ScreenVertex const& a,ScreenVertex const& b,
                 float px,float py) {
        return (px-a.x)*(b.y-a.y)-(py-a.y)*(b.x-a.x);
    };
    std::array<float,N*N> depth;
    depth.fill(-std::numeric_limits<float>::infinity());
    std::size_t filled=0;
    std::size_t mesh_number=0;
    for(auto const& mesh:d.meshes) {
        int const tint=int(mesh_number++ % 5u)*6;
        for(std::size_t i=0;i+2<mesh.indices.size() && filled<6000;i+=3,++filled) {
            auto const a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||
               c>=mesh.vertices.size())continue;
            auto const& wa=mesh.vertices[a].position;
            auto const& wb=mesh.vertices[b].position;
            auto const& wc=mesh.vertices[c].position;
            float const ex=wb[0]-wa[0],ey=wb[1]-wa[1],ez=wb[2]-wa[2];
            float const fx=wc[0]-wa[0],fy=wc[1]-wa[1],fz=wc[2]-wa[2];
            float const nx=ey*fz-ez*fy,ny=ez*fx-ex*fz,nz=ex*fy-ey*fx;
            float const nlen=std::sqrt(nx*nx+ny*ny+nz*nz);
            if(nlen<=1.e-10f || !std::isfinite(nlen))continue;
            float const light=std::clamp(
                .30f+.68f*std::abs((nx*.43f+ny*.81f+nz*.41f)/nlen),
                .25f,.98f);
            auto const color=pixel(int((75+tint)*light),
                int((158+tint)*light),int((206+tint)*light));
            auto const p0=projected(mesh.vertices[a]);
            auto const p1=projected(mesh.vertices[b]);
            auto const p2=projected(mesh.vertices[c]);
            float const area=edge(p0,p1,p2.x,p2.y);
            if(!std::isfinite(area) || std::abs(area)<1.e-5f)continue;
            int const x0=std::clamp(int(std::floor(std::min({p0.x,p1.x,p2.x}))),0,N-1);
            int const y0=std::clamp(int(std::floor(std::min({p0.y,p1.y,p2.y}))),0,N-1);
            int const x1=std::clamp(int(std::ceil(std::max({p0.x,p1.x,p2.x}))),0,N-1);
            int const y1=std::clamp(int(std::ceil(std::max({p0.y,p1.y,p2.y}))),0,N-1);
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                float const b0=edge(p1,p2,x+.5f,y+.5f)/area;
                float const b1=edge(p2,p0,x+.5f,y+.5f)/area;
                float const b2=1.f-b0-b1;
                if(b0<0.f || b1<0.f || b2<0.f)continue;
                float const z=b0*p0.z+b1*p1.z+b2*p2.z;
                auto const index=std::size_t(y)*N+x;
                if(z>depth[index]) {
                    depth[index]=z;
                    result.bgra[index]=color;
                }
            }
        }
        if(filled>=6000)break;
    }
    // Wireframe accent preserves mesh topology while the shaded surfaces
    // provide volume. The existing mesh triangle cap is retained.
    std::size_t faces=0;
    for(auto const& mesh:d.meshes) {
        for(std::size_t i=0;i+2<mesh.indices.size()&&faces<6000;i+=3,++faces) {
            auto const a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||
               c>=mesh.vertices.size())continue;
            auto const pa=project(mesh.vertices[a]),pb=project(mesh.vertices[b]),
                pc=project(mesh.vertices[c]);
            line(result,pa[0],pa[1],pb[0],pb[1],0xff82cee4u);
            line(result,pb[0],pb[1],pc[0],pc[1],0xff82cee4u);
            line(result,pc[0],pc[1],pa[0],pa[1],0xff82cee4u);
        }
        if(faces>=6000)break;
    }
    return result;
}
} // namespace northstarcpp::editor_tools
