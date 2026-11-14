// NorthStarCpp
// Author: Kayla Verner
#include <northstarcpp/shared_assets.hpp>
#include "asset_list_file.hpp"

#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
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
        result.push_back(std::move(material));
    }
    require(result.size() == file.entries, "Material count differs from NEF8 header");
    return result;
}

std::vector<Material> load_materials(AssetStore const& assets, std::string const& path)
{
    return detail::parse_materials(detail::read_list_file(assets, path), path);
}

std::vector<Drawable> load_drawables(AssetStore const& assets, std::string const& path)
{
    require(extension(path) == ".ydd", "Expected a .ydd drawable dictionary");
    auto const file = read_list_file(assets, path);
    require(file.kind == 2, "Expected a drawable dictionary in YDD");
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

} // namespace northstarcpp::shared_assets
