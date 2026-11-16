// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/shared_assets.hpp>
#include <northstarcpp/assets.hpp>
#include "asset_list_file.hpp"
#include "material_graph_xml.hpp"
#include <boost/property_tree/json_parser.hpp>

#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <limits>
#include <string_view>
#include <cmath>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace northstarcpp::shared_assets {
namespace {
using namespace detail;

std::vector<float> components(std::string text, std::size_t count)
{
    std::vector<float> values;
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    for (std::size_t i = 0; i < count; ++i) {
        float value = 0;
        require(bool(input >> value) && std::isfinite(value), "Invalid material parameter");
        values.push_back(value);
        if (i + 1 < count) {
            char separator = 0;
            require(bool(input >> separator) && separator == ',', "Expected comma-separated material values");
        }
    }
    require((input >> std::ws).eof(), "Unexpected material parameter components");
    return values;
}

template<std::size_t Size>
std::array<float, Size> coordinates(Tree const& values)
{
    require(values.size() == Size, "Invalid YDD vector size");
    std::array<float, Size> result{};
    std::size_t index = 0;
    for (auto const& value : values) {
        float const component = value.second.get_value<float>();
        require(value.first.empty() && std::isfinite(component), "Invalid YDD vector component");
        result[index++] = component;
    }
    return result;
}

std::size_t range_index(Tree const& entry, char const* range, char const* bound)
{
    return entry.get<std::size_t>(std::string(range) + "." + bound);
}

} // namespace

bool detail::legacy_material_library(ListFile const& file)
{
    return file.kind == 10 && file.metadata.get<std::string>("content_kind", "") == "ymt_material_library";
}

