// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// New independent scene-document V3 implementation.
// Reference archive informed the feature map; its code is not included here.

#include <northstarcpp/scene.hpp>
#include <northstarcpp/scene_pawn.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace northstarcpp {
namespace {

constexpr std::size_t max_scene_records = 16384;
constexpr std::size_t max_scene_cells = 8192;
constexpr std::size_t max_scene_volumes = 8192;
constexpr std::size_t max_scene_portals = 8192;
constexpr std::size_t max_scene_layers = 256;
constexpr float world_limit = 1000000.f;

void require(bool result, char const* error)
{
    if (!result)
        throw std::runtime_error(error);
}

bool finite_bounded(float value)
{
    return std::isfinite(value) && std::abs(value) <= world_limit;
}
bool valid_vector(Vector3 const& v)
{
    return finite_bounded(v.x) && finite_bounded(v.y) && finite_bounded(v.z);
}
bool ordered(Vector3 const& a, Vector3 const& b)
{
    return a.x <= b.x && a.y <= b.y && a.z <= b.z;
}
bool utf8(std::string const& text, std::size_t maximum)
{
    return !text.empty() && text.size() <= maximum &&
        std::none_of(text.begin(), text.end(), [](unsigned char ch) {
            return ch < 32 || ch == 127;
        }) &&
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0) != 0;
}
bool optional_utf8(std::string const& text, std::size_t maximum)
{
    return text.empty() || utf8(text, maximum);
}
bool safe_virtual_asset(std::string const& text)
{
    if (text.empty())
        return true;
    if (!utf8(text, 512) || text.front() == '/' || text.find('\\') != std::string::npos ||
        text.find(':') != std::string::npos)
        return false;
    std::istringstream components(text);
    std::string part;
    while (std::getline(components, part, '/'))
        if (part.empty() || part == "." || part == "..")
            return false;
    return true;
}
void validate_settings(SceneStreamingSettings const& settings)
{
    require(std::isfinite(settings.load_radius) && settings.load_radius >= 1.f &&
            settings.load_radius <= 1000000.f,
            "Scene streaming load radius must be 1..1000000");
    require(std::isfinite(settings.unload_radius) &&
            settings.unload_radius > settings.load_radius &&
            settings.unload_radius <= 1000000.f,
            "Scene streaming unload radius must exceed load radius");
    require(std::isfinite(settings.lookahead_seconds) &&
            settings.lookahead_seconds >= 0.f && settings.lookahead_seconds <= 10.f,
            "Scene streaming lookahead must be 0..10 seconds");
    require(settings.max_cell_loads_per_update >= 1 &&
            settings.max_cell_loads_per_update <= max_scene_cells &&
            settings.max_cell_unloads_per_update >= 1 &&
            settings.max_cell_unloads_per_update <= max_scene_cells,
            "Scene streaming cell budgets must be 1..8192");
}
std::uint64_t unsigned_id(std::string const& token)
{
    if (token.empty() || std::any_of(token.begin(), token.end(),
        [](unsigned char c) { return c < '0' || c > '9'; }))
        throw std::runtime_error("Scene object ID is not an unsigned decimal integer");
    std::size_t processed = 0;
    auto const id = std::stoull(token, &processed, 10);
    if (processed != token.size())
        throw std::runtime_error("Scene ID contains trailing data");
    return static_cast<std::uint64_t>(id);
}
bool empty_tail(std::istringstream& input)
{
    input >> std::ws;
    return input.eof();
}
std::string ensure(std::istringstream& input, bool ok, std::size_t line)
{
    if (!ok || !empty_tail(input))
        throw std::runtime_error("Malformed scene v3 record at line " + std::to_string(line));
    return {};
}
std::string quoted(std::string const& input)
{
    std::ostringstream result;
    result << std::quoted(input);
    return result.str();
}
bool record_size(std::size_t value, std::size_t cap)
{
    return value < cap;
}
void write_vector(std::ostream& stream, Vector3 const& v)
{
    stream << v.x << ' ' << v.y << ' ' << v.z;
}
void read_vector(std::istream& stream, Vector3& v)
{
    stream >> v.x >> v.y >> v.z;
}

} // namespace

