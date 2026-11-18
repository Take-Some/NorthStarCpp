// Native NEF8/YMAT editor codec. No authored material is ever persisted as
// plaintext JSON. JSON remains an internal compiler/Qt tree representation.
#include <northstarcpp/material_ymat_authoring.hpp>
#include <northstarcpp/texture_ytd_authoring.hpp>
#include "asset_list_file.hpp"
#include "material_graph_xml.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <blake3.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <vector>

namespace northstarcpp::shared_assets {
namespace {
using Tree=boost::property_tree::ptree;
std::string json(Tree const& tree) {
    std::ostringstream stream;
    boost::property_tree::write_json(stream,tree,false);
    return stream.str();
}
void write_integer(std::vector<std::uint8_t>& out,std::size_t pos,
                   unsigned width,std::uint64_t value) {
    for(unsigned i=0;i<width;++i)
        out.at(pos+i)=std::uint8_t(value>>(8*i));
}
float unit(float value) {
    if(!std::isfinite(value)||value<0.f||value>1.f)
        throw std::invalid_argument("Invalid editable YMAT PBR factor");
    return value;
}
float channel(unsigned value) {
    float x=value/255.f;
    return x<=.04045f ? x/12.92f : std::pow((x+.055f)/1.055f,2.4f);
}
std::array<float,4> decode_color(std::string const& hex) {
    if(hex.size()!=7 || hex[0]!='#')
        throw std::invalid_argument("Invalid editable YMAT Base Color");
    auto digit=[](char c)->unsigned {
        if(c>='0'&&c<='9')return unsigned(c-'0');
        if(c>='a'&&c<='f')return unsigned(c-'a'+10);
        if(c>='A'&&c<='F')return unsigned(c-'A'+10);
        throw std::invalid_argument("Invalid editable YMAT hex digit");
    };
    return {channel(digit(hex[1])*16+digit(hex[2])),
            channel(digit(hex[3])*16+digit(hex[4])),
            channel(digit(hex[5])*16+digit(hex[6])),1.f};
}
Tree array_float(std::array<float,4> const& values,unsigned count) {
    Tree list;
    for(unsigned i=0;i<count;++i) {
        Tree value;value.put_value(values[i]);list.push_back({"",value});
    }
    return list;
}
Tree parameter(std::string name,std::string type,
               std::array<float,4> const& values,unsigned count) {
    Tree result;
    result.put("name",std::move(name));
    result.put("type",std::move(type));
    result.add_child("value",array_float(values,count));
    return result;
}
std::vector<std::uint8_t> pack_payload(
    std::string const& body,std::uint16_t kind,std::size_t entries) {
    if(body.empty()||body.size()>60ull*1024*1024)
        throw std::invalid_argument("Native NEF8 source exceeds 60 MiB");
    z_stream compressed{};
    if(deflateInit2(&compressed,Z_BEST_COMPRESSION,Z_DEFLATED,
                    -MAX_WBITS,8,Z_DEFAULT_STRATEGY)!=Z_OK)
        throw std::runtime_error("Cannot initialize YMAT NEF8 compression");
    struct Finish {z_stream* z;~Finish(){deflateEnd(z);}} finish{&compressed};
    std::vector<std::uint8_t> zipped(deflateBound(
        &compressed,static_cast<uLong>(body.size())));
    compressed.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(body.data()));
    compressed.avail_in=static_cast<uInt>(body.size());
    compressed.next_out=zipped.data();
    compressed.avail_out=static_cast<uInt>(zipped.size());
    if(deflate(&compressed,Z_FINISH)!=Z_STREAM_END)
        throw std::runtime_error("Cannot compress native YMAT");
    zipped.resize(compressed.total_out);
    std::vector<std::uint8_t> data(128+zipped.size(),0);
    std::memcpy(data.data(),"NEF8",4);
    write_integer(data,4,2,1);   // NEF8 v1
    write_integer(data,6,2,128);
    write_integer(data,8,2,kind);  // Native YTD (1); material authoring uses XML kind 4
    write_integer(data,10,2,1);  // raw DEFLATE
    write_integer(data,12,2,1);  // schema
    write_integer(data,16,8,128);
    write_integer(data,24,8,zipped.size());
    write_integer(data,32,8,body.size());
    write_integer(data,40,8,entries);
    write_integer(data,112,8,1);
    blake3_hasher hash;
    blake3_hasher_init(&hash);
    blake3_hasher_update(&hash,body.data(),body.size());
    blake3_hasher_finalize(&hash,data.data()+64,32);
    std::memcpy(data.data()+128,zipped.data(),zipped.size());
    return data;
}

}
std::vector<std::uint8_t> encode_editor_ymat(
    std::string const& authored_descriptor) {
    if(authored_descriptor.empty()||authored_descriptor.size()>512*1024)
        throw std::invalid_argument("Invalid authored YMAT descriptor size");
    auto descriptor=detail::parse_json(authored_descriptor);
    if(descriptor.get<unsigned>("schema")!=1)
        throw std::invalid_argument("Unsupported authored material schema");
    auto const name=descriptor.get<std::string>("name");
    detail::validate_name(name);
    auto const shader=descriptor.get<std::string>("shader");
    if(shader!="standard_lit"&&shader!="unlit")
        throw std::invalid_argument("Unsupported editable YMAT shader");
    auto blend=descriptor.get<std::string>("blend_mode","opaque");
    if(blend!="opaque"&&blend!="masked"&&blend!="translucent")
        throw std::invalid_argument("Unsupported editable YMAT blend");
    auto const cutoff=unit(descriptor.get<float>(
        "alpha_cutoff",blend=="masked"?.5f:0.f));
    auto const color=decode_color(descriptor.get<std::string>("base_color"));
    auto const metallic=unit(descriptor.get<float>("metallic"));
    auto const roughness=unit(descriptor.get<float>("roughness"));

    // Author XML directly. A temporary JSON/NEF8 kind-30 library must never
    // be constructed, including inside editor code paths.
    Tree material;
    material.put("<xmlattr>.name",name);
    material.put("<xmlattr>.shader",shader);
    material.put("Surface.<xmlattr>.blend",blend=="translucent"?"alpha":blend);
    material.put("Surface.<xmlattr>.alpha_cutoff",cutoff);
    material.put("Surface.<xmlattr>.two_sided",
                 descriptor.get<bool>("two_sided",false)?"true":"false");
    Tree params;
    auto push_param=[&](std::string const& id,std::string const& type,
                        std::array<float,4> const& values,unsigned count) {
        Tree node;node.put("<xmlattr>.name",id);
        node.put("<xmlattr>.type",type);
        std::ostringstream data;data.imbue(std::locale::classic());data<<std::setprecision(9);
        for(unsigned i=0;i<count;++i) {
            if(i)data<<',';
            data<<values[i];
        }
        node.put("<xmlattr>.value",data.str());
        params.add_child("Param",node);
    };
    push_param("base_color","color",color,4);
    push_param("metallic","float",{metallic,0,0,0},1);
    push_param("roughness","float",{roughness,0,0,0},1);
    material.add_child("Params",params);
    Tree textures;
    for(auto const* slot:{"base_color","normal","roughness","metallic","emissive"}) {
        auto ref=descriptor.get<std::string>(std::string(slot)+"_texture","");
        if(ref.empty())continue;
        detail::validate_reference(ref);
        if(ref.find(".ytd")==std::string::npos)
            throw std::invalid_argument("Native YMAT texture must reference YTD");
        Tree entry;
        entry.put("<xmlattr>.slot",slot);
        entry.put("<xmlattr>.ref",ref);
        textures.add_child("Texture",entry);
    }
    if(!textures.empty())material.add_child("Textures",textures);
    // Parse typed original descriptor, not PropertyTree's stringified JSON,
    // preserving numeric/boolean Material Graph node types in typed XML.
    if(descriptor.get_child_optional("graph")) {
        auto parsed=detail::typed_xml_value_from_json(authored_descriptor);
        bool found=false;
        for(auto const& field:parsed) {
            if(field.first!="Value" ||
               field.second.get<std::string>("<xmlattr>.name","")!="graph")continue;
            if(field.second.get<std::string>("<xmlattr>.type")!="object")
                throw std::invalid_argument("Material Graph must be an object");
            material.add_child("Graph.Value",field.second);
            found=true;
        }
        if(!found)throw std::invalid_argument("Missing typed Material Graph");
    }
    Tree doc,library;
    library.put("<xmlattr>.version",2);
    library.put("<xmlattr>.schema","northstarcpp.ymat.material_library.v2");
    library.add_child("Material",material);
    doc.add_child("YmatMaterialLibrary",library);
    std::ostringstream xml;
    boost::property_tree::write_xml(xml,doc,
        boost::property_tree::xml_writer_make_settings<char>(' ',2));
    auto encoded=encode_xml_ymat(xml.str(),1);
    auto const list=detail::parse_list_file(encoded);
    (void)detail::parse_materials(list,"Materials/Editor.ymat");
    return encoded;
}
std::string decode_editor_ymat(std::vector<std::uint8_t> const& bytes) {
    auto file=detail::parse_list_file(bytes);
    if(file.entries!=1)
        throw std::invalid_argument("YMAT editor source requires one selected material");
    if(file.kind==4) {
        auto list=load_materials_from_bytes(bytes,"Materials/Editor.ymat");
        if(list.size()!=1)
            throw std::invalid_argument("Expected single XML YMAT material");
        auto const& material=list.front();
        auto quote=[](std::string const& value) {
            std::ostringstream out;
            out<<'"';
            constexpr char hex[]="0123456789abcdef";
            for(unsigned char c:value) {
                if(c=='"')out<<"\\\"";
                else if(c=='\\')out<<"\\\\";
                else if(c=='\n')out<<"\\n";
                else if(c=='\r')out<<"\\r";
                else if(c=='\t')out<<"\\t";
                else if(c<32)
                    out<<"\\u00"<<hex[c>>4]<<hex[c&15];
                else out<<char(c);
            }
            out<<'"';
            return out.str();
        };
        auto channel=[](float linear) {
            double x=std::clamp(double(linear),0.,1.);
            x=x<=.0031308 ? x*12.92 :
                1.055*std::pow(x,1./2.4)-.055;
            return int(std::lround(std::clamp(x,0.,1.)*255.));
        };
        std::string base="#ffffff";
        float roughness=.5f,metallic=0.f;
        for(auto const& param:material.parameters) {
            if(param.name=="roughness"&&!param.values.empty())
                roughness=param.values.front();
            if(param.name=="metallic"&&!param.values.empty())
                metallic=param.values.front();
            if(param.name=="base_color"&&param.values.size()>=3) {
                std::ostringstream rgb;
                rgb<<'#'<<std::hex<<std::setfill('0');
                for(int i=0;i<3;++i)
                    rgb<<std::setw(2)<<channel(param.values[i]);
                base=rgb.str();
            }
        }
        std::ostringstream json;
        json<<std::setprecision(9);
        json<<"{\"schema\":1,\"name\":"<<quote(material.name)
            <<",\"shader\":"<<quote(material.shader)
            <<",\"blend_mode\":"<<quote(material.blend=="alpha"?
                "translucent":material.blend)
            <<",\"two_sided\":"<<(material.two_sided?"true":"false")
            <<",\"alpha_cutoff\":"<<material.alpha_cutoff
            <<",\"base_color\":"<<quote(base)
            <<",\"metallic\":"<<metallic
            <<",\"roughness\":"<<roughness;
        json<<",\"parameters\":[";
        bool first=true;
        for(auto const& param:material.parameters) {
            if(!first)json<<',';
            first=false;
            json<<"{\"name\":"<<quote(param.name)
                <<",\"type\":"<<quote(param.type)<<",\"value\":[";
            for(std::size_t i=0;i<param.values.size();++i) {
                if(i)json<<',';
                json<<param.values[i];
            }
            json<<"]}";
        }
        json<<"],\"textures\":[";
        first=true;
        for(auto const& texture:material.textures) {
            if(!first)json<<',';
            first=false;
            json<<"{\"slot\":"<<quote(texture.slot)
                <<",\"ref\":"<<quote(texture.reference)<<"}";
        }
        json<<']';
        if(!material.graph_source.empty()) {
            (void)detail::parse_json(material.graph_source);
            json<<",\"graph\":"<<material.graph_source;
        }
        json<<'}';
        return json.str();
    }
    throw std::invalid_argument("Only XML NEF8 kind-4 YMAT is supported");
}
std::vector<std::uint8_t> encode_rgba8_ytd(
    std::string const& name,unsigned width,unsigned height,
    std::vector<std::uint8_t> const& pixels,bool srgb) {
    detail::validate_name(name);
    if(!width||!height||width>4096||height>4096 ||
       pixels.size()!=std::size_t(width)*height*4 ||
       pixels.size()>56ull*1024*1024)
        throw std::invalid_argument("Invalid imported YTD RGBA8 dimensions");
    constexpr std::size_t texture_table=64;
    constexpr std::size_t index_header=64;
    constexpr std::size_t entry_offset=64;
    constexpr std::size_t mip_offset=entry_offset+64;
    constexpr std::size_t names_offset=mip_offset+32;
    auto const table_size=names_offset+name.size();
    auto const payload_start=texture_table+table_size;
    std::vector<std::uint8_t> body(payload_start+pixels.size(),0);
    std::memcpy(body.data(),"NETD",4);
    write_integer(body,4,4,2);  // NETD version
    write_integer(body,8,4,64); // NETD header
    write_integer(body,12,4,1); // entries
    write_integer(body,16,8,texture_table);
    write_integer(body,24,8,table_size);
    write_integer(body,32,8,payload_start);
    write_integer(body,40,8,pixels.size());
    auto const index=texture_table;
    std::memcpy(body.data()+index,"NTDX",4);
    write_integer(body,index+4,2,1);
    write_integer(body,index+6,2,index_header);
    write_integer(body,index+8,4,32);
    write_integer(body,index+12,4,1);
    write_integer(body,index+16,4,1);
    write_integer(body,index+20,4,entry_offset);
    write_integer(body,index+24,4,mip_offset);
    write_integer(body,index+28,4,names_offset);
    write_integer(body,index+32,4,name.size());
    auto const entry=index+entry_offset;
    write_integer(body,entry+0,8,0);
    write_integer(body,entry+8,8,0);
    write_integer(body,entry+16,8,pixels.size());
    write_integer(body,entry+24,4,0);
    write_integer(body,entry+28,2,name.size());
    write_integer(body,entry+30,2,1); // RGBA8
    write_integer(body,entry+32,4,width);
    write_integer(body,entry+36,4,height);
    write_integer(body,entry+40,4,0);
    write_integer(body,entry+44,4,1);
    write_integer(body,entry+48,4,srgb?2:1);
    auto const mip=index+mip_offset;
    write_integer(body,mip+0,8,0);
    write_integer(body,mip+8,8,pixels.size());
    write_integer(body,mip+16,4,width);
    write_integer(body,mip+20,4,height);
    write_integer(body,mip+24,4,0);
    write_integer(body,mip+28,4,0);
    std::memcpy(body.data()+index+names_offset,name.data(),name.size());
    std::memcpy(body.data()+payload_start,pixels.data(),pixels.size());
    auto const packed=pack_payload(
        std::string(reinterpret_cast<char const*>(body.data()),body.size()),
        1,1);
    auto loaded=load_textures_from_bytes(packed,"Textures/Imported.ytd");
    if(loaded.size()!=1||loaded.front().name!=name ||
       loaded.front().data!=pixels)
        throw std::runtime_error("Native imported YTD did not pass codec roundtrip");
    return packed;
}
YmatLibraryBody read_ymat_library(
    std::vector<std::uint8_t> const& original) {
    auto file=detail::parse_list_file(original);
    if(file.kind!=4)
        throw std::invalid_argument("Only XML NEF8 kind-4 YMAT material libraries are supported");
    (void)detail::parse_materials(file,"library.ymat");
    return {file.kind,file.entries,
            std::string(file.body.begin(),file.body.end())};
}
} // namespace northstarcpp::shared_assets