std::vector<Material> detail::parse_materials(ListFile const& file, std::string const& path)
{
    auto const ext = extension(path);
    require(ext == ".ymat", "Materials must use the native .ymat format");
    // Both original YMAT families are native engine resources:
    // kind 4  = XML world/material library (v1),
    // kind 30 = JSON character/weapon material library (v1).
    // The editor/material importer must not reject character YMAT just
    // because its representation differs from the world-material format.
    if(file.kind == 30) {
        auto const doc = parse_json(std::string(file.body.begin(),file.body.end()));
        require(doc.get<std::string>("schema","")=="northstar.ymat.v1",
                "Unsupported character/weapon YMAT schema");
        auto const& records=doc.get_child("materials");
        require(records.size()==file.entries,
                "Character/weapon YMAT entry count differs from NEF8 header");
        std::vector<Material> result;
        result.reserve(records.size());
        std::set<std::string> names;
        static const std::map<std::string,unsigned> arities{
            {"float",1},{"float2",2},{"float3",3},{"float4",4},{"color",4}};
        for(auto const& entry:records) {
            require(entry.first.empty(),"Invalid YMAT materials array");
            auto const& value=entry.second;
            Material material;
            material.name=value.get<std::string>("name");
            validate_name(material.name);
            require(names.insert(material.name).second,
                    "Duplicate character/weapon YMAT material name");
            material.shader=value.get<std::string>("shader");
            require(!material.shader.empty(),"Character YMAT shader is required");
            material.blend=value.get<std::string>("blend","opaque");
            require(material.blend=="opaque" || material.blend=="masked" ||
                    material.blend=="alpha","Unsupported character YMAT blend");
            material.two_sided=value.get<bool>("two_sided",false);
            material.alpha_cutoff=value.get<float>("alpha_cutoff",
                material.blend=="masked" ? 0.5f :
                material.blend=="alpha" ? 0.15f : 0.f);
            require(std::isfinite(material.alpha_cutoff) &&
                    material.alpha_cutoff>=0.f && material.alpha_cutoff<=1.f,
                    "Invalid character YMAT alpha cutoff");
            std::set<std::string> slots;
            if(auto textures=value.get_child_optional("textures"))
                for(auto const& item:*textures) {
                    require(item.first.empty(),"Invalid character YMAT texture array");
                    TextureBinding binding{
                        item.second.get<std::string>("slot"),
                        item.second.get<std::string>("ref")};
                    validate_name(binding.slot);
                    validate_reference(binding.reference);
                    auto const at=binding.reference.find('@');
                    require(extension(binding.reference.substr(0,at))==".ytd" &&
                            slots.insert(binding.slot).second,
                            "Invalid or duplicate character YMAT texture slot");
                    // Validate the author's required flag without turning
                    // optional references into different material types.
                    (void)item.second.get<bool>("required",true);
                    material.textures.push_back(std::move(binding));
                }
            std::set<std::string> parameter_names;
            if(auto params=value.get_child_optional("params"))
                for(auto const& item:*params) {
                    require(item.first.empty(),"Invalid character YMAT parameter array");
                    MaterialParameter param;
                    param.name=item.second.get<std::string>("name");
                    param.type=item.second.get<std::string>("type");
                    validate_name(param.name);
                    require(parameter_names.insert(param.name).second,
                            "Duplicate character YMAT parameter");
                    auto const arity=arities.find(param.type);
                    require(arity!=arities.end(),"Unsupported character YMAT parameter type");
                    auto const& encoded=item.second.get_child("value");
                    if(encoded.empty()) {
                        param.values.push_back(encoded.get_value<float>());
                    } else {
                        for(auto const& component:encoded) {
                            require(component.first.empty(),
                                    "Character YMAT parameter must be an array");
                            param.values.push_back(component.second.get_value<float>());
                        }
                    }
                    require(param.values.size()==arity->second &&
                            std::all_of(param.values.begin(),param.values.end(),
                                [](float number){return std::isfinite(number);}),
                            "Invalid character YMAT parameter values");
                    material.parameters.push_back(std::move(param));
                }
            if(auto graph=value.get_child_optional("graph")) {
                std::ostringstream serialized;
                boost::property_tree::write_json(serialized,*graph,false);
                material.graph_source=serialized.str();
                require(material.graph_source.size()<=32768,
                    "Native YMAT Material Graph exceeds 32 KiB");
            }
            result.push_back(std::move(material));
        }
        return result;
    }
    auto const kind = file.metadata.get<std::string>("content_kind", "");
    require(file.kind == 4 && (kind.empty() || kind == "material_library"),
        "Expected canonical XML YMAT (NEF8 kind 4)");

    Tree xml;
    std::istringstream input(std::string(file.body.begin(), file.body.end()));
    boost::property_tree::read_xml(input, xml, boost::property_tree::xml_parser::no_comments);
    auto const& library = xml.get_child("YmatMaterialLibrary");
    auto const version=library.get<unsigned>("<xmlattr>.version");
    auto const schema=library.get<std::string>("<xmlattr>.schema");
    require((version==1 && schema=="northstarcpp.ymat.material_library.v1") ||
            (version==2 && schema=="northstarcpp.ymat.material_library.v2"),
        "Unsupported XML material library schema");
    std::vector<Material> result;
    std::set<std::string> names;
    for (auto const& node : library) {
        if (node.first == "<xmlattr>")
            continue;
        require(node.first == "Material", "Unexpected material library element");
        auto const& tree = node.second;
        Material material;
        material.name = tree.get<std::string>("<xmlattr>.name");
        validate_name(material.name);
        require(names.insert(material.name).second, "Duplicate material name");
        material.shader = tree.get<std::string>("<xmlattr>.shader");
        require(!material.shader.empty(), "Material shader is required");
        material.blend = tree.get<std::string>("Surface.<xmlattr>.blend", "opaque");
        auto const two_sided = tree.get<std::string>("Surface.<xmlattr>.two_sided", "false");
        require(two_sided == "true" || two_sided == "True" || two_sided == "false" || two_sided == "False",
            "Invalid material sidedness");
        material.two_sided = two_sided == "True" || two_sided == "true";
        material.alpha_cutoff = tree.get<float>("Surface.<xmlattr>.alpha_cutoff", 0.f);
        require(std::isfinite(material.alpha_cutoff) && material.alpha_cutoff >= 0.f &&
            material.alpha_cutoff <= 1.f, "Invalid material alpha cutoff");

        std::set<std::string> slots;
        if (auto textures = tree.get_child_optional("Textures")) {
            for (auto const& item : *textures) {
                require(item.first == "Texture", "Unexpected texture element");
                TextureBinding binding{item.second.get<std::string>("<xmlattr>.slot"),
                    item.second.get<std::string>("<xmlattr>.ref")};
                validate_name(binding.slot);
                validate_reference(binding.reference);
                require(extension(binding.reference.substr(0, binding.reference.find('@'))) == ".ytd" &&
                    slots.insert(binding.slot).second, "Invalid or duplicate material texture slot");
                material.textures.push_back(std::move(binding));
            }
        }

        std::set<std::string> parameters;
        if (auto params = tree.get_child_optional("Params")) {
            for (auto const& item : *params) {
                require(item.first == "Param", "Unexpected material parameter element");
                MaterialParameter param;
                param.name = item.second.get<std::string>("<xmlattr>.name");
                param.type = item.second.get<std::string>("<xmlattr>.type");
                validate_name(param.name);
                require(parameters.insert(param.name).second, "Duplicate material parameter");
                static std::map<std::string, unsigned> const arities{
                    {"float", 1}, {"float2", 2}, {"float3", 3}, {"float4", 4}, {"color", 4}};
                auto const arity = arities.find(param.type);
                require(arity != arities.end(), "Unsupported material parameter type");
                param.values = components(item.second.get<std::string>("<xmlattr>.value"), arity->second);
                material.parameters.push_back(std::move(param));
            }
        }
        auto shader_path=[&](char const* attribute,char const* suffix,
                             std::string& destination) {
            auto binding=tree.get_optional<std::string>(
                std::string("Shaders.<xmlattr>.")+attribute);
            if(!binding)return;
            auto const& path=*binding;
            bool valid=!path.empty() && path.size()<=1024 &&
                path.front()!='/' && path.find_first_of("@\\:")==std::string::npos &&
                extension(path)==suffix;
            std::string_view remaining(path);
            while(valid && !remaining.empty()) {
                auto slash=remaining.find('/');
                auto segment=remaining.substr(0,slash);
                valid=!segment.empty() && segment!="." && segment!=".." &&
                    std::none_of(segment.begin(),segment.end(),
                        [](unsigned char ch){return ch<32;});
                if(slash==std::string_view::npos)break;
                remaining.remove_prefix(slash+1);
                if(remaining.empty())valid=false;
            }
            require(valid,"Unsafe shader asset path in XML YMAT");
            destination=path;
        };
        shader_path("vertex",".vert",material.vertex_shader_asset);
        shader_path("fragment",".frag",material.fragment_shader_asset);
        shader_path("gbuffer",".frag",material.gbuffer_shader_asset);
        shader_path("vertex_hlsl",".hlsl",material.vertex_hlsl_asset);
        shader_path("fragment_hlsl",".hlsl",material.fragment_hlsl_asset);
        shader_path("gbuffer_hlsl",".hlsl",material.gbuffer_hlsl_asset);
        if(auto graph=tree.get_child_optional("Graph")) {
            if(auto value=graph->get_child_optional("Value"))
                material.graph_source=
                    detail::graph_json_from_xml_value(*value);
            else material.graph_source=graph->get_value<std::string>();
            require(material.graph_source.size()<=32768,
                "Native XML YMAT Material Graph exceeds 32 KiB");
        }
        result.push_back(std::move(material));
    }
    require(result.size() == file.entries, "Material count differs from NEF8 header");
    return result;
}

