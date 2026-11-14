// NorthStar Material Preview: bounded CPU studio sphere with approximate
// physically based lighting and real NEF8 texture bindings. No Vulkan device.
#include "editor_asset_thumbnails.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace northstarcpp::editor_tools {
namespace {
constexpr float PI=3.14159265358979323846f;
struct V {
    float x{},y{},z{};
    V operator+(V rhs) const {return {x+rhs.x,y+rhs.y,z+rhs.z};}
    V operator-(V rhs) const {return {x-rhs.x,y-rhs.y,z-rhs.z};}
    V operator*(float f) const {return {x*f,y*f,z*f};}
    V operator/(float f) const {return {x/f,y/f,z/f};}
};
V mul(V a,V b) {return {a.x*b.x,a.y*b.y,a.z*b.z};}
float dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V normalized(V v){return v/std::sqrt(std::max(1.e-12f,dot(v,v)));}
float saturate(float x){return std::clamp(x,0.f,1.f);}
V mix(V a,V b,float t){return a*(1.f-t)+b*t;}
float parameter(shared_assets::Material const& mat,std::string const& name,
                float fallback) {
    for(auto const& p:mat.parameters)
        if(p.name==name && !p.values.empty() && std::isfinite(p.values[0]))
            return p.values[0];
    return fallback;
}
V color_parameter(shared_assets::Material const& mat,std::string const& name,
                  V fallback) {
    for(auto const& p:mat.parameters)
        if(p.name==name && p.values.size()>=3 &&
           std::isfinite(p.values[0]) && std::isfinite(p.values[1]) &&
           std::isfinite(p.values[2]))
            return {saturate(p.values[0]),saturate(p.values[1]),saturate(p.values[2])};
    return fallback;
}
std::uint32_t pack(V rgb,float alpha=1.f) {
    auto channel=[](float v)->std::uint32_t {
        // Linear -> display sRGB approximation
        return std::uint32_t(std::lround(
            std::pow(saturate(v),1.f/2.2f)*255.f));
    };
    return (std::uint32_t(std::lround(saturate(alpha)*255.f))<<24) |
        (channel(rgb.x)<<16)|(channel(rgb.y)<<8)|channel(rgb.z);
}
struct Image {
    int width{},height{};
    std::vector<std::uint32_t> pixels;
    bool srgb=false;
};
Image decode(shared_assets::Texture const& texture) {
    auto bitmap=preview_texture(texture,1024);
    if(!bitmap.valid())return {};
    return {bitmap.width,bitmap.height,std::move(bitmap.argb),texture.srgb};
}
float sampleLinearColor(std::uint8_t channel,bool srgb) {
    float const encoded=float(channel)/255.f;
    if(!srgb)return encoded;
    // IEC 61966-2-1 inverse transfer, not a universal pow(2.2).
    return encoded<=0.04045f?encoded/12.92f:
           std::pow((encoded+0.055f)/1.055f,2.4f);
}
struct MaterialTexture {
    std::string slot;
    Image data;
};
V sample(Image const& image,float u,float v) {
    if(image.pixels.empty() || image.width<1 || image.height<1)
        return {1.f,1.f,1.f};
    // Repeat. Texture bindings use the same UV on the sphere.
    u-=std::floor(u);v-=std::floor(v);
    auto const x=std::clamp(int(u*image.width),0,image.width-1);
    auto const y=std::clamp(int(v*image.height),0,image.height-1);
    auto const p=image.pixels[std::size_t(y)*image.width+x];
    // Source NETD color-space metadata, not the material slot name,
    // decides whether GPU sampling applies an sRGB-to-linear transfer.
    return {sampleLinearColor(std::uint8_t((p>>16)&255),image.srgb),
            sampleLinearColor(std::uint8_t((p>>8)&255),image.srgb),
            sampleLinearColor(std::uint8_t(p&255),image.srgb)};
}
float sampleChannel(Image const& image,float u,float v) {
    if(image.pixels.empty())return 1.f;
    u-=std::floor(u);v-=std::floor(v);
    auto const x=std::clamp(int(u*image.width),0,image.width-1);
    auto const y=std::clamp(int(v*image.height),0,image.height-1);
    auto const p=image.pixels[std::size_t(y)*image.width+x];
    return sampleLinearColor(std::uint8_t((p>>16)&255),image.srgb);
}
V normalSample(Image const& image,float u,float v,float strength,
               V surface,V tangent,V bitangent) {
    if(image.pixels.empty() || strength<1.e-5f)return surface;
    u-=std::floor(u);v-=std::floor(v);
    int x=std::clamp(int(u*image.width),0,image.width-1);
    int y=std::clamp(int(v*image.height),0,image.height-1);
    auto p=image.pixels[std::size_t(y)*image.width+x];
    float nx=(float((p>>16)&255)/255.f*2.f-1.f)*strength;
    float ny=(float((p>>8)&255)/255.f*2.f-1.f)*strength;
    float nz=std::sqrt(std::max(.01f,1.f-nx*nx-ny*ny));
    return normalized(surface*nz+tangent*nx+bitangent*ny);
}
} // namespace

