// Software 3D orbit preview for NEF8 YDD ListFile entries.
// CPU-only: no Vulkan device required to inspect a project asset.
// This is an editor preview, not a replacement for the runtime PBR renderer.
#include "editor_asset_thumbnails.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace northstarcpp::editor_tools {
namespace {
constexpr float pi=3.14159265358979323846f;
struct ViewVertex {float x,y,inv_depth,light;};
struct Rgb {int r,g,b;};

Rgb tint(std::size_t ordinal) {
    static constexpr std::array<Rgb,8> palette{{
        {102,166,210},{170,184,205},{145,177,165},{196,161,144},
        {127,165,213},{176,174,205},{174,191,154},{192,163,181}
    }};
    return palette[ordinal%palette.size()];
}
std::uint32_t color(Rgb c,float intensity) {
    auto ch=[&](int x) {
        return std::uint32_t(std::clamp(int(std::lround(x*intensity)),0,255));
    };
    return 0xff000000u|(ch(c.r)<<16)|(ch(c.g)<<8)|ch(c.b);
}
void line(AssetPreviewImage& out,int x0,int y0,int x1,int y1,
          std::uint32_t ink) {
    // Clip by bounded steps. The caller also normalizes world geometry.
    if(std::abs(x0-x1)>out.width*5||std::abs(y0-y1)>out.height*5)return;
    int const dx=std::abs(x1-x0),sx=x0<x1?1:-1;
    int const dy=-std::abs(y1-y0),sy=y0<y1?1:-1;
    int error=dx+dy;
    int const limit=out.width*5+out.height*5;
    for(int i=0;i<limit;++i) {
        if(x0>=0&&x0<out.width&&y0>=0&&y0<out.height)
            out.argb[std::size_t(y0)*out.width+x0]=ink;
        if(x0==x1&&y0==y1)break;
        int const both=2*error;
        if(both>=dy){error+=dy;x0+=sx;}
        if(both<=dx){error+=dx;y0+=sy;}
    }
}
}
AssetPreviewImage preview_drawable(shared_assets::Drawable const& drawable,
                                   int width,int height,float yaw,float pitch,
                                   float zoom,bool wireframe,
                                   std::vector<MaterialDefinition> const* materials,
                                   float key_light_intensity) {
    AssetPreviewImage out;
    if(width<64 || height<64 || width>1280 || height>1280 ||
       !std::isfinite(yaw) || !std::isfinite(pitch) ||
       !std::isfinite(zoom) || !std::isfinite(key_light_intensity) ||
       key_light_intensity<0.f || key_light_intensity>3.f)return out;
    out.width=width;out.height=height;
    out.argb.resize(std::size_t(width)*height);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        float const g=float(y)/std::max(1,height-1);
        float const vignette=1.f-.12f*std::pow(2.f*float(x)/width-1.f,2.f);
        out.argb[std::size_t(y)*width+x]=color({29,37,49},vignette*(1.14f-.28f*g));
    }
    std::array<float,3> minimum{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max()};
    std::array<float,3> maximum{-minimum[0],-minimum[1],-minimum[2]};
    std::size_t vertices=0;
    for(auto const& mesh:drawable.meshes) {
        for(auto const& vertex:mesh.vertices) {
            if(++vertices>1000000)break;
            for(int j=0;j<3;++j) {
                float const value=vertex.position[j];
                if(!std::isfinite(value))return {};
                minimum[j]=std::min(minimum[j],value);
                maximum[j]=std::max(maximum[j],value);
            }
        }
        if(vertices>1000000)break;
    }
    if(!vertices)return out;
    float const extent=std::max({maximum[0]-minimum[0],
        maximum[1]-minimum[1],maximum[2]-minimum[2],.0001f});
    std::array<float,3> center{
        (minimum[0]+maximum[0])*.5f,
        (minimum[1]+maximum[1])*.5f,
        (minimum[2]+maximum[2])*.5f};
    auto const sy=std::sin(yaw),cy=std::cos(yaw);
    auto const sp=std::sin(pitch),cp=std::cos(pitch);
    float const distance=std::clamp(2.25f*zoom,1.2f,12.f);
    auto rotate=[&](std::array<float,3> const& v) {
        float const x=v[0]*cy+v[2]*sy;
        float const z=-v[0]*sy+v[2]*cy;
        return std::array<float,3>{x,v[1]*cp-z*sp,v[1]*sp+z*cp};
    };
    auto project=[&](std::array<float,3> const& raw,
                     float shading) {
        std::array<float,3> normalized{
            (raw[0]-center[0])/extent,(raw[1]-center[1])/extent,
            (raw[2]-center[2])/extent};
        auto const v=rotate(normalized);
        float const denom=std::max(.10f,distance-v[2]);
        float const scale=float(std::min(width,height))*1.48f/denom;
        return ViewVertex{
            float(width)*.5f+v[0]*scale,
            float(height)*.51f-v[1]*scale,
            1.f/denom,shading};
    };
    // Ground plane. Grid is behind all positive-depth geometry.
    auto gridPoint=[&](float x,float z) {
        auto const world=std::array<float,3>{
            center[0]+x*extent,minimum[1]-.02f*extent,
            center[2]+z*extent};
        return project(world,0.f);
    };
    for(int n=-8;n<=8;++n) {
        float v=float(n)*.125f;
        auto a=gridPoint(v,-1.f),b=gridPoint(v,1.f);
        auto c=gridPoint(-1.f,v),d=gridPoint(1.f,v);
        auto const ink=n==0?0xff466179u:0xff354357u;
        line(out,int(a.x),int(a.y),int(b.x),int(b.y),ink);
        line(out,int(c.x),int(c.y),int(d.x),int(d.y),ink);
    }

    std::vector<float> depth(std::size_t(width)*height,
                             -std::numeric_limits<float>::infinity());
    auto edge=[](ViewVertex const& a,ViewVertex const& b,float x,float y) {
        return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);
    };
    std::size_t total_faces=0,mesh_number=0;
    for(auto const& mesh:drawable.meshes) {
        auto const part_index=mesh_number++;
        auto const* material=materials && part_index<materials->size()
            ? &materials->at(part_index) : nullptr;
        auto base=tint(part_index);
        if(material) {
            auto ch=[](float x) {
                return std::clamp(static_cast<int>(std::lround(
                    std::clamp(x,0.f,1.f)*255.f)),0,255);
            };
            base={ch(material->base_color[0]),
                  ch(material->base_color[1]),
                  ch(material->base_color[2])};
        }
        MaterialImage const* albedo=material ? &material->textures[0] : nullptr;
        bool const textured=albedo && albedo->width>0 && albedo->height>0 &&
            albedo->rgba8.size()==
                std::size_t(albedo->width)*albedo->height*4u;
        for(std::size_t i=0;i+2<mesh.indices.size();i+=3) {
            if(++total_faces>120000)break;
            auto a=mesh.indices[i],b=mesh.indices[i+1],c=mesh.indices[i+2];
            if(a>=mesh.vertices.size()||b>=mesh.vertices.size()||
               c>=mesh.vertices.size())continue;
            auto const& va=mesh.vertices[a];
            auto const& vb=mesh.vertices[b];
            auto const& vc=mesh.vertices[c];
            auto face_normal=[&]() {
                auto const& p0=va.position;
                auto const& p1=vb.position;
                auto const& p2=vc.position;
                float const ux=p1[0]-p0[0],uy=p1[1]-p0[1],uz=p1[2]-p0[2];
                float const vx=p2[0]-p0[0],vy=p2[1]-p0[1],vz=p2[2]-p0[2];
                auto normal=std::array<float,3>{
                    uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx};
                float norm=std::sqrt(normal[0]*normal[0]+
                    normal[1]*normal[1]+normal[2]*normal[2]);
                if(norm>1.e-12f)for(auto& v:normal)v/=norm;
                return normal;
            }();
            auto light=[&](shared_assets::Vertex const& vertex) {
                auto normal=vertex.normal;
                float length=std::sqrt(normal[0]*normal[0]+
                    normal[1]*normal[1]+normal[2]*normal[2]);
                if(length<.1f || !std::isfinite(length))
                    normal=face_normal;
                else for(auto& part:normal)part/=length;
                auto n=rotate(normal);
                float const directional=std::clamp(
                    n[0]*.36f+n[1]*.72f+n[2]*.45f,0.f,1.f);
                float const fill=std::max(0.f,-n[0]*.50f+n[1]*.30f);
                float const spec=std::pow(std::max(0.f,
                    n[0]*.15f+n[1]*.35f+n[2]*.93f),24.f);
                return std::clamp(.22f+key_light_intensity*
                    (.58f*directional+.15f*fill+.18f*spec),.13f,2.5f);
            };
            auto const p0=project(va.position,light(va));
            auto const p1=project(vb.position,light(vb));
            auto const p2=project(vc.position,light(vc));
            float const area=edge(p0,p1,p2.x,p2.y);
            if(!std::isfinite(area)||std::abs(area)<1.e-4f)continue;
            int x0=std::clamp(int(std::floor(std::min({p0.x,p1.x,p2.x}))),0,width-1);
            int x1=std::clamp(int(std::ceil(std::max({p0.x,p1.x,p2.x}))),0,width-1);
            int y0=std::clamp(int(std::floor(std::min({p0.y,p1.y,p2.y}))),0,height-1);
            int y1=std::clamp(int(std::ceil(std::max({p0.y,p1.y,p2.y}))),0,height-1);
            if(x0>x1||y0>y1)continue;
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
                float const u=edge(p1,p2,x+.5f,y+.5f)/area;
                float const v=edge(p2,p0,x+.5f,y+.5f)/area;
                float const w=1.f-u-v;
                if(u<0.f||v<0.f||w<0.f)continue;
                auto const index=std::size_t(y)*width+x;
                float const z=u*p0.inv_depth+v*p1.inv_depth+w*p2.inv_depth;
                if(z<=depth[index])continue;
                depth[index]=z;
                // Perspective-correct UV sampling uses the very same
                // decoded YMAT/YTD base-color image as the runtime material
                // pipeline. CPU preview does not execute PBR shaders.
                float const shading=u*p0.light+v*p1.light+w*p2.light;
                auto pixel_color=base;
                if(textured && z>1.e-8f) {
                    auto const a=u*p0.inv_depth/z;
                    auto const b=v*p1.inv_depth/z;
                    auto const c=w*p2.inv_depth/z;
                    auto const tex_u=a*va.uv[0]+b*vb.uv[0]+c*vc.uv[0];
                    auto const tex_v=a*va.uv[1]+b*vb.uv[1]+c*vc.uv[1];
                    if(std::isfinite(tex_u) && std::isfinite(tex_v)) {
                        auto wrap=[](float t) {return t-std::floor(t);};
                        auto const tx=std::min(albedo->width-1,
                            static_cast<std::uint32_t>(
                                wrap(tex_u)*albedo->width));
                        auto const ty=std::min(albedo->height-1,
                            static_cast<std::uint32_t>(
                                wrap(tex_v)*albedo->height));
                        auto const offset=(std::size_t(ty)*albedo->width+tx)*4u;
                        auto const& data=albedo->rgba8;
                        pixel_color={
                            static_cast<int>(data[offset])*base.r/255,
                            static_cast<int>(data[offset+1])*base.g/255,
                            static_cast<int>(data[offset+2])*base.b/255
                        };
                    }
                }
                out.argb[index]=color(pixel_color,shading);
            }
            if(wireframe) {
                line(out,int(p0.x),int(p0.y),int(p1.x),int(p1.y),0xff86d9f0u);
                line(out,int(p1.x),int(p1.y),int(p2.x),int(p2.y),0xff86d9f0u);
                line(out,int(p2.x),int(p2.y),int(p0.x),int(p0.y),0xff86d9f0u);
            }
        }
        if(total_faces>120000)break;
    }
    // If drawable contains only points/lines, show their positions as lit
    // markers. This still represents the actual geometry, not a type icon.
    if(total_faces==0) {
        for(auto const& mesh:drawable.meshes)
            for(auto const& vertex:mesh.vertices) {
                auto p=project(vertex.position,.8f);
                int const x=int(p.x),y=int(p.y);
                for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
                    if(x+dx>=0&&x+dx<width&&y+dy>=0&&y+dy<height)
                        out.argb[std::size_t(y+dy)*width+x+dx]=0xff72c5e8u;
            }
    }
    return out;
}
} // namespace northstarcpp::editor_tools