std::vector<Material> load_materials(AssetStore const& assets, std::string const& path)
{
    return detail::parse_materials(detail::read_list_file(assets, path), path);
}

std::vector<Material> load_materials_from_bytes(
    std::vector<std::uint8_t> const& bytes, std::string const& path)
{
    return detail::parse_materials(detail::parse_list_file(bytes),path);
}

// Native binary NEF8/YDD dictionary (runtime mesh-parts v1).
// Layout: 120-byte header; name table; geometry header (72 bytes);
// repeated 48-byte part headers, then 32-byte render vertices,
// 48-byte auxiliary attributes, and uint32 triangle indices.
// Parse with bounds-checked offsets; no blind reinterpret_cast/struct alias.
static std::vector<Drawable> parse_binary_drawables(
    detail::ListFile const& file, std::string const& path)
{
    using detail::Bytes;
    using detail::require;
    Bytes const body(file.body);
    auto const type=body.integer(0,4);
    require(body.size()>=192 && body.integer(4,4)==1 &&
        body.integer(8,4)==40 && file.entries==1 &&
        (type==4 || type==5),
        "Unsupported native binary YDD/YDR header");
    auto const strings=body.integer(16,8);
    auto const strings_size=body.integer(24,8);
    auto const geometry=body.integer(32,8);
    require(strings>=120 && strings_size>0 &&
        strings<=body.size() && strings_size<=body.size()-strings &&
        geometry>=strings+strings_size && geometry<=body.size()-72,
        "Invalid binary drawable string/mesh offsets");
    auto const table_end=strings+strings_size;
    auto name_at=[&](std::uint64_t offset) {
        require(offset < strings_size,"Invalid drawable name reference");
        auto const start=strings+offset;
        auto end=start;
        while (end<table_end && body.integer(end,1)!=0) ++end;
        require(end<table_end,"Unterminated binary drawable name");
        auto text=body.string(start,end-start);
        detail::validate_name(text);
        return text;
    };
    auto const count=body.integer(geometry,4);
    require(count>0 && count<=128 && body.integer(geometry+4,4)==1,
        "Unsupported native drawable part table");
    Drawable drawable;
    drawable.name=name_at(0);
    drawable.meshes.reserve(static_cast<std::size_t>(count));
    auto cursor=geometry+72;
    std::size_t triangle_total=0;
    for (std::uint64_t part=0;part<count;++part) {
        auto const descriptor_bytes=type==5 ? 52u : 48u;
        require(cursor<=body.size() && body.size()-cursor>=descriptor_bytes,
            "Truncated binary drawable part");
        auto const vertices=body.integer(cursor+8,4);
        auto const indices=body.integer(cursor+12,4);
        auto const format=body.integer(cursor+40,4);
        auto const extra_stride=body.integer(cursor+44,4);
        require(vertices>0 && vertices<=200000 &&
            indices>0 && indices%3==0 && indices<=600000 &&
            format==1 && extra_stride==48,
            "Unsupported native drawable vertex layout");
        auto const vertex_bytes=vertices*80u;
        auto const index_bytes=indices*4u;
        auto const available=body.size()-cursor-descriptor_bytes;
        require(vertex_bytes<=available && index_bytes<=available-vertex_bytes,
            "Binary drawable buffers exceed decompressed size");
        auto const vertex_base=cursor+descriptor_bytes;
        auto const index_base=vertex_base+vertex_bytes;
        MeshPart mesh;
        mesh.name=name_at(body.integer(cursor,4));
        auto const material_offset=body.integer(cursor+4,4);
        if (material_offset!=0xFFFFFFFFu) {
            mesh.material=body.string(strings+material_offset,
                [&]() -> std::size_t {
                    require(material_offset<strings_size,"Invalid YDD material offset");
                    auto const start=strings+material_offset;
                    auto end=start;
                    while(end<table_end && body.integer(end,1)!=0)++end;
                    require(end<table_end,"Unterminated YDD material reference");
                    return static_cast<std::size_t>(end-start);
                }());
            detail::validate_reference(mesh.material);
            mesh.material_required=false; // The model remains drawable without optional material packs.
        }
        mesh.vertices.reserve(static_cast<std::size_t>(vertices));
        for (std::uint64_t i=0;i<vertices;++i) {
            auto const offset=vertex_base+i*32u;
            Vertex vertex{{
                body.real(offset), body.real(offset+4), body.real(offset+8)
            },{
                body.real(offset+12), body.real(offset+16), body.real(offset+20)
            },{
                body.real(offset+24), body.real(offset+28)
            }};
            for (auto const x:vertex.position) require(std::isfinite(x),"Nonfinite YDD position");
            for (auto const x:vertex.normal) require(std::isfinite(x),"Nonfinite YDD normal");
            for (auto const x:vertex.uv) require(std::isfinite(x),"Nonfinite YDD UV");
            mesh.vertices.push_back(vertex);
        }
        mesh.indices.reserve(static_cast<std::size_t>(indices));
        for (std::uint64_t i=0;i<indices;++i) {
            auto const index=body.integer(index_base+i*4u,4);
            require(index<vertices,"YDD triangle index outside vertex buffer");
            mesh.indices.push_back(static_cast<std::uint32_t>(index));
        }
        triangle_total+=static_cast<std::size_t>(indices/3);
        require(triangle_total<=250000,"Drawable exceeds triangle budget");
        drawable.meshes.push_back(std::move(mesh));
        cursor=index_base+index_bytes;
    }
    require(cursor==body.size(),"Unexpected trailing binary drawable bytes");
    for (std::size_t axis=0;axis<3;++axis) {
        drawable.bounds_min[axis]=body.real(76u+axis*4u);
        drawable.bounds_max[axis]=body.real(88u+axis*4u);
        require(std::isfinite(drawable.bounds_min[axis]) &&
            std::isfinite(drawable.bounds_max[axis]) &&
            drawable.bounds_min[axis]<=drawable.bounds_max[axis],
            "Invalid binary YDD drawable bounds");
    }
    return {std::move(drawable)};
}


