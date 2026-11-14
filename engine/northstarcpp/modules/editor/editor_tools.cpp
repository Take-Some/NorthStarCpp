// NorthStarCpp Editor tools
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
#include "editor_tools.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace northstarcpp::editor_tools {
namespace {
void require_index(Scene const& scene, std::size_t index)
{
    if (index >= scene.objects.size())
        throw std::out_of_range("Select an entity first");
}

std::uint64_t next_id(Scene const& scene)
{
    std::uint64_t maximum = 0;
    for (auto const& item : scene.objects)
        maximum = std::max(maximum, item.node.id);
    if (maximum == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("No more stable scene IDs are available");
    return maximum + 1;
}

std::string unique_label(std::string const& prefix, std::vector<std::string> const& occupied)
{
    for (unsigned i = 1; i <= 100000; ++i) {
        auto candidate = prefix + " " + std::to_string(i);
        if (std::find(occupied.begin(), occupied.end(), candidate) == occupied.end())
            return candidate;
    }
    throw std::runtime_error("Cannot generate an unused scene name");
}

float bounded_plus(float a, float b)
{
    return std::clamp(a + b, -900000.f, 900000.f);
}
}

Vector3 gizmo_axis(SceneObject const& object, GizmoAxis axis, bool local)
{
    Vector3 result = axis == GizmoAxis::x ? Vector3{1.f,0.f,0.f}
        : axis == GizmoAxis::y ? Vector3{0.f,1.f,0.f} : Vector3{0.f,0.f,1.f};
    if (!local) return result;
    constexpr float rad = 0.01745329251994329577f;
    auto const x = object.node.rotation.x * rad;
    auto const y = object.node.rotation.y * rad;
    auto const z = object.node.rotation.z * rad;
    auto const sx=std::sin(x), cx=std::cos(x);
    auto const sy=std::sin(y), cy=std::cos(y);
    auto const sz=std::sin(z), cz=std::cos(z);
    auto const basis = std::array<std::array<float,3>,3>{{
        {{cz*cy, cz*sy*sx-sz*cx, cz*sy*cx+sz*sx}},
        {{sz*cy, sz*sy*sx+cz*cx, sz*sy*cx-cz*sx}},
        {{-sy, cy*sx, cy*cx}}
    }};
    auto const column = axis == GizmoAxis::x ? 0u : axis == GizmoAxis::y ? 1u : 2u;
    return {basis[0][column], basis[1][column], basis[2][column]};
}

void apply_gizmo(Scene& scene, std::size_t index, GizmoMode mode,
                 GizmoAxis axis, float amount, GizmoOptions const& options)
{
    require_index(scene,index);
    if (!std::isfinite(amount) || std::abs(amount)>900000.f)
        throw std::invalid_argument("Invalid gizmo displacement");
    auto const step = mode == GizmoMode::translate ? options.translation_step :
        mode == GizmoMode::rotate ? options.rotation_step : options.scale_step;
    if (!std::isfinite(step) || step <= 0.f || step>10000.f)
        throw std::invalid_argument("Gizmo snap step must be positive and finite");
    if (mode != GizmoMode::translate && scene.version != 3)
        promote_to_v3(scene);
    auto& object = scene.objects[index];
    float const delta = options.snap ? std::round(amount / step) * step : amount;
    if (mode == GizmoMode::translate) {
        auto const direction=gizmo_axis(object,axis,options.local);
        object.position.x=std::clamp(object.position.x+direction.x*delta,-900000.f,900000.f);
        object.position.y=std::clamp(object.position.y+direction.y*delta,-900000.f,900000.f);
        object.position.z=std::clamp(object.position.z+direction.z*delta,-900000.f,900000.f);
    } else {
        auto* coordinate=axis==GizmoAxis::x ? &object.node.rotation.x :
            axis==GizmoAxis::y ? &object.node.rotation.y : &object.node.rotation.z;
        if (mode == GizmoMode::scale)
            coordinate=axis==GizmoAxis::x ? &object.node.scale.x :
                axis==GizmoAxis::y ? &object.node.scale.y : &object.node.scale.z;
        *coordinate=mode==GizmoMode::scale ?
            std::clamp(*coordinate+delta,0.01f,1000.f) :
            std::clamp(*coordinate+delta,-360000.f,360000.f);
    }
    validate_scene(scene);
}

void promote_to_v3(Scene& scene)
{
    if (scene.version == 3)
        return;
    if (scene.version != 1 && scene.version != 2)
        throw std::runtime_error("Unsupported scene format");
    scene.version = 3;
    std::uint64_t id = 1;
    for (auto& item : scene.objects) {
        item.node = SceneNode{};
        item.node.id = id++;
    }
    validate_scene(scene);
}

std::size_t duplicate(Scene& scene, std::size_t index, bool as_child)
{
    require_index(scene,index);
    if (scene.objects.size() >= max_scene_objects)
        throw std::runtime_error("Scene entity limit reached");
    if (as_child)
        promote_to_v3(scene);

    auto copy = scene.objects[index];
    copy.player = false;
    std::string const suffix = as_child ? " Child" : " Copy";
    if (copy.name.size() + suffix.size() > 96)
        copy.name.resize(96 - suffix.size());
    copy.name += suffix;
    copy.position.x = bounded_plus(copy.position.x, std::max(1.f, copy.size * 1.5f));
    if (scene.version == 3) {
        copy.node.id = next_id(scene);
        if (as_child)
            copy.node.parent = scene.objects[index].node.id;
    }
    scene.objects.push_back(std::move(copy));
    validate_scene(scene);
    return scene.objects.size() - 1;
}

std::size_t delete_subtree(Scene& scene, std::size_t index)
{
    require_index(scene,index);
    std::unordered_set<std::uint64_t> removing;
    if (scene.version == 3) {
        removing.insert(scene.objects[index].node.id);
        // Iterative traversal is bounded and does not recurse on untrusted scene input.
        bool added = true;
        while (added) {
            added = false;
            for (auto const& object : scene.objects) {
                if (object.node.parent &&
                    removing.count(object.node.parent) &&
                    removing.insert(object.node.id).second)
                    added = true;
            }
        }
    }
    if (scene.version == 3) {
        scene.objects.erase(std::remove_if(scene.objects.begin(),scene.objects.end(),
            [&](SceneObject const& object) {
                return removing.count(object.node.id) != 0;
            }),scene.objects.end());
    } else {
        scene.objects.erase(scene.objects.begin()+static_cast<std::ptrdiff_t>(index));
    }
    validate_scene(scene);
    return scene.objects.empty() ? 0 : std::min(index,scene.objects.size()-1);
}

std::size_t create_layer(Scene& scene, std::size_t index)
{
    promote_to_v3(scene);
    require_index(scene,index);
    std::vector<std::string> occupied;
    occupied.reserve(scene.layers.size()+1);
    occupied.push_back("Default");
    for (auto const& layer:scene.layers)
        occupied.push_back(layer.name);
    auto name=unique_label("Layer",occupied);
    scene.layers.push_back({name,true});
    scene.objects[index].node.layer=name;
    validate_scene(scene);
    return scene.layers.size()-1;
}

std::size_t create_cell(Scene& scene, std::size_t index)
{
    promote_to_v3(scene);
    require_index(scene,index);
    std::vector<std::string> occupied;
    for (auto const& cell:scene.cells)
        occupied.push_back(cell.name);
    auto name=unique_label("Cell",occupied);
    auto const p=scene.objects[index].position;
    SceneCell cell;
    cell.name=name;
    cell.minimum={bounded_plus(p.x,-32.f),bounded_plus(p.y,-16.f),bounded_plus(p.z,-32.f)};
    cell.maximum={bounded_plus(p.x,32.f),bounded_plus(p.y,48.f),bounded_plus(p.z,32.f)};
    scene.cells.push_back(cell);
    scene.objects[index].node.cell=name;
    validate_scene(scene);
    return scene.cells.size()-1;
}

void create_volume(Scene& scene, std::size_t index)
{
    promote_to_v3(scene);
    require_index(scene,index);
    std::vector<std::string> occupied;
    for (auto const& volume:scene.volumes)
        occupied.push_back(volume.name);
    SceneVolume volume;
    volume.name=unique_label("Trigger",occupied);
    volume.tag="trigger";
    volume.shape=SceneVolume::Shape::box;
    auto p=scene.objects[index].position;
    volume.minimum={bounded_plus(p.x,-2.f),bounded_plus(p.y,-1.f),bounded_plus(p.z,-2.f)};
    volume.maximum={bounded_plus(p.x, 2.f),bounded_plus(p.y, 3.f),bounded_plus(p.z, 2.f)};
    scene.volumes.push_back(volume);
    validate_scene(scene);
}

void create_portal(Scene& scene, std::size_t index)
{
    promote_to_v3(scene);
    require_index(scene,index);
    std::vector<std::string> occupied;
    for (auto const& portal:scene.portals)
        occupied.push_back(portal.name);
    ScenePortal portal;
    portal.name=unique_label("Portal",occupied);
    portal.room_a="Room A";
    portal.room_b="Room B";
    auto p=scene.objects[index].position;
    portal.minimum={bounded_plus(p.x,-1.f),bounded_plus(p.y,-1.f),bounded_plus(p.z,-.2f)};
    portal.maximum={bounded_plus(p.x, 1.f),bounded_plus(p.y, 2.f),bounded_plus(p.z, .2f)};
    scene.portals.push_back(portal);
    validate_scene(scene);
}

std::string hierarchy_label(Scene const& scene, std::size_t index)
{
    require_index(scene,index);
    auto const& object=scene.objects[index];
    if (scene.version != 3)
        return object.name;
    std::unordered_map<std::uint64_t,std::uint64_t> parents;
    parents.reserve(scene.objects.size());
    for (auto const& node:scene.objects)
        parents[node.node.id]=node.node.parent;
    unsigned depth=0;
    auto parent=object.node.parent;
    while(parent && depth<8) {
        ++depth;
        auto it=parents.find(parent);
        if(it==parents.end())break;
        parent=it->second;
    }
    std::string text(depth*2,' ');
    text+=object.name;
    text+=" ["+std::to_string(object.node.id)+"]";
    if(!object.node.layer.empty() && object.node.layer!="Default")
        text+=" @"+object.node.layer;
    if(!object.node.cell.empty())
        text+=" #"+object.node.cell;
    return text;
}

void History::clear() noexcept
{
    undo_.clear();
    redo_.clear();
}

void History::record(Scene before,std::size_t selected_index)
{
    if(undo_.size()>=limit)
        undo_.erase(undo_.begin());
    undo_.push_back({std::move(before),selected_index});
    redo_.clear();
}

bool History::undo(Scene current,std::size_t selected_index,
                   Scene& restored,std::size_t& restored_selection)
{
    if(undo_.empty())return false;
    if(redo_.size()>=limit)
        redo_.erase(redo_.begin());
    auto previous=std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({std::move(current),selected_index});
    restored=std::move(previous.scene);
    restored_selection=previous.selected_index;
    return true;
}

bool History::redo(Scene current,std::size_t selected_index,
                   Scene& restored,std::size_t& restored_selection)
{
    if(redo_.empty())return false;
    if(undo_.size()>=limit)
        undo_.erase(undo_.begin());
    auto next=std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({std::move(current),selected_index});
    restored=std::move(next.scene);
    restored_selection=next.selected_index;
    return true;
}
} // namespace northstarcpp::editor_tools
