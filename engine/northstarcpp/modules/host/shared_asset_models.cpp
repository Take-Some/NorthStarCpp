// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"
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
    require(file.kind == 4 && kind == "material_library",
        "Expected a native YMAT material library (NEF8 kind 4)");

    Tree xml;
    std::istringstream input(std::string(file.body.begin(), file.body.end()));
    boost::property_tree::read_xml(input, xml, boost::property_tree::xml_parser::no_comments);
    auto const& library = xml.get_child("YmatMaterialLibrary");
    require(library.get<unsigned>("<xmlattr>.version") == 1 &&
        library.get<std::string>("<xmlattr>.schema") ==
            "northstarcpp.ymat.material_library.v1",
        "Unsupported material library schema");
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
        if(auto graph=tree.get_optional<std::string>("Graph")) {
            require(graph->size()<=32768,
                "Native XML YMAT Material Graph exceeds 32 KiB");
            material.graph_source=*graph;
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

static std::vector<Drawable> parse_drawable_dictionary(
    detail::ListFile const& file, std::string const& path)
{
    auto const ext=extension(path);
    require(ext == ".ydd" || ext == ".ydr", "Expected native YDD/YDR model");
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
    return parse_drawable_dictionary(detail::read_list_file(assets,path),path);
}
std::vector<Drawable> load_drawables_from_bytes(std::vector<std::uint8_t> const& bytes,
                                     std::string const& path) {
    return parse_drawable_dictionary(detail::parse_list_file(bytes),path);
}

} // namespace northstarcpp::shared_assets
