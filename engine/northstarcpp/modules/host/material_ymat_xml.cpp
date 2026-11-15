// Canonical YMAT serialization: NEF8 kind 4, XML material library.
// Legacy kind 30 is decoded only for import/migration, never emitted.
#include <northstarcpp/material_ymat_authoring.hpp>
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"
#include "material_graph_xml.hpp"
#include <boost/property_tree/json_parser.hpp>
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
std::string render_json(Tree const& tree) {
    std::ostringstream o;boost::property_tree::write_json(o,tree,false);
    return o.str();
}
std::string render_xml(Tree const& tree) {
    std::ostringstream o;
    boost::property_tree::write_xml(o,tree,
        boost::property_tree::xml_writer_make_settings<char>(' ',2));
    return o.str();
}
void appendParam(Tree& dst,MaterialParameter const& source) {
    Tree item;
    item.put("<xmlattr>.name",source.name);
    item.put("<xmlattr>.type",source.type);
    std::ostringstream nums;
    nums.precision(9);
    for(std::size_t i=0;i<source.values.size();++i) {
        if(i)nums<<',';
        nums<<source.values[i];
    }
    item.put("<xmlattr>.value",nums.str());
    dst.add_child("Param",item);
}
Tree material_xml(Material const& source) {
    Tree m;
    m.put("<xmlattr>.name",source.name);
    m.put("<xmlattr>.shader",source.shader);
    m.put("Surface.<xmlattr>.blend",source.blend);
    m.put("Surface.<xmlattr>.two_sided",source.two_sided?"True":"False");
    m.put("Surface.<xmlattr>.alpha_cutoff",source.alpha_cutoff);
    Tree textures;
    for(auto const& texture:source.textures) {
        Tree t;t.put("<xmlattr>.slot",texture.slot);
        t.put("<xmlattr>.ref",texture.reference);
        textures.add_child("Texture",t);
    }
    m.add_child("Textures",textures);
    Tree params;
    for(auto const& param:source.parameters)appendParam(params,param);
    m.add_child("Params",params);
    if(!source.graph_source.empty())
        m.add_child("Graph.Value",
            detail::graph_xml_value_from_json(source.graph_source));
    return m;
}
std::string to_xml(
    std::vector<Material> const& materials,
    std::vector<std::map<std::string,std::string>> const& extras) {
    Tree result,library;
    library.put("<xmlattr>.version",2);
    library.put("<xmlattr>.schema","northstarcpp.ymat.material_library.v2");
    for(std::size_t i=0;i<materials.size();++i) {
        auto material=material_xml(materials[i]);
        if(i<extras.size()&&!extras[i].empty()) {
            Tree extensions;
            for(auto const& [key,encoded]:extras[i]) {
                // The first byte encodes whether the legacy source field was
                // a scalar string (S) or a structured JSON value (C).
                // It is never stored in the XML document.
                if(encoded.empty())
                    throw std::invalid_argument("Invalid migrated XML extension");
                auto const data=encoded.substr(1);
                if(key=="editor_source") {
                    // Editor source is not an independent material definition:
                    // PBR parameters and the typed Graph are authoritative.
                    // Storing a second copy would create stale conflicting data.
                    continue;
                }
                Tree item;item.put("<xmlattr>.name",key);
                if(encoded[0]=='C')
                    item.add_child("Value",
                        detail::typed_xml_value_from_json(data));
                else if(encoded[0]=='S')item.put_value(data);
                else throw std::invalid_argument("Invalid XML extension encoding");
                extensions.add_child("Property",item);
            }
            if(!extensions.empty())
                material.add_child("Extensions",extensions);
        }
        library.add_child("Material",material);
    }
    result.add_child("YmatMaterialLibrary",library);
    return render_xml(result);
}
// Material Graph source is a typed JSON AST in memory, not the persisted
// YMAT library format. Copy its source text losslessly before PropertyTree
// can turn numeric graph nodes into string values.
std::string graph_from_editor_source(std::string const& source) {
    auto const key=source.find("\"graph\"");
    if(key==std::string::npos)return {};
    auto const colon=source.find(':',key+7);
    if(colon==std::string::npos)return {};
    auto start=source.find_first_not_of(" \r\n\t",colon+1);
    if(start==std::string::npos || source[start]!='{')return {};
    bool quoted=false,escape=false;
    int depth=0;
    for(std::size_t i=start;i<source.size();++i) {
        char const c=source[i];
        if(quoted) {
            if(escape)escape=false;
            else if(c=='\\')escape=true;
            else if(c=='"')quoted=false;
            continue;
        }
        if(c=='"'){quoted=true;continue;}
        if(c=='{')++depth;
        if(c=='}' && --depth==0)return source.substr(start,i-start+1);
    }
    return {};
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
    if(file.kind!=30)
        throw std::invalid_argument("Unsupported legacy YMAT kind for migration");
    auto materials=detail::parse_materials(file,"Materials/Migrated.ymat");
    Tree legacy=detail::parse_json(
        std::string(file.body.begin(),file.body.end()));
    std::vector<std::map<std::string,std::string>> extras;
    std::size_t index=0;
    auto const source_json=std::string(file.body.begin(),file.body.end());
    for(auto const& item:legacy.get_child("materials")) {
        // Graphs are reconstructed from the original typed source, not via
        // PropertyTree's lossy number-to-string JSON serializer.
        auto exact=detail::typed_material_graph_from_json_library(
            source_json,index);
        if(exact.empty())
            if(auto editor=item.second.get_optional<std::string>("editor_source"))
                exact=graph_from_editor_source(*editor);
        if(!exact.empty())materials.at(index).graph_source=std::move(exact);
        ++index;
        std::map<std::string,std::string> attrs;
        for(auto const& [field,value]:item.second) {
            if(field=="name"||field=="shader"||field=="blend"||
               field=="two_sided"||field=="alpha_cutoff"||
               field=="textures"||field=="params"||field=="graph")continue;
            if(value.empty())
                attrs[field]="S"+value.get_value<std::string>();
            else attrs[field]="C"+render_json(value);
        }
        extras.push_back(std::move(attrs));
    }
    auto xml=to_xml(materials,extras);
    auto encoded=encode_xml_ymat(xml,materials.size());
    auto after=load_materials_from_bytes(encoded,"Materials/Migrated.ymat");
    verify_names(materials,after);
    return encoded;
}
} // namespace northstarcpp::shared_assets
