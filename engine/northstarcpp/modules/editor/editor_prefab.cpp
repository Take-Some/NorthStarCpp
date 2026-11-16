// NorthStarCpp Scene V3 prefab capture and transactional instancing.
// Copyright (C) 2026 Kayla's Systems - Kayla Verner.
#include "editor_prefab.hpp"
#include "editor_tools.hpp"
#include <northstarcpp/player_blueprint.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace northstarcpp::editor_tools {
namespace {

Vector3 moved(Vector3 original, Vector3 offset, bool add) {
    auto calc = [add](float a, float b) -> float {
        double const v = add ? double(a) + double(b) : double(a) - double(b);
        if (!std::isfinite(v) || std::abs(v)>900000.0)
            throw std::invalid_argument("Prefab position exceeds world bounds");
        return static_cast<float>(v);
    };
    return {calc(original.x,offset.x), calc(original.y,offset.y), calc(original.z,offset.z)};
}
void check_anchor(Vector3 value) {
    for (float p: {value.x,value.y,value.z})
        if (!std::isfinite(p) || std::abs(p)>900000.f)
            throw std::invalid_argument("Prefab anchor must be finite and in bounds");
}
std::string cell_name(std::string const& desired,
                      std::unordered_set<std::string> const& occupied) {
    if (!occupied.count(desired)) return desired;
    for (unsigned i=1;i<=100000u;++i) {
        std::string prefix=desired;
        std::string suffix="_"+std::to_string(i);
        if (prefix.size()+suffix.size()>96)
            prefix.resize(96-suffix.size());
        std::string candidate=prefix+suffix;
        if (!occupied.count(candidate)) return candidate;
    }
    throw std::runtime_error("Exhausted prefab streaming-cell names");
}

} // namespace

Scene capture_prefab(Scene const& source,std::size_t root_index) {
    if (root_index>=source.objects.size())
        throw std::out_of_range("Select an entity to capture as prefab");
    Scene original=source;
    promote_to_v3(original);
    validate_scene(original);
    auto const root_id=original.objects[root_index].node.id;
    auto const anchor=original.objects[root_index].position;
    check_anchor(anchor);

    std::unordered_set<std::uint64_t> included{root_id};
    // Parent cycles have already been rejected by validate_scene.
    bool changed=true;
    while(changed) {
        changed=false;
        for (auto const& o:original.objects) {
            if (o.node.parent && included.count(o.node.parent))
                changed |= included.insert(o.node.id).second;
        }
    }
    Scene prefab;
    prefab.version=3;
    prefab.name=original.objects[root_index].name;
    prefab.streaming=original.streaming;
    prefab.objects.reserve(included.size());
    std::unordered_set<std::string> layers;
    std::unordered_set<std::string> cells;
    // Root first is important for deterministic instance result indices.
    auto append = [&](SceneObject object) {
        if (object.node.id==root_id) object.node.parent=0;
        object.position=moved(object.position,anchor,false);
        layers.insert(object.node.layer);
        if (!object.node.cell.empty()) cells.insert(object.node.cell);
        prefab.objects.push_back(std::move(object));
    };
    append(original.objects[root_index]);
    for (std::size_t i=0;i<original.objects.size();++i)
        if (i!=root_index && included.count(original.objects[i].node.id))
            append(original.objects[i]);
    for (auto const& layer:original.layers)
        if (layers.count(layer.name)) prefab.layers.push_back(layer);
    for (auto cell:original.cells)
        if (cells.count(cell.name)) {
            cell.minimum=moved(cell.minimum,anchor,false);
            cell.maximum=moved(cell.maximum,anchor,false);
            prefab.cells.push_back(std::move(cell));
        }
    // Prefab components are owned by stable template entity IDs.
    // Preserve collision, materials and provider-owned ECS payloads.
    bool const is_player_blueprint=
        player_blueprint::layout(original,root_id).has_value();
    for(auto const& component:original.components)
        if(included.count(component.entity_id) &&
           (component.type!=player_character::pawn_type||is_player_blueprint))
            prefab.components.push_back(component);
    // Global triggers/portals have no entity owner in Scene V3 and should not
    // be silently duplicated into unrelated world locations.
    validate_scene(prefab);
    return prefab;
}

