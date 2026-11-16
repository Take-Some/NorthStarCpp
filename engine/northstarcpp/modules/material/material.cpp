// NorthStarCpp 2026 — first-class engine.material authoring asset loader.
// JSON descriptors and native YMAT/YTD resolve through the current VFS.
#include <northstarcpp/material.hpp>
#include "../models/character_asset_namespace.hpp"
#include <northstarcpp/assets.hpp>
#include <northstarcpp/shared_assets.hpp>
#include <boost/property_tree/json_parser.hpp>
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <objbase.h>
#endif

namespace northstarcpp {
namespace {
bool suffix(std::string const& text,std::string const& end) {
    return text.size()>=end.size() &&
        text.compare(text.size()-end.size(),end.size(),end)==0;
}
bool safe(std::string const& path) {
    if(path.empty() || path.size()>1024 || path.front()=='/' ||
       path.find('\\')!=std::string::npos || path.find(':')!=std::string::npos ||
       path.find("../")!=std::string::npos || path.find("/..")!=std::string::npos ||
       path.find("//")!=std::string::npos || path.find('\0')!=std::string::npos)
        return false;
    return true;
}
float unit(float value) {
    if(!std::isfinite(value) || value<0.f || value>1.f)
        throw std::invalid_argument("Material factor must be finite and within [0,1]");
    return value;
}
float srgb(float x) {
    return x<=0.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
}
std::array<float,4> parse_hex(std::string const& code) {
    if(code.size()!=7 || code[0]!='#')
        throw std::invalid_argument("Invalid Base Color hex");
    auto digit=[](char c)->unsigned {
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='A'&&c<='F')return unsigned(c-'A'+10);
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        throw std::invalid_argument("Invalid Base Color channel");
    };
    auto channel=[&](int i) {return srgb(float((digit(code[i])<<4)+digit(code[i+1]))/255.f);};
    return {channel(1),channel(3),channel(5),1.f};
}
MaterialImage image_from_native(shared_assets::Texture const& texture) {
    if(texture.mips.empty())throw std::runtime_error("Texture has no mip");
    auto const& mip=texture.mips.front();
    if(!mip.width || !mip.height || mip.width>4096 || mip.height>4096 ||
       mip.offset>texture.data.size() ||
       mip.size>texture.data.size()-mip.offset)
        throw std::invalid_argument("Invalid or oversized texture mip");
    MaterialImage out;
    out.width=mip.width;out.height=mip.height;
    out.rgba8.resize(std::size_t(mip.width)*mip.height*4);
    auto const* data=texture.data.data()+mip.offset;
    if(texture.format_id==1 || texture.format_id==2) {
        if(mip.size!=out.rgba8.size())
            throw std::invalid_argument("Invalid RGBA texture payload");
        std::memcpy(out.rgba8.data(),data,mip.size);
        return out;
    }
    if(texture.format_id<101 || texture.format_id>105)
        throw std::invalid_argument("Unsupported native texture format");
    unsigned const blocks_x=(mip.width+3)/4,blocks_y=(mip.height+3)/4;
    unsigned const block_bytes=texture.format_id<=102?8:16;
    if(mip.size<std::size_t(blocks_x)*blocks_y*block_bytes)
        throw std::invalid_argument("Compressed mip truncated");
    for(unsigned by=0;by<blocks_y;++by)for(unsigned bx=0;bx<blocks_x;++bx) {
        auto const* block=data+(std::size_t(by)*blocks_x+bx)*block_bytes;
        std::array<std::uint8_t,64> decoded{};
        if(texture.format_id==101)bcdec_bc1(block,decoded.data(),16);
        else if(texture.format_id==103)bcdec_bc3(block,decoded.data(),16);
        else if(texture.format_id==104)bcdec_bc7(block,decoded.data(),16);
        else if(texture.format_id==102) {
            std::array<std::uint8_t,16> grey{};
            bcdec_bc4(block,grey.data(),4);
            for(unsigned i=0;i<16;++i) {
                decoded[4*i+0]=grey[i];
                decoded[4*i+1]=grey[i];
                decoded[4*i+2]=grey[i];
                decoded[4*i+3]=255;
            }
        }else {
            std::array<std::uint8_t,32> channels{};
            bcdec_bc5(block,channels.data(),8);
            for(unsigned i=0;i<16;++i) {
                decoded[4*i+0]=channels[(i/4)*8+(i%4)*2];
                decoded[4*i+1]=channels[(i/4)*8+(i%4)*2+1];
                decoded[4*i+2]=255;
                decoded[4*i+3]=255;
            }
        }
        for(unsigned y=0;y<4 && by*4+y<mip.height;++y)
            for(unsigned x=0;x<4 && bx*4+x<mip.width;++x)
                std::memcpy(out.rgba8.data()+
                    (std::size_t(by*4+y)*mip.width+(bx*4+x))*4,
                    decoded.data()+(y*4+x)*4,4);
    }
    return out;
}
#ifdef _WIN32
template<typename T> struct Com {
    T* ptr=nullptr;
    ~Com(){if(ptr)ptr->Release();}
    T** out(){return &ptr;}
    T* operator->()const{return ptr;}
};
MaterialImage decode_image_file(std::vector<std::uint8_t>& bytes) {
    if(bytes.empty()||bytes.size()>32*1024*1024)
        throw std::invalid_argument("Image asset size out of bounds");
    HRESULT const init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(init)&&init!=RPC_E_CHANGED_MODE)
        throw std::runtime_error("Cannot initialize image codec apartment");
    struct Clean {bool yes;~Clean(){if(yes)CoUninitialize();}} cleanup{SUCCEEDED(init)};
    Com<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
              IID_PPV_ARGS(factory.out()))))
        throw std::runtime_error("Windows Imaging Component unavailable");
    Com<IWICStream> stream;
    if(FAILED(factory->CreateStream(stream.out()))||
       FAILED(stream->InitializeFromMemory(bytes.data(),static_cast<DWORD>(bytes.size()))))
        throw std::runtime_error("Cannot open image input stream");
    Com<IWICBitmapDecoder> decoder;
    if(FAILED(factory->CreateDecoderFromStream(stream.ptr,nullptr,
              WICDecodeMetadataCacheOnDemand,decoder.out())))
        throw std::runtime_error("Image is not a supported PNG/JPEG/etc resource");
    Com<IWICBitmapFrameDecode> frame;
    if(FAILED(decoder->GetFrame(0,frame.out())))
        throw std::runtime_error("Cannot decode image frame");
    Com<IWICFormatConverter> convert;
    if(FAILED(factory->CreateFormatConverter(convert.out()))||
       FAILED(convert->Initialize(frame.ptr,GUID_WICPixelFormat32bppRGBA,
              WICBitmapDitherTypeNone,nullptr,0.f,WICBitmapPaletteTypeCustom)))
        throw std::runtime_error("Cannot convert image to RGBA8");
    UINT w=0,h=0;
    if(FAILED(convert->GetSize(&w,&h))||!w||!h||w>4096||h>4096)
        throw std::invalid_argument("Unsupported material texture size");
    MaterialImage result;
    result.width=w;result.height=h;result.rgba8.resize(std::size_t(w)*h*4);
    if(FAILED(convert->CopyPixels(nullptr,w*4,static_cast<UINT>(result.rgba8.size()),
                                  result.rgba8.data())))
        throw std::runtime_error("Cannot read RGBA8 image");
    return result;
}
#endif
struct MaterialLoadCache {
    std::unordered_map<std::string,std::vector<shared_assets::Texture>> ytd;
    std::size_t encoded_bytes=0;
};
// Optional transaction-local cache; no process-global asset state.
MaterialImage load_texture(AssetStore const& assets,std::string const& path,
                           MaterialLoadCache* cache=nullptr) {
    if(!safe(path))throw std::invalid_argument("Unsafe texture reference");
    if(path.find(".ytd")!=std::string::npos) {
        auto const at=path.find('@');
        auto const dict=at==std::string::npos?path:path.substr(0,at);
        std::vector<shared_assets::Texture> decoded;
        std::vector<shared_assets::Texture> const* list=nullptr;
        if(cache) {
            auto found=cache->ytd.find(dict);
            if(found!=cache->ytd.end())list=&found->second;
        }
        if(!list) {
            decoded=shared_assets::load_textures(assets,dict);
            if(cache && cache->ytd.size()<8) {
                std::size_t bytes=0;
                for(auto const& tex:decoded)bytes+=tex.data.size();
                // A large native YTD can expand to hundreds of MiB.
                // Cache within one material-loading transaction, never
                // globally; the bounded 384-MiB budget includes BCn payloads.
                constexpr std::size_t limit=384ull*1024ull*1024ull;
                if(bytes<=limit && cache->encoded_bytes<=limit-bytes) {
                    cache->encoded_bytes+=bytes;
                    list=&cache->ytd.emplace(dict,std::move(decoded)).first->second;
                }
            }
            if(!list)list=&decoded;
        }
        if(list->empty())throw std::runtime_error("Texture dictionary contains no entries");
        if(at!=std::string::npos) {
            auto const name=path.substr(at+1);
            auto it=std::find_if(list->begin(),list->end(),[&](auto const& entry){
                return entry.name==name;
            });
            if(it==list->end())throw std::runtime_error("Texture dictionary entry not found");
            return image_from_native(*it);
        }
        return image_from_native(list->front());
    }
#ifdef _WIN32
    if(assets.size(path)>32*1024*1024)
        throw std::invalid_argument("Material image exceeds 32 MiB");
    auto bytes=assets.read(path);
    return decode_image_file(bytes);
#else
    throw std::runtime_error("Raster texture decoding provider unavailable on this platform");
#endif
}
unsigned binding(std::string const& name) {
    if(name=="base_color"||name=="albedo"||name=="diffuse"||name=="base_color_texture")return 0;
    if(name=="normal"||name=="normal_texture")return 1;
    if(name=="roughness"||name=="roughness_texture")return 2;
    if(name=="metallic"||name=="metallic_texture")return 3;
    if(name=="emissive"||name=="emissive_texture")return 4;
    return 5;
}
}
namespace {
MaterialDefinition load_material_recursive(AssetStore const& assets,
    std::string const& reference,std::unordered_set<std::string>& active,
    unsigned depth,MaterialLoadCache* cache)
{
    if(depth>8 || !active.insert(reference).second)
        throw std::invalid_argument("Material instance inheritance cycle or depth exceeded");
    struct Pop {
        std::unordered_set<std::string>& entries;
        std::string key;
        ~Pop(){entries.erase(key);}
    } pop{active,reference};
    bool const instance=suffix(reference,".material-instance.json");
    auto const native_entry=reference.find(".ymat@");
    bool const authored=suffix(reference,".material.json");
    bool const compiled=suffix(reference,".ymat") ||
        (native_entry!=std::string::npos && native_entry+6<reference.size());
    if(!safe(reference) || !(instance||authored||compiled))
        throw std::invalid_argument("Not a supported material or instance reference");
    MaterialDefinition result;
    std::array<std::string,5> refs{};
    std::array<const char*,5> keys{"base_color_texture","normal_texture",
        "roughness_texture","metallic_texture","emissive_texture"};
    if(instance||authored) {
        if(assets.size(reference)>512*1024)
            throw std::invalid_argument("Material descriptor exceeds 512 KiB");
        auto const bytes=assets.read(reference);
        std::istringstream stream(std::string(bytes.begin(),bytes.end()));
        boost::property_tree::ptree doc;
        boost::property_tree::read_json(stream,doc);
        if(doc.get<unsigned>("schema")!=1)
            throw std::invalid_argument("Unsupported material schema");
        boost::property_tree::ptree const* attributes=&doc;
        if(instance) {
            std::string parent=doc.get<std::string>("parent");
            if(!safe(parent)||parent==reference)
                throw std::invalid_argument("Unsafe Material Instance parent");
            result=load_material_recursive(assets,parent,active,depth+1,cache);
            auto over=doc.get_child_optional("overrides");
            if(over)attributes=&*over;
        }
        auto const& props=*attributes;
        if(auto shader=props.get_optional<std::string>("shader")) {
            if(*shader!="standard_lit"&&*shader!="unlit")
                throw std::invalid_argument("Unsupported material shader");
            result.unlit=*shader=="unlit";
        }else if(!instance)
            throw std::invalid_argument("Material shader must be declared");
        if(auto value=props.get_optional<std::string>("base_color"))
            result.base_color=parse_hex(*value);
        else if(!instance)
            throw std::invalid_argument("Material Base Color must be declared");
        if(auto rough=props.get_optional<float>("roughness"))
            result.roughness=unit(*rough);
        else if(!instance)
            throw std::invalid_argument("Material roughness must be declared");
        if(auto metal=props.get_optional<float>("metallic"))
            result.metallic=unit(*metal);
        else if(!instance)
            throw std::invalid_argument("Material metallic must be declared");
        if(auto opacity=props.get_optional<float>("opacity"))
            result.opacity=unit(*opacity);
        if(auto cutoff=props.get_optional<float>("alpha_cutoff"))
            result.alpha_cutoff=unit(*cutoff);
        if(auto mode=props.get_optional<std::string>("blend_mode")) {
            if(*mode=="opaque")result.blend_mode=0;
            else if(*mode=="masked")result.blend_mode=1;
            else if(*mode=="translucent")result.blend_mode=2;
            else throw std::invalid_argument("Unsupported Material blend mode");
        }
        // Inherited parent GPU images remain unless an explicit override
        // replaces or clears one. No duplicate VFS loads for unchanged slots.
        for(unsigned i=0;i<keys.size();++i)
            if(auto ref=props.get_optional<std::string>(keys[i])) {
                refs[i]=*ref;
                result.textures[i]=MaterialImage{};
            }
        // Authoring a graph explicitly replaces the inherited graph.
        if(auto graph=props.get_child_optional("graph")) {
            std::ostringstream json;
            boost::property_tree::write_json(json,*graph,false);
            result.graph_shader_defines=compile_material_graph_with_functions(
                json.str(),[&assets](std::string const& function_path) {
                    if(assets.size(function_path)>32768)
                        throw std::invalid_argument(
                            "Material Function asset exceeds 32 KiB");
                    auto bytes=assets.read(function_path);
                    return std::string(bytes.begin(),bytes.end());
                });
        }
    }else {
        shared_assets::Material native;
        if(native_entry!=std::string::npos)
            native=assets.load_material(reference);
        else {
            auto list=shared_assets::load_materials(assets,reference);
            if(list.empty())throw std::runtime_error("Native YMAT contains no materials");
            native=std::move(list.front());
        }
        result.unlit=native.shader=="unlit";
        result.fragment_shader_asset=native.fragment_shader_asset;
        result.vertex_shader_asset=native.vertex_shader_asset;
        result.gbuffer_shader_asset=native.gbuffer_shader_asset;
        result.vertex_hlsl_asset=native.vertex_hlsl_asset;
        result.fragment_hlsl_asset=native.fragment_hlsl_asset;
        result.gbuffer_hlsl_asset=native.gbuffer_hlsl_asset;
        if(!native.graph_source.empty()) {
            result.graph_shader_defines=compile_material_graph_with_functions(
                native.graph_source,[&assets](std::string const& function_path) {
                    if(assets.size(function_path)>32768)
                        throw std::invalid_argument(
                            "Native YMAT Material Function exceeds 32 KiB");
                    auto source=assets.read(function_path);
                    return std::string(source.begin(),source.end());
                });
        }
        for(auto const& param:native.parameters) {
            if(param.name=="base_color" && param.values.size()>=3)
                for(unsigned i=0;i<3;++i)result.base_color[i]=unit(param.values[i]);
            else if(param.name=="metallic" && !param.values.empty())
                result.metallic=unit(param.values[0]);
            else if(param.name=="roughness" && !param.values.empty())
                result.roughness=unit(param.values[0]);
        }
        if(native.blend=="alpha"||native.blend=="translucent")
            result.blend_mode=2;
        else if(native.alpha_cutoff>0.f) {
            result.blend_mode=1;
            result.alpha_cutoff=unit(native.alpha_cutoff);
        }
        for(auto const& tex:native.textures) {
            auto const slot=binding(tex.slot);
            if(slot<5)
                refs[slot]=detail::rebase_embedded_character_texture(
                    reference,tex.reference);
        }
    }
    for(unsigned i=0;i<5;++i)
        if(!refs[i].empty())
            result.textures[i]=load_texture(assets,refs[i],cache);
    return result;
}
}
struct MaterialLoadSession::State {
    AssetStore const& assets;
    MaterialLoadCache cache;
    explicit State(AssetStore const& source):assets(source){}
};
MaterialLoadSession::MaterialLoadSession(AssetStore const& source)
    :state_(std::make_unique<State>(source)){}
MaterialLoadSession::~MaterialLoadSession()=default;
MaterialDefinition MaterialLoadSession::load(std::string const& reference) {
    std::unordered_set<std::string> active;
    return load_material_recursive(state_->assets,reference,active,0,&state_->cache);
}
MaterialDefinition load_material_definition(AssetStore const& assets,
                                            std::string const& reference)
{
    std::unordered_set<std::string> active;
    return load_material_recursive(assets,reference,active,0,nullptr);
}
} // namespace northstarcpp
