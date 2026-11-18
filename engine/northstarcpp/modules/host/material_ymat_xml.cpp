// Canonical YMAT serialization: NEF8 kind 4, XML material library.
// JSON kind 30 has no runtime, authoring or migration compatibility path.
#include <northstarcpp/material_ymat_authoring.hpp>
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"
#include "material_graph_xml.hpp"
#include <boost/property_tree/xml_parser.hpp>
#include <blake3.h>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace northstarcpp::shared_assets {
namespace {
using Tree=boost::property_tree::ptree;
void write64(std::vector<std::uint8_t>& data,std::size_t pos,std::uint64_t n) {
    for(unsigned i=0;i<8;++i)data.at(pos+i)=std::uint8_t(n>>(i*8));
}
void write16(std::vector<std::uint8_t>& data,std::size_t pos,std::uint16_t n) {
    data.at(pos)=std::uint8_t(n);
    data.at(pos+1)=std::uint8_t(n>>8);
}
std::string render_xml(Tree const& tree) {
    std::ostringstream o;
    boost::property_tree::write_xml(o,tree,
        boost::property_tree::xml_writer_make_settings<char>(' ',2));
    return o.str();
}
void verify_names(std::vector<Material> const& before,
                  std::vector<Material> const& after) {
    if(before.size()!=after.size())
        throw std::runtime_error("YMAT migration lost material records");
    for(std::size_t i=0;i<before.size();++i) {
        auto const& a=before[i];auto const& b=after[i];
        if(a.name!=b.name||a.shader!=b.shader||a.blend!=b.blend||
           a.two_sided!=b.two_sided||a.textures.size()!=b.textures.size()||
           a.parameters.size()!=b.parameters.size()||
           std::abs(a.alpha_cutoff-b.alpha_cutoff)>1e-6f)
            throw std::runtime_error("YMAT migration changed material semantics");
        for(std::size_t j=0;j<a.textures.size();++j)
            if(a.textures[j].slot!=b.textures[j].slot ||
               a.textures[j].reference!=b.textures[j].reference)
                throw std::runtime_error("YMAT migration changed texture reference");
        for(std::size_t j=0;j<a.parameters.size();++j) {
            auto const& lhs=a.parameters[j];
            auto const& rhs=b.parameters[j];
            if(lhs.name!=rhs.name||lhs.type!=rhs.type||
               lhs.values.size()!=rhs.values.size())
                throw std::runtime_error("YMAT migration changed typed parameter");
            for(std::size_t k=0;k<lhs.values.size();++k)
                if(std::abs(lhs.values[k]-rhs.values[k])>1e-5f)
                    throw std::runtime_error("YMAT migration changed parameter value");
        }
    }
}
}
bool is_xml_ymat(std::vector<std::uint8_t> const& source) {
    try {return detail::parse_list_file(source).kind==4;}
    catch(...) {return false;}
}
std::vector<std::uint8_t> encode_xml_ymat(
    std::string const& xml,std::size_t count) {
    if(xml.empty()||xml.size()>64ull*1024*1024||count>65536)
        throw std::invalid_argument("Invalid XML YMAT size or material count");
    Tree doc;std::istringstream stream(xml);
    boost::property_tree::read_xml(stream,doc);
    auto& library=doc.get_child("YmatMaterialLibrary");
    auto const version=library.get<unsigned>("<xmlattr>.version");
    auto const schema=library.get<std::string>("<xmlattr>.schema");
    if(!((version==1 && schema=="northstarcpp.ymat.material_library.v1") ||
         (version==2 && schema=="northstarcpp.ymat.material_library.v2")))
        throw std::invalid_argument("Unsupported XML YMAT library schema");
    library.put("<xmlattr>.version",2);
    library.put("<xmlattr>.schema","northstarcpp.ymat.material_library.v2");
    auto const normalized_xml=render_xml(doc);
    std::size_t actual=0;
    for(auto const& item:library)
        if(item.first=="Material")++actual;
    if(actual!=count || !count)
        throw std::invalid_argument("XML YMAT material count does not match header");

    z_stream z{};
    if(deflateInit2(&z,Z_BEST_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,
                    Z_DEFAULT_STRATEGY)!=Z_OK)
        throw std::runtime_error("Cannot initialize XML YMAT compressor");
    struct Cleanup {z_stream& z;~Cleanup(){deflateEnd(&z);}} cleanup{z};
    std::vector<std::uint8_t> packed(deflateBound(&z,static_cast<uLong>(normalized_xml.size())));
    z.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(normalized_xml.data()));
    z.avail_in=static_cast<uInt>(normalized_xml.size());
    z.next_out=packed.data();z.avail_out=static_cast<uInt>(packed.size());
    if(deflate(&z,Z_FINISH)!=Z_STREAM_END)
        throw std::runtime_error("XML YMAT compression failed");
    packed.resize(z.total_out);
    // Canonical YMAT has no JSON metadata section. The only serialized
    // document is the XML material library inside the NEF8 envelope.
    std::vector<std::uint8_t> output(128+packed.size(),0);
    std::memcpy(output.data(),"NEF8",4);
    write16(output,4,1);
    write16(output,6,128);
    write16(output,8,4); // The sole output kind for YMAT.
    write16(output,10,1);
    write16(output,12,1);
    write64(output,16,128);
    write64(output,24,packed.size());
    write64(output,32,normalized_xml.size());
    write64(output,40,count);
    write64(output,112,1);
    blake3_hasher hash{};
    blake3_hasher_init(&hash);
    blake3_hasher_update(&hash,normalized_xml.data(),normalized_xml.size());
    blake3_hasher_finalize(&hash,output.data()+64,32);
    std::memcpy(output.data()+128,packed.data(),packed.size());
    auto validated=detail::parse_list_file(output);
    if(validated.kind!=4||validated.entries!=count)
        throw std::runtime_error("XML YMAT header roundtrip failed");
    return output;
}
std::vector<std::uint8_t> migrate_ymat_to_xml(
    std::vector<std::uint8_t> const& source) {
    auto file=detail::parse_list_file(source);
    if(file.kind==4) {
        (void)detail::parse_materials(file,"Materials/Migrated.ymat");
        // Normalize old XML which had JSON text embedded in <Graph> and
        // <Extensions><Property name="editor_source">.
        Tree doc;
        std::istringstream input(
            std::string(file.body.begin(),file.body.end()));
        boost::property_tree::read_xml(input,doc);
        bool changed=false;
        for(auto& [tag,entry]:doc.get_child("YmatMaterialLibrary")) {
            if(tag!="Material")continue;
            if(auto graph=entry.get_child_optional("Graph")) {
                if(!graph->get_child_optional("Value")) {
                    auto legacy=graph->get_value<std::string>();
                    if(!legacy.empty()) {
                        entry.erase("Graph");
                        entry.add_child("Graph.Value",
                            detail::graph_xml_value_from_json(legacy));
                        changed=true;
                    }
                }
            }
            if(entry.get_child_optional("Editor")) {
                entry.erase("Editor");
                changed=true;
            }
            if(auto extras=entry.get_child_optional("Extensions")) {
                for(auto it=extras->begin();it!=extras->end();) {
                    if(it->first!="Property" ||
                       it->second.get<std::string>(
                           "<xmlattr>.name","")!="editor_source") {
                        ++it;continue;
                    }
                    auto editor=it->second.get_value<std::string>();
                    // Remove redundant legacy editor descriptor entirely.
                    // Typed Params, Surface, Textures, Graph remain authoritative.
                    it=extras->erase(it);
                    changed=true;
                }
                if(extras->empty())entry.erase("Extensions");
            }
        }
        // Older kind4 YMAT also used a JSON NEF8 metadata block.
        // Rewrite it into the canonical metadata-free XML container.
        auto const& root=doc.get_child("YmatMaterialLibrary");
        bool const current=root.get<unsigned>("<xmlattr>.version",0)==2 &&
            root.get<std::string>("<xmlattr>.schema","")==
                "northstarcpp.ymat.material_library.v2";
        if(!changed && file.metadata.empty() && current)return source;
        auto result=encode_xml_ymat(render_xml(doc),file.entries);
        auto after=load_materials_from_bytes(result,
                                             "Materials/Migrated.ymat");
        auto before=load_materials_from_bytes(source,
                                              "Materials/Migrated.ymat");
        verify_names(before,after);
        return result;
    }
    throw std::invalid_argument("Only XML NEF8 kind-4 YMAT is supported");
}
} // namespace northstarcpp::shared_assets
