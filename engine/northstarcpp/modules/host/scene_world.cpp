// NorthStarCpp
// Copyright (c) 2026 Kayla's Systems - Kayla Verner.
// Independent scene spatial query + deterministic streaming implementation.

#include <northstarcpp/scene_world.hpp>
#include "scene_portal_polygon.hpp"
#include <northstarcpp/scene_pawn.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <deque>
#include <map>
#include <tuple>
#include <limits>
#include <stdexcept>
#include <utility>

namespace northstarcpp {
namespace {

bool finite(Vector3 const& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool ordered(Vector3 const& a, Vector3 const& b)
{
    return a.x <= b.x && a.y <= b.y && a.z <= b.z;
}
float squared(float v) { return v * v; }
float distance_sq(Vector3 a, Vector3 b)
{
    return squared(a.x-b.x) + squared(a.y-b.y) + squared(a.z-b.z);
}
float box_distance_sq(Vector3 point, Vector3 a, Vector3 b)
{
    auto axis = [](float p, float lo, float hi) {
        if (p < lo) return lo-p;
        if (p > hi) return p-hi;
        return 0.f;
    };
    return squared(axis(point.x,a.x,b.x)) +
        squared(axis(point.y,a.y,b.y)) + squared(axis(point.z,a.z,b.z));
}
bool box_overlaps(Vector3 a_min, Vector3 a_max, Vector3 b_min, Vector3 b_max)
{
    return a_min.x <= b_max.x && a_max.x >= b_min.x &&
        a_min.y <= b_max.y && a_max.y >= b_min.y &&
        a_min.z <= b_max.z && a_max.z >= b_min.z;
}
// Conservative AABB/plane rejection: if the vertex furthest along the
// plane normal is outside, no point of this portal is in the camera view.
bool portal_intersects_frustum(ScenePortal const& portal,SceneViewFrustum const& frustum)
{
    for (auto const& plane:frustum.planes) {
        auto const point=Vector3{
            plane.normal.x>=0.f?portal.maximum.x:portal.minimum.x,
            plane.normal.y>=0.f?portal.maximum.y:portal.minimum.y,
            plane.normal.z>=0.f?portal.maximum.z:portal.minimum.z};
        double const signed_distance=double(plane.normal.x)*point.x+
            double(plane.normal.y)*point.y+double(plane.normal.z)*point.z+
            plane.offset;
        if(signed_distance < -1.e-5)return false;
    }
    return true;
}
// Prefer an authored convex polygon from a scene-level NEF8 component.
// Legacy AABB portals use the thinnest axis as a rectangular aperture;
// both variants emit camera-to-edge clipping half-spaces.
std::vector<SceneViewPlane> portal_aperture_planes(
    ScenePortal const& portal,Vector3 camera,
    std::vector<Vector3> const* polygon=nullptr)
{
    auto const& lo=portal.minimum;
    auto const& hi=portal.maximum;
    auto const dx=hi.x-lo.x,dy=hi.y-lo.y,dz=hi.z-lo.z;
    Vector3 center{(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,(lo.z+hi.z)*0.5f};
    std::vector<Vector3> corners;
    if(polygon && polygon->size()>=3) {
        corners=*polygon;
        center={0.f,0.f,0.f};
        for(auto const& v:corners){
            center.x+=v.x;center.y+=v.y;center.z+=v.z;
        }
        auto const scale=1.f/float(corners.size());
        center={center.x*scale,center.y*scale,center.z*scale};
    }else if(dx<=dy && dx<=dz) {
        corners={Vector3{center.x,lo.y,lo.z},Vector3{center.x,hi.y,lo.z},
                 Vector3{center.x,hi.y,hi.z},Vector3{center.x,lo.y,hi.z}};
    } else if(dy<=dz) {
        corners={Vector3{lo.x,center.y,lo.z},Vector3{hi.x,center.y,lo.z},
                 Vector3{hi.x,center.y,hi.z},Vector3{lo.x,center.y,hi.z}};
    } else {
        corners={Vector3{lo.x,lo.y,center.z},Vector3{hi.x,lo.y,center.z},
                 Vector3{hi.x,hi.y,center.z},Vector3{lo.x,hi.y,center.z}};
    }
    std::vector<SceneViewPlane> result;
    result.reserve(corners.size());
    auto const toward=Vector3{center.x-camera.x,center.y-camera.y,
                              center.z-camera.z};
    for(std::size_t i=0;i<corners.size();++i) {
        auto const& a=corners[i];
        auto const& b=corners[(i+1)%corners.size()];
        Vector3 const u{a.x-camera.x,a.y-camera.y,a.z-camera.z};
        Vector3 const v{b.x-camera.x,b.y-camera.y,b.z-camera.z};
        Vector3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        double const len=std::sqrt(double(n.x)*n.x+double(n.y)*n.y+
                                   double(n.z)*n.z);
        if(len<1.e-8)continue; // degenerate or camera within aperture
        auto const inward=double(n.x)*toward.x+double(n.y)*toward.y+
                          double(n.z)*toward.z;
        auto const sign=inward<0.0?-1.0:1.0;
        n={float(n.x*sign/len),float(n.y*sign/len),float(n.z*sign/len)};
        result.push_back({n,-(n.x*camera.x+n.y*camera.y+n.z*camera.z)});
    }
    return result;
}

bool portal_intersects_clip(ScenePortal const& portal,
                            std::vector<SceneViewPlane> const& clip)
{
    for(auto const& plane:clip) {
        Vector3 const point{
            plane.normal.x>=0.f?portal.maximum.x:portal.minimum.x,
            plane.normal.y>=0.f?portal.maximum.y:portal.minimum.y,
            plane.normal.z>=0.f?portal.maximum.z:portal.minimum.z};
        double const d=double(plane.normal.x)*point.x+
            double(plane.normal.y)*point.y+double(plane.normal.z)*point.z+
            plane.offset;
        if(d < -1.e-5)return false;
    }
    return true;
}

int grid_coord(float world, float grid)
{
    auto const result = std::floor(static_cast<double>(world) / grid);
    if (!std::isfinite(result) || result < static_cast<double>(INT32_MIN) ||
        result > static_cast<double>(INT32_MAX))
        throw std::out_of_range("Invalid scene spatial index coordinate");
    return static_cast<int>(result);
}
Vector3 subtract(Vector3 a, Vector3 b)
{
    return {a.x-b.x,a.y-b.y,a.z-b.z};
}
Vector3 add(Vector3 a, Vector3 b)
{
    return {a.x+b.x,a.y+b.y,a.z+b.z};
}
Vector3 scaled(Vector3 a, float t)
{
    return {a.x*t,a.y*t,a.z*t};
}
float extent(SceneObject const& object)
{
    return object.size * 0.5f * std::max({object.node.scale.x,
        object.node.scale.y,object.node.scale.z});
}
float cell_distance_sq(Vector3 position, SceneCell const& cell)
{
    return box_distance_sq(position,cell.minimum,cell.maximum);
}

} // namespace

std::size_t SceneSpatialIndex::Hash::operator()(CellKey const& key) const
{
    auto const a=static_cast<std::uint32_t>(key.x);
    auto const b=static_cast<std::uint32_t>(key.z);
    return (static_cast<std::size_t>(a)*0x9e3779b185ebca87ull) ^
           (static_cast<std::size_t>(b)*0xc2b2ae3d27d4eb4full);
}

SceneSpatialIndex::SceneSpatialIndex(Scene const& scene, float grid_size)
    : grid_size_(grid_size)
{
    validate_scene(scene);
    if (!std::isfinite(grid_size) || grid_size < 1.f || grid_size > 100000.f)
        throw std::invalid_argument("Scene spatial cell width is invalid");
    objects_.reserve(scene.objects.size());
    cells_.reserve(scene.objects.size());
    object_cells_.reserve(scene.objects.size());
    object_slots_.reserve(scene.objects.size());
    staged_cells_.reserve(scene.objects.size());
    for (std::size_t i=0;i<scene.objects.size();++i) {
        auto const& object=scene.objects[i];
        float const half=extent(object);
        objects_.push_back({object.node.id,object.position,half});
        CellKey key{grid_coord(object.position.x,grid_size_),
                    grid_coord(object.position.z,grid_size_)};
        object_cells_.push_back(key);
        auto& bucket=cells_[key];
        object_slots_.push_back(bucket.size());
        bucket.push_back(i);
        maximum_extent_ = std::max(maximum_extent_,half);
    }
}

void SceneSpatialIndex::update_dynamic_positions(Scene const& live_scene)
{
    if (live_scene.objects.size()!=objects_.size())
        throw std::invalid_argument("Live scene entity count differs from authored scene");
    staged_cells_.clear();
    // Validate the entire update before changing any bucket. Invalid or
    // reordered runtime data must not corrupt the spatial index.
    for (std::size_t i=0;i<objects_.size();++i) {
        auto const& live=live_scene.objects[i];
        if (objects_[i].id != live.node.id)
            throw std::invalid_argument("Live scene stable ID ordering changed");
        if (!finite(live.position) ||
            std::max({std::abs(live.position.x),std::abs(live.position.y),
                      std::abs(live.position.z)})>1000000.f)
            throw std::invalid_argument("Live scene position exceeds world bounds");
        staged_cells_.push_back({grid_coord(live.position.x,grid_size_),
                           grid_coord(live.position.z,grid_size_)});
    }
    for (std::size_t i=0;i<objects_.size();++i) {
        auto const& next=staged_cells_[i];
        auto const& previous=object_cells_[i];
        if (!(next==previous)) {
            // Allocate destination before modifying the source bucket.
            auto& destination=cells_[next];
            auto const new_slot=destination.size();
            destination.push_back(i);
            auto bucket=cells_.find(previous);
            if (bucket!=cells_.end()) {
                auto& indices=bucket->second;
                auto const slot=object_slots_[i];
                auto const moved=indices.back();
                indices[slot]=moved;
                object_slots_[moved]=slot;
                indices.pop_back();
                if (indices.empty()) cells_.erase(bucket);
            }
            object_slots_[i]=new_slot;
            object_cells_[i]=next;
        }
        objects_[i].position=live_scene.objects[i].position;
    }
}

std::vector<std::size_t> SceneSpatialIndex::query_box(Vector3 minimum, Vector3 maximum) const
{
    if (!finite(minimum) || !finite(maximum) || !ordered(minimum,maximum))
        throw std::invalid_argument("Invalid scene bounding box query");
    std::vector<std::size_t> result;
    auto matches=[&](std::size_t index) {
        auto const& object=objects_[index];
        auto const half=object.half_extent;
        auto const pad=Vector3{half,half,half};
        if (box_overlaps(minimum,maximum,subtract(object.position,pad),
                          add(object.position,pad)))
            result.push_back(index);
    };
    auto const low_x=grid_coord(minimum.x-maximum_extent_,grid_size_);
    auto const high_x=grid_coord(maximum.x+maximum_extent_,grid_size_);
    auto const low_z=grid_coord(minimum.z-maximum_extent_,grid_size_);
    auto const high_z=grid_coord(maximum.z+maximum_extent_,grid_size_);
    std::int64_t count=(static_cast<std::int64_t>(high_x)-low_x+1) *
                       (static_cast<std::int64_t>(high_z)-low_z+1);
    if (count > static_cast<std::int64_t>(cells_.size())*4+32) {
        for (size_t i=0;i<objects_.size();++i) matches(i);
    } else {
        for (int64_t x=low_x;x<=high_x;++x)
            for (int64_t z=low_z;z<=high_z;++z) {
                auto const bucket=cells_.find({static_cast<int>(x),static_cast<int>(z)});
                if (bucket!=cells_.end())
                    for (auto idx:bucket->second) matches(idx);
            }
    }
    std::sort(result.begin(),result.end());
    return result;
}

std::vector<std::size_t> SceneSpatialIndex::query_sphere(Vector3 center, float radius) const
{
    if (!finite(center) || !std::isfinite(radius) || radius < 0.f ||
        radius > 1000000.f)
        throw std::invalid_argument("Invalid scene sphere query");
    auto const r=Vector3{radius,radius,radius};
    auto candidates=query_box(subtract(center,r),add(center,r));
    candidates.erase(std::remove_if(candidates.begin(),candidates.end(),
        [&](std::size_t index) {
            auto const& object=objects_[index];
            auto const half=object.half_extent;
            auto const pad=Vector3{half,half,half};
            return box_distance_sq(center,subtract(object.position,pad),
                                    add(object.position,pad)) > radius*radius;
        }),candidates.end());
    return candidates;
}

SceneWorld::SceneWorld(Scene scene)
    : scene_(std::move(scene)), spatial_(scene_)
{
    validate_scene(scene_);
    for (size_t i=0;i<scene_.cells.size();++i)
        cell_lookup_.emplace(scene_.cells[i].name,i);
    for (size_t i=0;i<scene_.objects.size();++i)
        if (scene_.objects[i].node.id != 0)
            id_lookup_.emplace(scene_.objects[i].node.id,i);
    // Authored convex portal outlines ride within existing NEF8 scene-level
    // XML component blobs. Invalid optional geometry fails open to the AABB.
    for(auto const& component:scene_.components) {
        if(component.type!="engine.scene.portal_polygon" ||
           component.entity_id!=0 || component.version!=1)continue;
        try {
            auto parsed=scene_portal_polygon::parse(component.payload);
            auto const found=std::find_if(scene_.portals.begin(),scene_.portals.end(),
                [&](ScenePortal const& portal){return portal.name==parsed.name;});
            if(found==scene_.portals.end())
                throw std::invalid_argument("polygon references unknown portal");
            portal_polygons_[parsed.name]=std::move(parsed.vertices);
        }catch(std::exception const& e) {
            std::fprintf(stderr,"[WARN] [scene.portal] Invalid polygon component: %s\n",
                         e.what());
        }
    }
    for(std::size_t i=0;i<scene_.portals.size();++i) {
        auto const& portal=scene_.portals[i];
        portals_by_room_[portal.room_a].push_back(i);
        portals_by_room_[portal.room_b].push_back(i);
    }
    cell_telemetry_.reserve(scene_.cells.size());
    for (auto const& cell:scene_.cells)
        cell_telemetry_.push_back(SceneCellTelemetry{cell.name});
    loaded_cells_.resize(scene_.cells.size(),0);
    payload_ready_.resize(scene_.cells.size(),1);
    effective_cell_indices_.assign(scene_.objects.size(),
        std::numeric_limits<std::size_t>::max());
    last_live_positions_.reserve(scene_.objects.size());
    for (std::size_t i=0;i<scene_.objects.size();++i) {
        auto const& object=scene_.objects[i];
        last_live_positions_.push_back(object.position);
        auto const cell=cell_lookup_.find(object.node.cell);
        if (cell!=cell_lookup_.end())
            effective_cell_indices_[i]=cell->second;
    }
    // One-time authored cell costs; runtime residency is updated each frame.
    std::vector<std::unordered_set<std::string>> cell_assets(scene_.cells.size());
    for(std::size_t i=0;i<scene_.objects.size();++i) {
        auto const owner=effective_cell_indices_[i];
        if(owner==std::numeric_limits<std::size_t>::max())continue;
        ++cell_telemetry_[owner].authored_entities;
        auto const& asset=scene_.objects[i].node.asset;
        if(!asset.empty())cell_assets[owner].insert(asset);
    }
    for(std::size_t i=0;i<cell_assets.size();++i)
        cell_telemetry_[i].unique_authored_assets=cell_assets[i].size();
    resident_mask_.resize(scene_.objects.size(),0);
    visible_mask_.resize(scene_.objects.size(),0);

    // Build two levels of deterministic static-object HLOD clusters.
    // Group only objects with matching cell/layer/room, so a closed portal
    // or hidden layer cannot leak grouped geometry into another area.
    if (scene_.version>=3) {
        using Key=std::tuple<int,std::string,std::string,std::string,int,int>;
        std::map<Key,std::vector<std::size_t>> groups;
        for (std::size_t i=0;i<scene_.objects.size();++i) {
            auto const& o=scene_.objects[i];
            if (scene_pawn::is_pawn(scene_,o) || o.mass>0.f || !o.node.visible)
                continue;
            for (int level=1;level<=2;++level) {
                float const width=level==1 ? 64.f : 256.f;
                int const gx=grid_coord(o.position.x,width);
                int const gz=grid_coord(o.position.z,width);
                groups[{level,o.node.cell,o.node.layer,o.node.room,gx,gz}]
                    .push_back(i);
            }
        }
        for (auto const& entry:groups) {
            int const level=std::get<0>(entry.first);
            std::size_t const minimum_count=level==1 ? 3u : 8u;
            if (entry.second.size()<minimum_count)
                continue;
            Vector3 lo{1000000.f,1000000.f,1000000.f};
            Vector3 hi{-1000000.f,-1000000.f,-1000000.f};
            std::uint64_t red=0,green=0,blue=0;
            for (auto const index:entry.second) {
                auto const& o=scene_.objects[index];
                float const half=extent(o);
                lo.x=std::min(lo.x,o.position.x-half);
                lo.y=std::min(lo.y,o.position.y-half);
                lo.z=std::min(lo.z,o.position.z-half);
                hi.x=std::max(hi.x,o.position.x+half);
                hi.y=std::max(hi.y,o.position.y+half);
                hi.z=std::max(hi.z,o.position.z+half);
                red+=(o.color>>16)&255;
                green+=(o.color>>8)&255;
                blue+=o.color&255;
            }
            auto const count=entry.second.size();
            SceneHlodProxy proxy;
            proxy.center={(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,
                          (lo.z+hi.z)*0.5f};
            proxy.half_extent={std::max(0.02f,(hi.x-lo.x)*0.5f),
                               std::max(0.02f,(hi.y-lo.y)*0.5f),
                               std::max(0.02f,(hi.z-lo.z)*0.5f)};
            proxy.color=(static_cast<std::uint32_t>(red/count)<<16) |
                        (static_cast<std::uint32_t>(green/count)<<8) |
                         static_cast<std::uint32_t>(blue/count);
            proxy.level=static_cast<std::uint32_t>(level);
            proxy.member_count=count;
            proxy.members=entry.second;
            hlod_clusters_.push_back(std::move(proxy));
        }
        // Coarse clusters own their members first. Fine clusters cover only
        // uncovered members, avoiding duplicate proxy draws.
        std::stable_sort(hlod_clusters_.begin(),hlod_clusters_.end(),
            [](SceneHlodProxy const& a,SceneHlodProxy const& b) {
                return a.level>b.level;
            });
    }
}

void SceneWorld::update_cell_selection(Vector3 position,Vector3 predicted)
{
    const auto equal=[](Vector3 a,Vector3 b) {
        return a.x==b.x && a.y==b.y && a.z==b.z;
    };
    transitions_.clear();
    if(!selection_valid_ || !equal(position,selection_position_) ||
       !equal(predicted,selection_prediction_)) {
        selection_valid_=false;
        cell_loads_.clear();
        cell_unloads_.clear();
        next_load_=next_unload_=0;
        for (std::size_t i=0;i<scene_.cells.size();++i) {
            auto const& cell=scene_.cells[i];
            auto const loaded=loaded_cells_[i]!=0;
            auto const range=loaded ? scene_.streaming.unload_radius :
                                       scene_.streaming.load_radius;
            auto const range_sq=range*range;
            auto const distance_current=cell_distance_sq(position,cell);
            auto const distance_predicted=cell_distance_sq(predicted,cell);
            auto const forced=cell.always_loaded || pinned_.count(cell.name)!=0;
            bool const needed=forced || distance_current<=range_sq ||
                distance_predicted<=range_sq;
            if (needed && !loaded) {
                // Higher priority wins. A nearby cell always outranks a lookahead
                // cell at the same distance, even when speed is very high.
                float const now_priority=distance_current<=range_sq ?
                    100000.f / (1.f+std::sqrt(distance_current)) : 0.f;
                float const ahead_priority=distance_predicted<=range_sq ?
                    30000.f / (1.f+std::sqrt(distance_predicted)) : 0.f;
                cell_loads_.push_back({i,forced ? 1000000.f :
                    now_priority+ahead_priority,forced});
            } else if (!needed && loaded) {
                // Evict farthest first, retaining nearer scene collision/visual
                // placeholders as long as the current budget allows.
                cell_unloads_.push_back({i,std::sqrt(std::min(distance_current,distance_predicted)),false});
            }
        }
        auto priority_order=[](CellCandidate const& lhs,CellCandidate const& rhs) {
            if (lhs.priority != rhs.priority)
                return lhs.priority > rhs.priority;
            return lhs.index < rhs.index;
        };
        std::sort(cell_loads_.begin(),cell_loads_.end(),priority_order);
        std::sort(cell_unloads_.begin(),cell_unloads_.end(),priority_order);
        selection_position_=position;
        selection_prediction_=predicted;
        selection_valid_=true;
        ++diagnostics_.cell_selection_passes;
    }

    // For stationary sources, keep the sorted work until its budgets drain.
    // Load/unload hysteresis guarantees a completed transition cannot
    // create an opposite candidate until the position or pin set changes.
    const auto unloads=std::min<std::size_t>(scene_.streaming.max_cell_unloads_per_update,
                                            cell_unloads_.size()-next_unload_);
    for(std::size_t i=0;i<unloads;++i) {
        const auto& entry=cell_unloads_[next_unload_];
        transitions_.push_back({scene_.cells[entry.index].name,false,false,entry.priority});
        loaded_cells_[entry.index]=0;
        --diagnostics_.loaded_cells;
        ++next_unload_;
    }
    std::size_t regular_loads=0;
    while(next_load_<cell_loads_.size()) {
        const auto& entry=cell_loads_[next_load_];
        if(!entry.forced && regular_loads>=scene_.streaming.max_cell_loads_per_update)
            break;
        transitions_.push_back({scene_.cells[entry.index].name,true,entry.forced,entry.priority});
        loaded_cells_[entry.index]=1;
        ++diagnostics_.loaded_cells;
        if(!entry.forced)++regular_loads;
        ++next_load_;
    }
    diagnostics_.pending_loads=cell_loads_.size()-next_load_;
    diagnostics_.pending_unloads=cell_unloads_.size()-next_unload_;
}

void SceneWorld::update(Vector3 camera_position, Vector3 camera_velocity,
                        std::string current_room, Scene const* live_scene)
{
    auto const update_started=std::chrono::steady_clock::now();
    diagnostics_.portals_frustum_rejected=0;
    if (!finite(camera_position) || !finite(camera_velocity))
        throw std::invalid_argument("Scene streaming view contains non-finite coordinates");
    if (live_scene) {
        spatial_.update_dynamic_positions(*live_scene);
        // Reassign entities that have actually moved into another authored
        // streaming cell. Cell membership is runtime-only and never changes
        // their stable ECS IDs or persistent project-file metadata.
        auto const npos=std::numeric_limits<std::size_t>::max();
        for (std::size_t i=0;i<scene_.objects.size();++i) {
            auto const& object=scene_.objects[i];
            auto const position=live_scene->objects[i].position;
            auto const previous=last_live_positions_[i];
            bool const moved=position.x!=previous.x || position.y!=previous.y ||
                             position.z!=previous.z;
            last_live_positions_[i]=position;
            if (!moved || scene_pawn::is_pawn(scene_,object) || object.node.cell.empty())
                continue;
            auto index=effective_cell_indices_[i];
            if (index!=npos &&
                box_distance_sq(position,scene_.cells[index].minimum,
                                scene_.cells[index].maximum)==0.f)
                continue;
            // Crossing a cell boundary is rare compared with frame updates;
            // scan only on those crossings, not for every object each frame.
            index=npos;
            for (std::size_t j=0;j<scene_.cells.size();++j) {
                if (box_distance_sq(position,scene_.cells[j].minimum,
                                    scene_.cells[j].maximum)==0.f) {
                    index=j;
                    break;
                }
            }
            // No valid cell: keep the entity persistent until re-entry rather
            // than letting a moving NPC or vehicle disappear in empty space.
            effective_cell_indices_[i]=index;
        }
    }
    auto predicted=add(camera_position,
        scaled(camera_velocity,scene_.streaming.lookahead_seconds));

    update_cell_selection(camera_position,predicted);

    // Interior visibility is a graph traversal over open portals. Explicit
    // current_room selects a connected component; empty means no restriction.
    std::unordered_set<std::string> accessible;
    if (!current_room.empty()) {
        std::deque<std::string> pending;
        accessible.insert(current_room);
        pending.push_back(current_room);
        while (!pending.empty()) {
            auto room=std::move(pending.front());
            pending.pop_front();
            auto const edges=portals_by_room_.find(room);
            if(edges==portals_by_room_.end())continue;
            for(auto const portal_index:edges->second) {
                auto const& portal=scene_.portals[portal_index];
                if (!portal.open)
                    continue;
                // Use the camera frustum only when provided. This is an
                // AABB rejection gate, not polygonal portal clip recursion.
                if (view_frustum_ && !portal_intersects_frustum(portal,*view_frustum_)) {
                    ++diagnostics_.portals_frustum_rejected;
                    continue;
                }
                std::string const* next=nullptr;
                if (portal.room_a == room) next=&portal.room_b;
                else if (portal.room_b == room) next=&portal.room_a;
                if (next && accessible.insert(*next).second)
                    pending.push_back(*next);
            }
        }
    }

    // Recursively restrict each downstream portal by the aperture already
    // traversed. Distinct camera-to-room paths have distinct clip volumes;
    // do not mark a room closed after its first visible entrance.
    if(view_frustum_ && !current_room.empty()) {
        struct ClipState {
            std::string room;
            std::vector<SceneViewPlane> clip;
            std::vector<std::size_t> traversed;
        };
        std::deque<ClipState> queue;
        queue.push_back({current_room,
            {view_frustum_->planes.begin(),view_frustum_->planes.end()}, {}});
        std::unordered_set<std::string> clipped_rooms{current_room};
        std::size_t visited_paths=0;
        bool overflow=false;
        while(!queue.empty()) {
            if(++visited_paths>2048) {overflow=true;break;}
            auto node=std::move(queue.front());
            queue.pop_front();
            if(node.traversed.size()>=24) {overflow=true;break;}
            auto const edges=portals_by_room_.find(node.room);
            if(edges==portals_by_room_.end())continue;
            for(auto const id:edges->second) {
                if(std::find(node.traversed.begin(),node.traversed.end(),id)!=
                   node.traversed.end())continue;
                auto const& portal=scene_.portals[id];
                if(!portal.open)continue;
                if(!portal_intersects_clip(portal,node.clip)) {
                    ++diagnostics_.portals_frustum_rejected;
                    continue;
                }
                auto const& target=portal.room_a==node.room?
                    portal.room_b:portal.room_a;
                clipped_rooms.insert(target);
                auto const outline=portal_polygons_.find(portal.name);
                auto side_planes=portal_aperture_planes(portal,camera_position,
                    outline==portal_polygons_.end()?nullptr:&outline->second);
                auto child=node.clip;
                child.insert(child.end(),side_planes.begin(),side_planes.end());
                auto used=node.traversed;
                used.push_back(id);
                queue.push_back({target,std::move(child),std::move(used)});
                // Preserve the parent clip for its other outgoing portals.
            }
        }
        // Fail open on path explosion rather than silently deleting rooms.
        if(!overflow)accessible=std::move(clipped_rooms);
    }

    std::unordered_map<std::string,bool> layer_visibility{{"Default",true}};
    for (auto const& layer:scene_.layers)
        layer_visibility[layer.name]=layer.visible;

    resident_indices_.clear();
    visible_indices_.clear();
    std::fill(visible_mask_.begin(),visible_mask_.end(),0);
    auto& local=visible_mask_;
    for (size_t i=0;i<scene_.objects.size();++i) {
        auto const& object=scene_.objects[i];
        auto const& node=object.node;
        auto const owner=effective_cell_indices_[i];
        // WorldPartition's desired logical cell state is not evidence of a
        // successfully read NEF8 payload. Static entities wait for decoded
        // data; player and dynamic traffic must not be evicted mid-simulation.
        bool const resident=scene_pawn::is_pawn(scene_,object) || object.mass>0.f ||
            owner==std::numeric_limits<std::size_t>::max() ||
            (loaded_cells_[owner]!=0 &&
             (!payload_gate_ || payload_ready_[owner]!=0));
        resident_mask_[i]=static_cast<std::uint8_t>(resident);
        if (!resident)
            continue;
        resident_indices_.push_back(i);
        bool const room_allowed=current_room.empty() ||
            (node.room.empty() ? accessible.count("Exterior")!=0 :
                                 accessible.count(node.room)!=0);
        auto const& position=live_scene ? live_scene->objects[i].position :
                                        object.position;
        auto const distance=std::sqrt(distance_sq(position,camera_position));
        auto const layer=layer_visibility.find(node.layer);
        bool const visible=scene_pawn::is_pawn(scene_,object) || (node.visible &&
            (layer==layer_visibility.end() || layer->second) &&
            room_allowed && distance>=node.lod_near && distance<=node.lod_far);
        local[i]=static_cast<std::uint8_t>(visible);
    }

    // Hierarchical visibility: a child cannot be visible while its parent is
    // hidden, but persistent/player root entities remain resident.
    hierarchy_state_.assign(local.size(),0);
    auto& state=hierarchy_state_;
    auto resolve=[&](auto&& self, std::size_t index)->bool {
        if (state[index] == 2) return local[index]!=0;
        if (state[index] == 1)
            throw std::runtime_error("Unexpected runtime scene hierarchy cycle");
        state[index]=1;
        auto const parent=scene_.objects[index].node.parent;
        if (parent!=0 && local[index]) {
            auto it=id_lookup_.find(parent);
            if (it==id_lookup_.end() || !self(self,it->second))
                local[index]=0;
        }
        state[index]=2;
        return local[index]!=0;
    };
    for (size_t i=0;i<scene_.objects.size();++i) {
        (void)resolve(resolve,i);
    }

    // Select the first (coarsest) distant HLOD cluster with all members
    // visible and resident. Never merge player/dynamic bodies; the original
    // collision geometry is unchanged by HLOD selection.
    hlod_proxies_.clear();
    for (auto const& cluster:hlod_clusters_) {
        float const threshold=cluster.level==2 ? 350.f : 90.f;
        if (distance_sq(cluster.center,camera_position)<threshold*threshold)
            continue;
        bool eligible=true;
        for (auto const member:cluster.members) {
            if (!visible_mask_[member]) {
                eligible=false;
                break;
            }
        }
        if (!eligible)
            continue;
        hlod_proxies_.push_back(cluster);
        for (auto const member:cluster.members)
            visible_mask_[member]=0;
    }
    for (std::size_t i=0;i<scene_.objects.size();++i)
        if (visible_mask_[i])
            visible_indices_.push_back(i);
    ++generation_;
    diagnostics_.resident_entities=resident_indices_.size();
    diagnostics_.visible_entities=visible_indices_.size();
    diagnostics_.generation=generation_;
    for(std::size_t i=0;i<cell_telemetry_.size();++i) {
        auto& stat=cell_telemetry_[i];
        stat.assigned_entities=stat.resident_entities=stat.visible_entities=0;
        stat.active_hlod_proxies=stat.hlod_replaced_entities=0;
        stat.loaded=loaded_cells_[i]!=0;
        stat.payload_ready=!payload_gate_ || payload_ready_[i]!=0;
    }
    for(std::size_t i=0;i<effective_cell_indices_.size();++i) {
        auto const owner=effective_cell_indices_[i];
        if(owner>=cell_telemetry_.size())continue;
        auto& stat=cell_telemetry_[owner];
        ++stat.assigned_entities;
        if(resident_mask_[i])++stat.resident_entities;
        if(visible_mask_[i])++stat.visible_entities;
    }
    for(auto const& proxy:hlod_proxies_) {
        if(proxy.members.empty())continue;
        auto const owner=effective_cell_indices_[proxy.members.front()];
        if(owner>=cell_telemetry_.size())continue;
        ++cell_telemetry_[owner].active_hlod_proxies;
        cell_telemetry_[owner].hlod_replaced_entities+=proxy.member_count;
    }
    diagnostics_.last_update_cpu_us=static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-update_started).count());
    cost_profiler_.record_cpu(diagnostics_.last_update_cpu_us);
}

void SceneWorld::set_view_frustum(SceneViewFrustum const& frustum)
{
    for(auto const& plane:frustum.planes) {
        if(!finite(plane.normal) || !std::isfinite(plane.offset) ||
            double(plane.normal.x)*plane.normal.x+
            double(plane.normal.y)*plane.normal.y+
            double(plane.normal.z)*plane.normal.z < 1.e-12)
            throw std::invalid_argument("Scene camera frustum contains invalid plane");
    }
    view_frustum_=frustum;
}

void SceneWorld::set_layer_visible(std::string const& name, bool value)
{
    for (auto& layer:scene_.layers) {
        if (layer.name==name) {
            layer.visible=value;
            return;
        }
    }
    if (name=="Default") {
        scene_.layers.push_back({"Default",value});
        return;
    }
    throw std::invalid_argument("Unknown scene layer");
}
void SceneWorld::set_portal_open(std::string const& name, bool open)
{
    for (auto& portal:scene_.portals)
        if (portal.name==name) { portal.open=open; return; }
    throw std::invalid_argument("Unknown scene portal");
}
void SceneWorld::pin_cell(std::string const& name, bool pinned)
{
    if (cell_lookup_.count(name)==0)
        throw std::invalid_argument("Unknown scene streaming cell");
    const bool modified=pinned?pinned_.insert(name).second:pinned_.erase(name)!=0;
    if(modified)selection_valid_=false;
}
void SceneWorld::enable_payload_gate(bool enabled)
{
    payload_gate_=enabled;
    // A new source starts gated until the chunk provider confirms each cell.
    std::fill(payload_ready_.begin(),payload_ready_.end(),enabled ? 0u : 1u);
}

void SceneWorld::set_cell_payload_ready(std::string const& name,bool ready)
{
    auto const found=cell_lookup_.find(name);
    if(found==cell_lookup_.end())
        throw std::invalid_argument("Unknown NEF8 streaming cell");
    payload_ready_[found->second]=ready ? 1u : 0u;
}

bool SceneWorld::cell_payload_ready(std::string const& name) const
{
    auto const found=cell_lookup_.find(name);
    return found!=cell_lookup_.end() &&
        (!payload_gate_ || payload_ready_[found->second]!=0);
}

bool SceneWorld::cell_loaded(std::string const& name) const
{
    auto const it=cell_lookup_.find(name);
    return it!=cell_lookup_.end() && loaded_cells_[it->second]!=0;
}
bool SceneWorld::object_visible(std::size_t index) const
{
    if (index>=visible_mask_.size())
        throw std::out_of_range("Scene visible index out of range");
    return visible_mask_[index]!=0;
}
std::string SceneWorld::effective_cell_for_object(std::size_t index) const
{
    if (index>=effective_cell_indices_.size())
        throw std::out_of_range("Scene cell-owner index out of range");
    auto const owner=effective_cell_indices_[index];
    return owner==std::numeric_limits<std::size_t>::max() ?
        std::string{} : scene_.cells[owner].name;
}

bool SceneWorld::object_resident(std::size_t index) const
{
    if (index>=resident_mask_.size())
        throw std::out_of_range("Scene resident index out of range");
    return resident_mask_[index]!=0;
}

std::vector<std::string> SceneWorld::asset_dependencies() const
{
    // The asset manager treats these as virtual paths and can then schedule
    // asynchronous request_drawable / request_bytes operations per priority.
    std::vector<std::string> result;
    std::unordered_set<std::string> unique;
    for (auto index:visible_indices_) {
        auto const& asset=scene_.objects[index].node.asset;
        if (!asset.empty() && unique.insert(asset).second)
            result.push_back(asset);
    }
    for (auto index:resident_indices_) {
        auto const& asset=scene_.objects[index].node.asset;
        if (!asset.empty() && unique.insert(asset).second)
            result.push_back(asset);
    }
    return result;
}
std::vector<std::size_t> SceneWorld::query_sphere(Vector3 center, float radius) const
{
    return spatial_.query_sphere(center,radius);
}
std::vector<std::size_t> SceneWorld::query_box(Vector3 minimum, Vector3 maximum) const
{
    return spatial_.query_box(minimum,maximum);
}
bool SceneWorld::volume_contains(std::string const& name, Vector3 position) const
{
    if (!finite(position))
        throw std::invalid_argument("Invalid point for scene volume");
    for (auto const& volume:scene_.volumes) {
        if (volume.name!=name || !volume.enabled)
            continue;
        if (volume.shape==SceneVolume::Shape::sphere)
            return distance_sq(position,volume.minimum) <= volume.radius*volume.radius;
        return box_distance_sq(position,volume.minimum,volume.maximum)==0.f;
    }
    return false;
}
std::size_t SceneWorld::loaded_cell_count() const
{
    return diagnostics_.loaded_cells;
}

} // namespace northstarcpp
