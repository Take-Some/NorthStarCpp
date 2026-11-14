// NorthStar Editor native asset previews, independent of render plugin ABI.
#include "editor_asset_thumbnails.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
std::uint32_t texel(shared_assets::Texture const& t,int x,int y) {
    auto const& mip=t.mips.front();
    if(mip.width==0 || mip.height==0)return 0xff354253u;
    x=std::clamp(x,0,int(mip.width)-1);
    y=std::clamp(y,0,int(mip.height)-1);
    auto const* ptr=t.data.data()+mip.offset;
    if(t.format_id==1) {
        auto const offset=4u*(std::size_t(y)*mip.width+std::size_t(x));
        if(offset+4>mip.size)return 0xff354253u;
        auto const a=ptr[offset+3];
        return (std::uint32_t(a)<<24)|(std::uint32_t(ptr[offset])<<16)|
            (std::uint32_t(ptr[offset+1])<<8)|ptr[offset+2];
    }
    // NETD 101=BC1, 103=BC3 (BC1 color plus BC3 alpha block).
    if(t.format_id==101 || t.format_id==103) {
        std::size_t const stride=t.format_id==101?8u:16u;
        std::size_t const blocks=(mip.width+3u)/4u;
        std::size_t const base=(std::size_t(y/4)*blocks+std::size_t(x/4))*stride;
        if(base+stride>mip.size)return 0xff354253u;
        auto const* c=ptr+base+(t.format_id==103?8:0);
        auto read16=[](std::uint8_t const* p){return unsigned(p[0])|(unsigned(p[1])<<8);};
        unsigned const c0=read16(c),c1=read16(c+2);
        auto expand=[](unsigned v) {return std::array<int,3>{
            int(((v>>11)&31)*255/31),int(((v>>5)&63)*255/63),int((v&31)*255/31)};};
        auto color0=expand(c0),color1=expand(c1);
        int palette[4][3]{};
        for(int j=0;j<3;++j) {
            palette[0][j]=color0[j];palette[1][j]=color1[j];
            palette[2][j]=(2*color0[j]+color1[j])/3;
            palette[3][j]=(color0[j]+2*color1[j])/3;
            if(t.format_id==101 && c0<=c1) {
                palette[2][j]=(color0[j]+color1[j])/2;
                palette[3][j]=0;
            }
        }
        int const index=(y%4)*4+x%4;
        unsigned const packed=unsigned(c[4])|(unsigned(c[5])<<8)|
            (unsigned(c[6])<<16)|(unsigned(c[7])<<24);
        int const i=int((packed>>(2*index))&3u);
        int alpha=(t.format_id==101&&c0<=c1&&i==3)?0:255;
        if(t.format_id==103) {
            unsigned char const a0=ptr[base],a1=ptr[base+1];
            int table[8]{int(a0),int(a1)};
            if(a0>a1)
                for(int k=2;k<8;++k)
                    table[k]=((8-k)*int(a0)+(k-1)*int(a1))/7;
            else {
                for(int k=2;k<6;++k)
                    table[k]=((6-k)*int(a0)+(k-1)*int(a1))/5;
                table[6]=0;table[7]=255;
            }
            std::uint64_t codes=0;
            for(int k=0;k<6;++k)
                codes|=std::uint64_t(ptr[base+2+k])<<(8*k);
            alpha=table[(codes>>(3*index))&7u];
        }
        return (std::uint32_t(alpha)<<24)|
            (std::uint32_t(palette[i][0])<<16)|
            (std::uint32_t(palette[i][1])<<8)|std::uint32_t(palette[i][2]);
    }
    return 0xff354253u;
}
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
    if(t.mips.empty() || t.mips.front().width==0 ||
        t.mips.front().height==0 || t.mips.front().offset>t.data.size() ||
        t.mips.front().size>t.data.size()-t.mips.front().offset)
        return {};
    if(t.format_id!=1 && t.format_id!=101 && t.format_id!=103)return {};
    auto result=background();
    auto const& mip=t.mips.front();
    float const scale=std::min(float(N-8)/float(mip.width),
                               float(N-8)/float(mip.height));
    float const w=float(mip.width)*scale,h=float(mip.height)*scale;
    float const left=(float(N)-w)*.5f,top=(float(N)-h)*.5f;
    for(int y=0;y<N;++y)for(int x=0;x<N;++x) {
        float const u=(float(x)-left)/scale;
        float const v=(float(y)-top)/scale;
        if(u>=0.f&&v>=0.f&&u<float(mip.width)&&v<float(mip.height))
            blend(result,x,y,texel(t,int(u),int(v)));
    }
    return result;
}
Thumbnail thumbnail_material(shared_assets::Material const& m) {
    std::array<float,3> tint{.43f,.75f,.85f};
    for(auto const& param:m.parameters)
        if((param.name=="base_color" || param.name=="albedo")&&param.values.size()>=3) {
            tint={param.values[0],param.values[1],param.values[2]};
            break;
        }
    auto result=background();
    for(int y=0;y<N;++y)for(int x=0;x<N;++x) {
        float const dx=(float(x)-47.5f)/38.f;
        float const dy=(float(y)-47.5f)/38.f;
        float const radius=dx*dx+dy*dy;
        if(radius>1.f)continue;
        float const z=std::sqrt(std::max(0.f,1.f-radius));
        float const lit=std::clamp(.13f+.77f*(z*.81f-dx*.48f-dy*.34f),.07f,.95f);
        result.bgra[y*N+x]=pixel(int(tint[0]*255.f*lit),
            int(tint[1]*255.f*lit),int(tint[2]*255.f*lit));
    }
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