// Called by the engine's shared validation path before editor, ECS, physics,
// rendering or asset import can consume an untrusted scene v3 document.
void validate_scene_v3(Scene const& scene)
{
    require(scene.objects.size() <= max_scene_records, "Scene v3 entity count exceeds limit");
    require(utf8(scene.name, 128), "Scene v3 name must be valid UTF-8");
    validate_settings(scene.streaming);

    require(scene.layers.size() <= max_scene_layers, "Too many scene layers");
    require(scene.cells.size() <= max_scene_cells, "Too many streaming cells");
    require(scene.volumes.size() <= max_scene_volumes, "Too many scene volumes");
    require(scene.portals.size() <= max_scene_portals, "Too many scene portals");

    std::unordered_set<std::string> layers{"Default"};
    bool explicit_default = false;
    for (auto const& layer : scene.layers) {
        require(utf8(layer.name, 96), "Scene layer has invalid name");
        if (layer.name == "Default") {
            require(!explicit_default, "Duplicate Default scene layer");
            explicit_default = true;
        } else {
            require(layers.insert(layer.name).second, "Duplicate scene layer name");
        }
    }
    std::unordered_set<std::string> cells;
    for (auto const& cell : scene.cells) {
        require(utf8(cell.name, 96), "Invalid streaming cell name");
        require(cells.insert(cell.name).second, "Duplicate streaming cell");
        require(valid_vector(cell.minimum) && valid_vector(cell.maximum) &&
                ordered(cell.minimum, cell.maximum),
                "Streaming cell bounds must be finite, ordered and within world limits");
    }

    std::unordered_set<std::string> volume_names;
    for (auto const& volume : scene.volumes) {
        require(utf8(volume.name, 96) && volume_names.insert(volume.name).second,
                "Duplicate or invalid scene volume");
        require(optional_utf8(volume.tag, 96), "Scene volume tag is invalid");
        require(valid_vector(volume.minimum), "Scene volume origin is invalid");
        if (volume.shape == SceneVolume::Shape::sphere) {
            require(std::isfinite(volume.radius) && volume.radius > 0.f &&
                    volume.radius <= world_limit, "Scene sphere radius is invalid");
        } else {
            require(valid_vector(volume.maximum) &&
                    ordered(volume.minimum, volume.maximum),
                    "Scene box bounds are invalid");
        }
    }

    std::unordered_set<std::string> portal_names;
    for (auto const& portal : scene.portals) {
        require(utf8(portal.name, 96) && portal_names.insert(portal.name).second,
                "Duplicate or invalid portal name");
        require(utf8(portal.room_a, 96) && utf8(portal.room_b, 96) &&
                portal.room_a != portal.room_b, "Portal must connect two different rooms");
        require(valid_vector(portal.minimum) && valid_vector(portal.maximum) &&
                ordered(portal.minimum, portal.maximum), "Portal bounds are invalid");
    }

    std::unordered_map<std::uint64_t, std::uint64_t> parents;
    parents.reserve(scene.objects.size());
    for (auto const& object : scene.objects) {
        SceneNode const& node = object.node;
        require(node.id != 0 && parents.emplace(node.id, node.parent).second,
                "Scene v3 requires unique nonzero stable entity IDs");
        require(node.parent != node.id, "Entity cannot parent itself");
        require(layers.count(node.layer) != 0, "Entity references unknown scene layer");
        require(node.cell.empty() || cells.count(node.cell) != 0,
                "Entity references unknown streaming cell");
        require(optional_utf8(node.room, 96), "Invalid interior room name");
        require(safe_virtual_asset(node.asset), "Unsafe or invalid virtual asset reference");
        require(node.script.empty() ||
            (safe_virtual_asset(node.script) &&
             node.script.rfind("scripts/",0)==0 &&
             node.script.size() > 11 &&
             (node.script.compare(node.script.size()-3,3,".js")==0 ||
              node.script.compare(node.script.size()-4,4,".mjs")==0)),
            "Unsafe or invalid script component path");

        require(valid_vector(node.rotation) && valid_vector(node.scale) &&
                node.scale.x > 0.f && node.scale.y > 0.f && node.scale.z > 0.f &&
                node.scale.x <= 10000.f && node.scale.y <= 10000.f &&
                node.scale.z <= 10000.f, "Entity authored transform is invalid");
        require(std::isfinite(node.lod_near) && std::isfinite(node.lod_far) &&
                node.lod_near >= 0.f && node.lod_far >= node.lod_near &&
                node.lod_far <= world_limit, "Entity LOD distance range is invalid");

    }

    // Tri-color topological check detects cycles without recursion or stack overflow.
    std::unordered_map<std::uint64_t, std::uint8_t> status;
    status.reserve(parents.size());
    for (auto const& [id, ignored] : parents) {
        (void)ignored;
        std::uint64_t current = id;
        std::vector<std::uint64_t> chain;
        while (current != 0 && status[current] != 2) {
            auto next = parents.find(current);
            require(next != parents.end(), "Missing parent entity in scene hierarchy");
            require(status[current] != 1, "Cycle detected in scene hierarchy");
            status[current] = 1;
            chain.push_back(current);
            current = next->second;
        }
        for (auto member : chain)
            status[member] = 2;
    }
}

