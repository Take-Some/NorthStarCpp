// Native NEF8/YMAT editor codec. No authored material is ever persisted as
// plaintext JSON. JSON remains an internal compiler/Qt tree representation.
#include <northstarcpp/material_ymat_authoring.hpp>
#include "asset_list_file.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <blake3.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>
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
std::vector<std::uint8_t> pack(Tree const& root) {
    std::string const body=json(root);
    if(body.empty()||body.size()>512*1024)
        throw std::invalid_argument("Editable YMAT body exceeds 512 KiB");
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
    write_integer(data,8,2,30);  // Native Material Library kind 30
    write_integer(data,10,2,1);  // raw DEFLATE
    write_integer(data,12,2,1);  // schema
    write_integer(data,16,8,128);
    write_integer(data,24,8,zipped.size());
    write_integer(data,32,8,body.size());
    write_integer(data,40,8,1);  // single editable material
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
    Tree material;
    material.put("name",name);
    material.put("shader",shader);
    material.put("blend",blend=="translucent"?"alpha":blend);
    material.put("alpha_cutoff",cutoff);
    material.put("two_sided",descriptor.get<bool>("two_sided",false));
    Tree params;
    params.push_back({"",parameter("base_color","color",color,4)});
    params.push_back({"",parameter("metallic","float",{metallic,0,0,0},1)});
    params.push_back({"",parameter("roughness","float",{roughness,0,0,0},1)});
    material.add_child("params",params);
    Tree textures;
    for(auto const* slot:{"base_color","normal","roughness","metallic","emissive"}) {
        auto ref=descriptor.get<std::string>(std::string(slot)+"_texture","");
        if(ref.empty())continue;
        detail::validate_reference(ref);
        if(ref.find(".ytd")==std::string::npos)
            throw std::invalid_argument("Native YMAT texture must reference YTD");
        Tree entry;
        entry.put("slot",slot);
        entry.put("ref",ref);
        entry.put("required",true);
        textures.push_back({"",entry});
    }
    if(!textures.empty())material.add_child("textures",textures);
    if(auto graph=descriptor.get_child_optional("graph"))
        material.add_child("graph",*graph);
    // PropertyTree writes numeric JSON scalars as strings. Store the exact
    // validated authoring source as an encoded field instead, preserving
    // number/bool types for the Qt editor's strict descriptor validation.
    material.put("editor_source",authored_descriptor);
    Tree materials;
    materials.push_back({"",material});
    Tree root;
    root.put("schema","northstar.ymat.v1");
    root.put("editor_schema","northstar.ymat.editor.v1");
    root.add_child("materials",materials);
    auto encoded=pack(root);
    auto const list=detail::parse_list_file(encoded);
    (void)detail::parse_materials(list,"Materials/Editor.ymat");
    return encoded;
}
std::string decode_editor_ymat(std::vector<std::uint8_t> const& bytes) {
    auto file=detail::parse_list_file(bytes);
    if(file.kind!=30 || file.entries!=1)
        throw std::invalid_argument("Imported YMAT library is not directly editable");
    auto root=detail::parse_json(std::string(file.body.begin(),file.body.end()));
    if(root.get<std::string>("editor_schema","")!="northstar.ymat.editor.v1")
        throw std::invalid_argument("Read-only imported YMAT: create a material copy");
    auto const& materials=root.get_child("materials");
    if(materials.size()!=1)
        throw std::invalid_argument("Editable YMAT must have exactly one material");
    auto editor=materials.begin()->second.get<std::string>("editor_source");
    if(editor.size()>512*1024)
        throw std::invalid_argument("Editable YMAT descriptor exceeds size limit");
    auto typed=detail::parse_json(editor);
    if(typed.get<unsigned>("schema")!=1)
        throw std::invalid_argument("Invalid editable YMAT source");
    return editor;
}
YmatLibraryBody read_ymat_library(
    std::vector<std::uint8_t> const& original) {
    auto file=detail::parse_list_file(original);
    if(file.kind!=4 && file.kind!=30)
        throw std::invalid_argument("Only NEF8 kind 4/30 are material libraries");
    (void)detail::parse_materials(file,"library.ymat");
    return {file.kind,file.entries,
            std::string(file.body.begin(),file.body.end())};
}
std::vector<std::uint8_t> replace_ymat_library_body(
    std::vector<std::uint8_t> const& original,
    std::string const& updated_body) {
    if(updated_body.empty()||updated_body.size()>64ull*1024*1024)
        throw std::invalid_argument("Edited YMAT exceeds 64 MiB");
    detail::Bytes view(original);
    auto old=detail::parse_list_file(original);
    if(old.kind!=4 && old.kind!=30)
        throw std::invalid_argument("Only NEF8 YMAT materials can be edited");
    if(original.size()<32)
        throw std::invalid_argument("Truncated NEF8 YMAT");
    bool const v2=view.integer(4,1)==2;
    auto const compressed_original=v2?view.integer(16,8):
        view.integer(24,8);
    auto const offset=v2?original.size()-compressed_original:
        view.integer(16,8);
    if(offset>original.size() || offset<32)
        throw std::invalid_argument("Invalid YMAT compressed body offset");
    z_stream compressed{};
    if(deflateInit2(&compressed,Z_BEST_COMPRESSION,Z_DEFLATED,
                    -MAX_WBITS,8,Z_DEFAULT_STRATEGY)!=Z_OK)
        throw std::runtime_error("NEF8 YMAT compressor initialization failed");
    struct Finish {z_stream& z;~Finish(){deflateEnd(&z);}} cleanup{compressed};
    auto const input_size=static_cast<uLong>(updated_body.size());
    std::vector<std::uint8_t> payload(deflateBound(&compressed,input_size));
    compressed.next_in=reinterpret_cast<Bytef*>(
        const_cast<char*>(updated_body.data()));
    compressed.avail_in=static_cast<uInt>(input_size);
    compressed.next_out=payload.data();
    compressed.avail_out=static_cast<uInt>(payload.size());
    if(deflate(&compressed,Z_FINISH)!=Z_STREAM_END)
        throw std::runtime_error("NEF8 YMAT compression failed");
    payload.resize(compressed.total_out);
    std::vector<std::uint8_t> replacement(
        original.begin(),original.begin()+static_cast<std::ptrdiff_t>(offset));
    replacement.insert(replacement.end(),payload.begin(),payload.end());
    if(v2) {
        write_integer(replacement,16,8,payload.size());
        write_integer(replacement,24,8,updated_body.size());
        if(view.integer(8,2)&4u) {
            blake3_hasher hash{};
            blake3_hasher_init(&hash);
            blake3_hasher_update(&hash,updated_body.data(),updated_body.size());
            blake3_hasher_finalize(&hash,replacement.data()+32,32);
        }
    }else {
        write_integer(replacement,24,8,payload.size());
        write_integer(replacement,32,8,updated_body.size());
        blake3_hasher hash{};
        blake3_hasher_init(&hash);
        blake3_hasher_update(&hash,updated_body.data(),updated_body.size());
        blake3_hasher_finalize(&hash,replacement.data()+64,32);
    }
    // A successful decode is mandatory; count/identity checks must happen in
    // caller after a targeted named-record edit.
    auto verified=detail::parse_list_file(replacement);
    if(verified.kind!=old.kind || verified.entries!=old.entries ||
       verified.body.size()!=updated_body.size() ||
       !std::equal(verified.body.begin(),verified.body.end(),
                   updated_body.begin()))
        throw std::runtime_error("Edited YMAT did not pass NEF8 roundtrip");
    (void)detail::parse_materials(verified,"library.ymat");
    return replacement;
}
} // namespace northstarcpp::shared_assets