AssetPreviewImage preview_material(
    shared_assets::Material const& material,
    std::vector<shared_assets::ResolvedAsset<shared_assets::Texture>> const& textures,
    int dimension,float yaw,float pitch,float zoom) {
    AssetPreviewImage out;
    if(dimension<64 || dimension>1024 ||
       !std::isfinite(yaw) || !std::isfinite(pitch) ||
       !std::isfinite(zoom) || zoom<.25f || zoom>8.f)return out;
    out.width=dimension;out.height=dimension;
    out.argb.resize(std::size_t(dimension)*dimension);
    auto const baseParam=color_parameter(material,"base_color",
                          color_parameter(material,"albedo",{.53f,.65f,.72f}));
    auto const emissive=color_parameter(material,"emissive",{0.f,0.f,0.f});
    auto const roughParam=std::clamp(parameter(material,"roughness",.55f),.045f,1.f);
    auto const metalParam=saturate(parameter(material,"metallic",
                              parameter(material,"metalness",0.f)));
    auto const emissiveStrength=std::clamp(
        parameter(material,"emissive_strength",1.f),0.f,8.f);
    auto const normalScale=std::clamp(parameter(material,"normal_scale",1.f),0.f,2.f);
    // Load only recognized bindings. Names are validated by the NEF8 parser;
    // unknown slots are preserved as metadata, not interpreted as albedo.
    std::map<std::string,Image> bound;
    for(auto const& binding:material.textures) {
        if(bound.find(binding.slot)!=bound.end())continue;
        auto const found=std::find_if(textures.begin(),textures.end(),
            [&](auto const& asset){return asset.reference==binding.reference;});
        if(found!=textures.end()) {
            auto decoded=decode(found->asset);
            if(!decoded.pixels.empty())bound.emplace(binding.slot,std::move(decoded));
        }
    }
    auto image=[&](std::string const& slot)->Image const* {
        auto at=bound.find(slot);
        return at==bound.end()?nullptr:&at->second;
    };
    auto const baseImage=image("base_color");
    auto const roughImage=image("roughness");
    auto const metalImage=image("metallic");
    auto const normalImage=image("normal");
    auto const emissiveImage=image("emissive");
    float const cy=std::cos(yaw),sy=std::sin(yaw);
    float const cp=std::cos(pitch),sp=std::sin(pitch);
    V const key=normalized({-.53f,.70f,.55f});
    V const fill=normalized({.80f,.14f,.58f});
    V const view={0.f,0.f,1.f};
    auto environment=[&](V n,float r,float metallic) {
        float const stripe=std::pow(saturate(n.x*.60f+n.y*.8f+.30f),
                                    12.f/(r+.14f));
        float const softbox=std::pow(saturate(-n.x*.3f+n.y*.8f+n.z*.22f),
                                     8.f/(r+.2f));
        return V{.015f,.024f,.035f}*(1.f-metallic)+
               V{.17f,.24f,.32f}*(stripe*.36f+softbox*.19f);
    };
    for(int y=0;y<dimension;++y)for(int x=0;x<dimension;++x) {
        float const fx=(float(x)+.5f)/dimension;
        float const fy=(float(y)+.5f)/dimension;
        float const sphereRadius=.405f*zoom;
        float const dx=(fx-.5f)/sphereRadius,dy=(.5f-fy)/sphereRadius;
        auto const radius=dx*dx+dy*dy;
        auto const idx=std::size_t(y)*dimension+x;
        // Studio background: non-uniform neutral backdrop and a fine ground.
        float const bg=.042f+.032f*fy;
        V backdrop{bg*.79f,bg*.93f,bg*1.12f};
        float const vignette=saturate(1.f-.42f*(
            (fx-.5f)*(fx-.5f)+(fy-.5f)*(fy-.5f)));
        backdrop=backdrop*vignette;
        if(fy>.86f) {
            int tile=(int(fx*dimension/22)^int(fy*dimension/22))&1;
            backdrop=backdrop*(tile?.94f:1.05f);
        }
        if(radius>1.f) {
            // Soft dark contact shadow.
            float const sx=(fx-.5f)/.47f,syy=(fy-.88f)/.065f;
            float shadow=std::exp(-2.7f*(sx*sx+syy*syy));
            out.argb[idx]=pack(backdrop*(1.f-.53f*shadow));
            continue;
        }
        auto const n=V{dx,dy,std::sqrt(std::max(0.f,1.f-radius))};
        // Rotate surface UV independently of studio light direction.
        auto const rotated=V{
            cy*n.x+sy*n.z,
            cp*n.y-sp*(-sy*n.x+cy*n.z),
            sp*n.y+cp*(-sy*n.x+cy*n.z)};
        float const uvx=.5f+std::atan2(rotated.z,rotated.x)/(2.f*PI);
        float const uvy=.5f-std::asin(std::clamp(rotated.y,-1.f,1.f))/PI;
        auto const tangent=normalized(V{-rotated.z,0.f,rotated.x});
        auto const bitangent=normalized(V{
            -rotated.x*rotated.y,rotated.x*rotated.x+rotated.z*rotated.z,
            -rotated.z*rotated.y});
        V shaded=n;
        if(normalImage) {
            // Bump basis follows sphere orientation while lighting stays
            // fixed in camera space.
            auto bumped=normalSample(*normalImage,uvx,uvy,normalScale,
                                      rotated,tangent,bitangent);
            V unrotated{
                cy*bumped.x-sy*(cp*bumped.z-sp*bumped.y),
                cp*bumped.y+sp*bumped.z,
                sy*bumped.x+cy*(cp*bumped.z-sp*bumped.y)};
            shaded=normalized(unrotated);
        }
        V albedo=baseParam;
        if(baseImage)albedo=mul(albedo,sample(*baseImage,uvx,uvy));
        auto const r=std::clamp(roughParam*
            (roughImage?sampleChannel(*roughImage,uvx,uvy):1.f),.045f,1.f);
        auto const metallic=saturate(metalParam*
            (metalImage?sampleChannel(*metalImage,uvx,uvy):1.f));
        auto const f0=mix(V{.04f,.04f,.04f},albedo,metallic);
        auto const alpha=r*r, alpha2=alpha*alpha;
        V final=mul(albedo,{.075f,.086f,.115f})*(1.f-metallic);
        V const lights[2]={key,fill};
        V const radiances[2]={{2.8f,2.70f,2.52f},{.52f,.67f,.92f}};
        for(int l=0;l<2;++l) {
            auto const light=lights[l];
            float const NdotL=saturate(dot(shaded,light));
            float const NdotV=saturate(dot(shaded,view));
            if(NdotL<=0.f||NdotV<=0.f)continue;
            V const h=normalized(view+light);
            float const NdotH=saturate(dot(shaded,h));
            float const VdotH=saturate(dot(view,h));
            float const denom=NdotH*NdotH*(alpha2-1.f)+1.f;
            float const d=alpha2/(PI*std::max(denom*denom,.00001f));
            auto smith=[&](float nd) {
                float const k=(r+1.f)*(r+1.f)/8.f;
                return nd/(nd*(1.f-k)+k);
            };
            float const g=smith(NdotL)*smith(NdotV);
            float const fresnel=std::pow(1.f-VdotH,5.f);
            V const F=f0+(V{1.f,1.f,1.f}-f0)*fresnel;
            V const kd=(V{1.f,1.f,1.f}-F)*(1.f-metallic);
            V const spec=F*(d*g/std::max(.0001f,4.f*NdotL*NdotV));
            V const diffuse=mul(kd,albedo)*(1.f/PI);
            final=final+mul(diffuse+spec,radiances[l])*NdotL;
        }
        final=final+environment(shaded,r,metallic);
        V glow=emissive*emissiveStrength;
        if(emissiveImage)glow=mul(glow,sample(*emissiveImage,uvx,uvy));
        final=final+glow;
        final=final/(1.f+.35f*std::max({final.x,final.y,final.z}));
        // Antialiased sphere silhouette and alpha-masked materials.
        float const edge=saturate((1.f-std::sqrt(radius))*dimension*.23f);
        float coverage=edge;
        if(material.blend=="masked" || material.alpha_cutoff>0.f) {
            if(baseImage) {
                auto const img=*baseImage;
                int tx=std::clamp(int((uvx-std::floor(uvx))*img.width),0,img.width-1);
                int ty=std::clamp(int((uvy-std::floor(uvy))*img.height),0,img.height-1);
                float a=float(img.pixels[std::size_t(ty)*img.width+tx]>>24)/255.f;
                if(a<material.alpha_cutoff)coverage=0.f;
            }
        }
        out.argb[idx]=pack(final*coverage+backdrop*(1.f-coverage));
    }
    return out;
}
} // namespace northstarcpp::editor_tools