Scene parse_scene_v3(std::string const& bytes)
{
    std::istringstream input(bytes);
    input.imbue(std::locale::classic());
    std::string line;
    std::getline(input, line);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    require(line == "NorthStarCpp.Scene 3", "Unsupported v3 scene header");

    Scene scene;
    scene.version = 3;
    scene.name = "Untitled";
    std::size_t line_number = 1;
    bool has_streaming = false;
    bool has_budget = false;
    bool has_scene = false;
    std::unordered_map<std::uint64_t,std::string> scripts;
    std::vector<std::uint64_t> old_owned_ids;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        std::istringstream record(line);
        record.imbue(std::locale::classic());
        record >> std::ws;
        if (record.eof() || record.peek() == '#')
            continue;
        std::string kind;
        record >> kind;
        if (kind == "scene") {
            if (has_scene || !(record >> std::quoted(scene.name)))
                throw std::runtime_error("Invalid scene metadata at line " + std::to_string(line_number));
            has_scene = true;
        } else if (kind == "streaming") {
            if (has_streaming ||
                !(record >> scene.streaming.load_radius >>
                  scene.streaming.unload_radius >> scene.streaming.lookahead_seconds))
                throw std::runtime_error("Invalid scene streaming settings at line " + std::to_string(line_number));
            has_streaming = true;
        } else if (kind == "stream_budget") {
            if (has_budget ||
                !(record >> scene.streaming.max_cell_loads_per_update >>
                  scene.streaming.max_cell_unloads_per_update))
                throw std::runtime_error("Invalid scene streaming budget at line " +
                                         std::to_string(line_number));
            has_budget = true;
        } else if (kind == "layer") {
            SceneLayer layer;
            int flag = 0;
            if (!(record >> std::quoted(layer.name) >> flag) || (flag != 0 && flag != 1) ||
                !record_size(scene.layers.size(), max_scene_layers))
                throw std::runtime_error("Invalid layer at line " + std::to_string(line_number));
            layer.visible = flag == 1;
            scene.layers.push_back(std::move(layer));
        } else if (kind == "cell") {
            SceneCell cell;
            int flag = 0;
            if (!(record >> std::quoted(cell.name)))
                throw std::runtime_error("Invalid cell name at line " + std::to_string(line_number));
            read_vector(record, cell.minimum);
            read_vector(record, cell.maximum);
            if (!(record >> flag) || (flag != 0 && flag != 1) ||
                !record_size(scene.cells.size(), max_scene_cells))
                throw std::runtime_error("Invalid streaming cell at line " + std::to_string(line_number));
            cell.always_loaded = flag == 1;
            scene.cells.push_back(std::move(cell));
        } else if (kind == "volume") {
            SceneVolume volume;
            std::string shape;
            int flag = 0;
            if (!(record >> std::quoted(volume.name) >> shape))
                throw std::runtime_error("Invalid scene volume at line " + std::to_string(line_number));
            if (shape == "sphere") {
                volume.shape = SceneVolume::Shape::sphere;
                read_vector(record, volume.minimum);
                record >> volume.radius;
            } else if (shape == "box") {
                volume.shape = SceneVolume::Shape::box;
                read_vector(record, volume.minimum);
                read_vector(record, volume.maximum);
            } else {
                throw std::runtime_error("Unsupported scene volume shape at line " + std::to_string(line_number));
            }
            if (!(record >> std::quoted(volume.tag) >> flag) || (flag != 0 && flag != 1) ||
                !record_size(scene.volumes.size(), max_scene_volumes))
                throw std::runtime_error("Invalid scene volume at line " + std::to_string(line_number));
            volume.enabled = flag == 1;
            scene.volumes.push_back(std::move(volume));
        } else if (kind == "portal") {
            ScenePortal portal;
            int flag = 0;
            if (!(record >> std::quoted(portal.name) >>
                std::quoted(portal.room_a) >> std::quoted(portal.room_b)))
                throw std::runtime_error("Invalid portal at line " + std::to_string(line_number));
            read_vector(record, portal.minimum);
            read_vector(record, portal.maximum);
            if (!(record >> flag) || (flag != 0 && flag != 1) ||
                !record_size(scene.portals.size(), max_scene_portals))
                throw std::runtime_error("Invalid portal at line " + std::to_string(line_number));
            portal.open = flag == 1;
            scene.portals.push_back(std::move(portal));
        } else if (kind == "script") {
            std::string id, path;
            if (!(record >> id >> std::quoted(path)) ||
                !scripts.emplace(unsigned_id(id),path).second ||
                scripts.size()>max_scene_records)
                throw std::runtime_error("Invalid or duplicate script component at line " +
                    std::to_string(line_number));
        } else if (kind == "component") {
            SceneComponentBlob entry;
            std::string id, hex;
            if(!(record>>id>>std::quoted(entry.type)>>entry.version>>
                 std::quoted(hex)) ||
               hex.size()%2 || hex.size()>2097152 ||
               scene.components.size()>=max_scene_records)
                throw std::runtime_error("Invalid component at line "+
                                         std::to_string(line_number));
            entry.entity_id=unsigned_id(id);
            auto nibble=[](char ch)->int {
                if(ch>='0'&&ch<='9')return ch-'0';
                if(ch>='a'&&ch<='f')return ch-'a'+10;
                if(ch>='A'&&ch<='F')return ch-'A'+10;
                return -1;
            };
            for(std::size_t n=0;n<hex.size();n+=2) {
                int const a=nibble(hex[n]),b=nibble(hex[n+1]);
                if(a<0||b<0)throw std::runtime_error("Invalid component hex");
                entry.payload.push_back(char(a*16+b));
            }
            scene.components.push_back(std::move(entry));
        } else if (kind == "entity") {
            SceneObject object;
            std::string id, parent;
            int player = 0, visible = 0, collision = 0;
            if (!(record >> id >> parent >> std::quoted(object.name) >>
                std::quoted(object.node.layer) >> std::quoted(object.node.cell) >>
                std::quoted(object.node.room) >> std::quoted(object.node.asset)))
                throw std::runtime_error("Invalid entity metadata at line " + std::to_string(line_number));
            object.node.id = unsigned_id(id);
            object.node.parent = unsigned_id(parent);
            read_vector(record, object.position);
            if (!(record >> object.size >> std::hex >> object.color >> std::dec >> player >>
                    object.mass))
                throw std::runtime_error("Invalid scene entity at line " + std::to_string(line_number));
            read_vector(record, object.node.rotation);
            read_vector(record, object.node.scale);
            if (!(record >> object.node.lod_near >> object.node.lod_far >> visible >> collision) ||
                (player != 0 && player != 1) || (visible != 0 && visible != 1) ||
                (collision != 0 && collision != 1) ||
                !record_size(scene.objects.size(), max_scene_records))
                throw std::runtime_error("Invalid scene entity at line " + std::to_string(line_number));
            if(player==1)old_owned_ids.push_back(object.node.id);
            object.node.visible = visible == 1;
            object.node.collision = collision == 1;
            scene.objects.push_back(std::move(object));
        } else {
            throw std::runtime_error("Unknown scene v3 record '" + kind +
                                     "' at line " + std::to_string(line_number));
        }
        ensure(record, true, line_number);
    }
    for (auto& object : scene.objects) {
        auto found=scripts.find(object.node.id);
        if (found!=scripts.end()) {
            object.node.script=std::move(found->second);
            scripts.erase(found);
        }
    }
    require(scripts.empty(),"Script component references a missing scene entity");
    for(auto id:old_owned_ids)scene_pawn::migrate_imported_local(scene,id);
    validate_scene(scene);
    return scene;
}