std::size_t instantiate_prefab(Scene& destination,Scene const& prefab,
                               Vector3 anchor,std::uint64_t parent_id) {
    check_anchor(anchor);
    validate_scene(prefab);
    if (prefab.version!=3 || prefab.objects.empty())
        throw std::invalid_argument("Prefab must be a non-empty Scene V3 document");
    std::uint64_t template_root=0;
    std::unordered_set<std::uint64_t> source_ids;
    for (auto const& entity:prefab.objects) {
        source_ids.insert(entity.node.id);
        if (entity.node.parent==0) {
            if (template_root)
                throw std::invalid_argument("Prefab must have exactly one hierarchy root");
            template_root=entity.node.id;
        }
    }
    if (!template_root) throw std::invalid_argument("Prefab root is missing");
    if (prefab.objects.size()>max_scene_objects ||
        destination.objects.size()>max_scene_objects-prefab.objects.size())
        throw std::length_error("Prefab instancing exceeds the scene object limit");

    Scene result=destination; // Strong exception safety: publish only after validation.
    promote_to_v3(result);
    std::uint64_t id_max=0;
    bool parent_exists=parent_id==0;
    for (auto const& entity:result.objects) {
        id_max=std::max(id_max,entity.node.id);
        parent_exists |= entity.node.id==parent_id;
    }
    if (!parent_exists)
        throw std::invalid_argument("Selected prefab parent is missing");
    if (id_max>std::numeric_limits<std::uint64_t>::max()-prefab.objects.size())
        throw std::overflow_error("No stable entity IDs available for prefab");

    // Layer references are globally named; existing destination layers take
    // precedence, preserving the destination's visibility settings.
    std::unordered_set<std::string> layers{"Default"};
    for (auto const& layer:result.layers) layers.insert(layer.name);
    for (auto const& layer:prefab.layers)
        if (layers.insert(layer.name).second) result.layers.push_back(layer);

    // Every instance owns distinct streaming cells; otherwise moving copies
    // would accidentally share sector bounds of earlier instances.
    std::unordered_set<std::string> occupied_cells;
    std::unordered_map<std::string,std::string> cell_remap;
    for (auto const& cell:result.cells) occupied_cells.insert(cell.name);
    for (auto cell:prefab.cells) {
        std::string old=cell.name;
        cell.name=cell_name(old,occupied_cells);
        occupied_cells.insert(cell.name);
        cell_remap.emplace(old,cell.name);
        cell.minimum=moved(cell.minimum,anchor,true);
        cell.maximum=moved(cell.maximum,anchor,true);
        result.cells.push_back(std::move(cell));
    }
    std::unordered_map<std::uint64_t,std::uint64_t> ids;
    ids.reserve(prefab.objects.size());
    for (auto const& item:prefab.objects)
        ids.emplace(item.node.id,++id_max);

    std::size_t root_index=result.objects.size();
    for (auto entity:prefab.objects) {
        std::uint64_t const original_id=entity.node.id;
        entity.node.id=ids.at(original_id);
        entity.node.parent=original_id==template_root
            ? parent_id : ids.at(entity.node.parent);
        if (!entity.node.cell.empty())
            entity.node.cell=cell_remap.at(entity.node.cell);
        entity.position=moved(entity.position,anchor,true);
        if (original_id==template_root) root_index=result.objects.size();
        result.objects.push_back(std::move(entity));
    }
    for(auto component:prefab.components) {
        if(!component.entity_id || !ids.count(component.entity_id))
            throw std::invalid_argument("Prefab component has an invalid entity ID");
        component.entity_id=ids.at(component.entity_id);
        result.components.push_back(std::move(component));
    }
    validate_scene(result);
    destination=std::move(result);
    return root_index;
}

} // namespace northstarcpp::editor_tools