static std::vector<Drawable> parse_nvrm_drawables(
    detail::ListFile const& file,std::string const& path)
{
    using detail::require;
    using detail::Bytes;
    Bytes const data(file.body);
    require(file.kind==6 && file.entries==1 &&
            data.size()>=20 && data.magic(0,"NVRM") &&
            data.integer(4,4)==1,"Expected NorthStar NVRM model v1");
    auto const metadata_bytes=data.integer(8,4);
    auto const geometry_bytes=data.integer(12,8);
    require(metadata_bytes>0 && metadata_bytes<=8u*1024u*1024u &&
            metadata_bytes<=data.size()-20 &&
            geometry_bytes==data.size()-20-metadata_bytes,
            "NVRM metadata or geometry length mismatch");
    auto const doc=detail::parse_json(data.string(20,metadata_bytes));
    require(doc.get<std::string>("schema","")=="northstar.model.runtime.v1",
            "Unsupported NVRM model schema");
    auto const& meshes=doc.get_child("meshes");
    require(!meshes.empty() && meshes.size()<=128,
            "Invalid NVRM mesh part count");
    Drawable drawable;
    drawable.name=std::filesystem::u8path(path).stem().u8string();
    detail::validate_name(drawable.name);
    for(auto const& [axis,which]:std::array<std::pair<char const*,char const*>,0>{}) {
        (void)axis;(void)which;
    }
    auto const& bounds=doc.get_child("bounds");
    std::size_t axis=0;
    for(auto const& v:bounds.get_child("min")) {
        require(axis<3,"NVRM bounds min invalid");
        drawable.bounds_min[axis++]=v.second.get_value<float>();
    }
    require(axis==3,"NVRM bounds min count invalid");
    axis=0;
    for(auto const& v:bounds.get_child("max")) {
        require(axis<3,"NVRM bounds max invalid");
        drawable.bounds_max[axis++]=v.second.get_value<float>();
    }
    require(axis==3,"NVRM bounds max count invalid");
    for(unsigned k=0;k<3;++k)
        require(std::isfinite(drawable.bounds_min[k]) &&
                std::isfinite(drawable.bounds_max[k]) &&
                drawable.bounds_min[k]<=drawable.bounds_max[k],
                "Nonfinite or inverted NVRM bounds");
    auto const payload_offset=20+metadata_bytes;
    Bytes const& source=data;
    auto const& materials=doc.get_child("material_slots");
    std::set<std::size_t> valid_slots;
    for(auto const& entry:materials)
        require(valid_slots.insert(entry.second.get<std::size_t>("slot")).second,
                "Duplicate NVRM material slot");
    auto folder=path.substr(0,path.find_last_of('/')==std::string::npos?
                            0:path.find_last_of('/')+1);
    if(folder.compare(0,7,"models/")==0)
        folder.replace(0,7,"materials/");
    else folder="materials/";
    auto const material_path=folder+drawable.name+".ymat";
    std::size_t vertices_total=0,indices_total=0,mesh_number=0;
    for(auto const& element:meshes) {
        auto const& mesh=element.second;
        auto const count=mesh.get<std::size_t>("vertex_count");
        require(count>0 && count<=1000000 &&
                vertices_total<=1200000-count,
                "NVRM vertex budget exceeded");
        vertices_total+=count;
        struct Stream {std::uint64_t offset=0;unsigned components=0;bool valid=false;};
        Stream position,normal,uv;
        auto decode_stream=[&](boost::property_tree::ptree const& stream) {
            auto const offset=stream.get<std::uint64_t>("offset");
            auto const length=stream.get<std::uint64_t>("length");
            auto const stride=stream.get<std::uint64_t>("stride");
            auto const semantic=stream.get<std::string>("semantic");
            auto const format=stream.get<std::string>("format");
            require(offset<=geometry_bytes && length<=geometry_bytes-offset &&
                    stride>0 && length==count*stride,
                    "NVRM vertex stream outside payload");
            Stream* destination=nullptr;
            unsigned dims=0;
            if(semantic=="position") {destination=&position;dims=3;}
            if(semantic=="normal") {destination=&normal;dims=3;}
            if(semantic=="texcoord0"){destination=&uv;dims=2;}
            if(!destination)return;
            require(!destination->valid &&
                    format==(dims==2?"f32x2":"f32x3") &&
                    stride==dims*4,"Invalid NVRM render stream");
            *destination={payload_offset+offset,dims,true};
        };
        for(auto const& stream:mesh.get_child("streams"))
            decode_stream(stream.second);
        require(position.valid && normal.valid && uv.valid,
                "NVRM render mesh requires position, normal, texcoord0");
        auto const& index=mesh.get_child("index_buffer");
        auto const index_offset=index.get<std::uint64_t>("offset");
        auto const index_count=index.get<std::size_t>("index_count");
        auto const index_length=index.get<std::uint64_t>("length");
        require(index.get<std::string>("format")=="u32" &&
                index_count>0 && index_count%3==0 &&
                index_length==index_count*4ull &&
                index_offset<=geometry_bytes &&
                index_length<=geometry_bytes-index_offset &&
                indices_total<=6000000-index_count,
                "Invalid NVRM index buffer");
        indices_total+=index_count;
        std::vector<Vertex> decoded;
        decoded.reserve(count);
        for(std::size_t vertex=0;vertex<count;++vertex) {
            Vertex dst{};
            for(unsigned i=0;i<3;++i) {
                dst.position[i]=source.real(position.offset+vertex*12+i*4);
                dst.normal[i]=source.real(normal.offset+vertex*12+i*4);
                require(std::isfinite(dst.position[i]) &&
                        std::isfinite(dst.normal[i]),
                        "Nonfinite NVRM vertex");
            }
            for(unsigned i=0;i<2;++i) {
                dst.uv[i]=source.real(uv.offset+vertex*8+i*4);
                require(std::isfinite(dst.uv[i]),"Nonfinite NVRM UV");
            }
            decoded.push_back(dst);
        }
        auto const& primitives=mesh.get_child("primitives");
        require(!primitives.empty() && primitives.size()<=64,
                "Invalid NVRM primitive count");
        for(auto const& item:primitives) {
            auto const& primitive=item.second;
            auto const start=primitive.get<std::size_t>("first_index");
            auto const length=primitive.get<std::size_t>("index_count");
            auto const base=primitive.get<std::size_t>("base_vertex",0);
            auto const slot=primitive.get<std::size_t>("material_slot");
            require(start<=index_count && length>0 && length%3==0 &&
                    length<=index_count-start && base<count &&
                    valid_slots.count(slot)!=0 &&
                    drawable.meshes.size()<256,
                    "Invalid NVRM render primitive");
            MeshPart part;
            part.name="mesh_"+std::to_string(mesh_number)+"_slot_"+std::to_string(slot);
            part.material=material_path+"@mat_"+std::to_string(slot);
            part.material_required=false; // Missing YMAT must not hide geometry.
            part.vertices=decoded;
            part.indices.reserve(length);
            for(std::size_t i=start;i<start+length;++i) {
                auto const idx=source.integer(payload_offset+index_offset+i*4,4);
                require(idx<count-base,"NVRM index outside vertex stream");
                part.indices.push_back(static_cast<std::uint32_t>(base+idx));
            }
            drawable.meshes.push_back(std::move(part));
        }
        ++mesh_number;
    }
    return {std::move(drawable)};
}