std::string serialize_scene_v3(Scene const& scene)
{
    validate_scene(scene);
    require(scene.version == 3, "serialize_scene_v3 requires a v3 scene");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "NorthStarCpp.Scene 3\n";
    out << "scene " << quoted(scene.name) << '\n';
    out << "streaming " << scene.streaming.load_radius << ' ' <<
        scene.streaming.unload_radius << ' ' << scene.streaming.lookahead_seconds << '\n';
    out << "stream_budget " << scene.streaming.max_cell_loads_per_update << ' ' <<
        scene.streaming.max_cell_unloads_per_update << '\n';
    for (auto const& layer : scene.layers)
        out << "layer " << quoted(layer.name) << ' ' << layer.visible << '\n';
    for (auto const& cell : scene.cells) {
        out << "cell " << quoted(cell.name) << ' ';
        write_vector(out, cell.minimum);
        out << ' ';
        write_vector(out, cell.maximum);
        out << ' ' << cell.always_loaded << '\n';
    }
    for (auto const& volume : scene.volumes) {
        out << "volume " << quoted(volume.name) << ' ';
        if (volume.shape == SceneVolume::Shape::sphere) {
            out << "sphere ";
            write_vector(out, volume.minimum);
            out << ' ' << volume.radius;
        } else {
            out << "box ";
            write_vector(out, volume.minimum);
            out << ' ';
            write_vector(out, volume.maximum);
        }
        out << ' ' << quoted(volume.tag) << ' ' << volume.enabled << '\n';
    }
    for (auto const& portal : scene.portals) {
        out << "portal " << quoted(portal.name) << ' ' <<
            quoted(portal.room_a) << ' ' << quoted(portal.room_b) << ' ';
        write_vector(out, portal.minimum);
        out << ' ';
        write_vector(out, portal.maximum);
        out << ' ' << portal.open << '\n';
    }
    for (auto const& object : scene.objects) {
        auto const& node = object.node;
        out << "entity " << node.id << ' ' << node.parent << ' ' << quoted(object.name) <<
            ' ' << quoted(node.layer) << ' ' << quoted(node.cell) << ' ' <<
            quoted(node.room) << ' ' << quoted(node.asset) << ' ';
        write_vector(out, object.position);
        out << ' ' << object.size << ' ' << std::hex << std::setfill('0') <<
            std::setw(6) << object.color << std::dec << std::setfill(' ') <<
            ' ' << 0 << ' ' << object.mass << ' '; // text-v3 compatibility slot
        write_vector(out, node.rotation);
        out << ' ';
        write_vector(out, node.scale);
        out << ' ' << node.lod_near << ' ' << node.lod_far << ' ' <<
            node.visible << ' ' << node.collision << '\n';
        if (!node.script.empty())
            out << "script " << node.id << ' ' << quoted(node.script) << '\n';
    }
    static constexpr char digits[]="0123456789abcdef";
    for(auto const& component:scene.components) {
        out<<"component "<<component.entity_id<<' '<<quoted(component.type)<<
            ' '<<component.version<<" \"";
        for(unsigned char byte:component.payload)
            out<<digits[byte>>4]<<digits[byte&15];
        out<<"\"\n";
    }
    return out.str();
}

} // namespace northstarcpp