static std::vector<Drawable> parse_drawable_dictionary(
    detail::ListFile const& file, std::string const& path)
{
    auto const ext=extension(path);
    require(ext == ".ydd" || ext == ".ydr", "Expected native YDD/YDR model");
    if(file.kind==6)return parse_nvrm_drawables(file,path);
    require(file.kind == 2, "Expected native drawable dictionary");
    if (!file.body.empty() && file.body.front()!='{')
        return parse_binary_drawables(file,path);
    auto const body = parse_json(std::string(file.body.begin(), file.body.end()));
    require(body.get<std::string>("schema") == "newengine.ydd.drawable_dictionary.v1" &&
        body.get<std::string>("mesh_encoding") == "newengine.ydd.runtime_mesh_parts.v1",
        "Unsupported YDD body or mesh encoding");
    auto const& entries = body.get_child("entries");
    auto const& names = body.get_child("common_entries");
    auto const& slots = body.get_child("material_slots");
    auto const& bounds = body.get_child("bounds");
    auto const& meshes = body.get_child("runtime_mesh_parts");
    require(entries.size() == file.entries && names.size() == file.entries, "YDD entry count mismatch");

    std::vector<Tree const*> indexed_slots, indexed_bounds;
    for (auto const& slot : slots)
        indexed_slots.push_back(&slot.second);
    for (auto const& bound : bounds)
        indexed_bounds.push_back(&bound.second);
    std::vector<Drawable> result;
    std::map<std::string, std::size_t> lookup;
    std::vector<std::map<std::string, std::pair<std::string, bool>>> bindings;
    std::vector<std::size_t> mesh_counts;
    auto name = names.begin();
    for (auto const& entry : entries) {
        Drawable drawable;
        drawable.name = name++->second.get<std::string>("name");
        validate_name(drawable.name);
        require(lookup.emplace(drawable.name, result.size()).second, "Duplicate YDD drawable");
        auto const& item = entry.second;
        auto const bound = item.get<std::size_t>("bounds_index");
        require(bound < indexed_bounds.size(), "YDD bounds index outside table");
        drawable.bounds_min = coordinates<3>(indexed_bounds[bound]->get_child("aabb_min"));
        drawable.bounds_max = coordinates<3>(indexed_bounds[bound]->get_child("aabb_max"));
        for (std::size_t axis = 0; axis < 3; ++axis)
            require(drawable.bounds_min[axis] <= drawable.bounds_max[axis], "Invalid YDD bounds");
        auto const first = range_index(item, "material_slot_range", "start");
        auto const end = range_index(item, "material_slot_range", "end");
        require(first <= end && end <= indexed_slots.size(), "Invalid YDD material slot range");
        bindings.emplace_back();
        for (auto i = first; i < end; ++i) {
            auto const& slot = *indexed_slots[i];
            auto const slot_name = slot.get<std::string>("slot_name");
            auto const reference = slot.get<std::string>("material_ref");
            validate_name(slot_name);
            validate_reference(reference);
            auto const ext = extension(reference.substr(0, reference.find('@')));
            require(ext == ".ymat", "YDD materials must reference .ymat");
            require(bindings.back().emplace(slot_name, std::make_pair(reference,
                slot.get<bool>("required", true))).second, "Duplicate YDD material slot");
        }
        auto const first_mesh = range_index(item, "mesh_part_range", "start");
        auto const end_mesh = range_index(item, "mesh_part_range", "end");
        require(first_mesh <= end_mesh && end_mesh <= meshes.size(), "Invalid YDD mesh range");
        mesh_counts.push_back(end_mesh - first_mesh);
        result.push_back(std::move(drawable));
    }
    for (auto const& mesh : meshes) {
        auto const& item = mesh.second;
        auto const found = lookup.find(item.get<std::string>("entry"));
        require(found != lookup.end(), "YDD mesh references unknown drawable");
        MeshPart part;
        part.name = item.get<std::string>("name");
        validate_name(part.name);
        auto const slot_name = item.get<std::string>("material_slot", "");
        if (!slot_name.empty()) {
            auto const& slots_for_entry = bindings[found->second];
            auto const binding = slots_for_entry.find(slot_name);
            require(binding != slots_for_entry.end(), "YDD mesh references unknown material slot");
            part.material = binding->second.first;
            part.material_required = binding->second.second;
        }
        auto const& vertices = item.get_child("vertices");
        auto const& indices = item.get_child("indices");
        require(!vertices.empty() && !indices.empty() && indices.size() % 3 == 0,
            "Invalid YDD triangle mesh");
        part.vertices.reserve(vertices.size());
        part.indices.reserve(indices.size());
        for (auto const& vertex : vertices) {
            auto const& v = vertex.second;
            part.vertices.push_back({coordinates<3>(v.get_child("pos")),
                coordinates<3>(v.get_child("nrm")), coordinates<2>(v.get_child("uv"))});
        }
        for (auto const& index : indices) {
            auto const value = index.second.get_value<std::uint32_t>();
            require(index.first.empty() && value < part.vertices.size(), "YDD index outside vertex buffer");
            part.indices.push_back(value);
        }
        result[found->second].meshes.push_back(std::move(part));
    }
    for (std::size_t i = 0; i < result.size(); ++i)
        require(result[i].meshes.size() == mesh_counts[i], "YDD drawable mesh count mismatch");
    return result;
}

std::vector<Drawable> load_drawables(AssetStore const& assets,
                                     std::string const& path) {
    // NVRM kind-6 drawables may carry binary NEF16 metadata; their
    // authoritative JSON manifest is inside the decompressed NVRM body.
    // Never interpret the optional outer metadata as JSON.
    if(assets.size(path)>64u*1024u*1024u)
        throw std::length_error("Drawable asset exceeds 64 MiB");
    return parse_drawable_dictionary(detail::parse_list_file(assets.read(path),false),path);
}
std::vector<Drawable> load_drawables_from_bytes(std::vector<std::uint8_t> const& bytes,
                                     std::string const& path) {
    return parse_drawable_dictionary(detail::parse_list_file(bytes,false),path);
}

} // namespace northstarcpp::shared_assets
