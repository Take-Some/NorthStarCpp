#pragma once
// Per-Viewport component index for one frame. No renderer/provider types.
#include <northstarcpp/scene.hpp>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace northstarcpp::viewport_detail {

struct FirstPersonVisual {
    float forward=.55f, drop=.98f, arm_pitch=55.f, right=0.f;
    bool full_body=false;
    bool aim_down_sights=false;
    float recoil_back=0.f, recoil_arm=0.f;
};

template<class T> class FrameLookup {
    struct Slot { T value{}; std::uint64_t epoch=0; };
    std::unordered_map<std::uint64_t,Slot> entries_;
public:
    void set(std::uint64_t key,T value,std::uint64_t epoch) {
        auto [it,added]=entries_.try_emplace(key);
        it->second.value=std::move(value);
        it->second.epoch=epoch;
    }
    bool contains(std::uint64_t key,std::uint64_t epoch) const {
        auto it=entries_.find(key);
        return it!=entries_.end() && it->second.epoch==epoch;
    }
    T const* find(std::uint64_t key,std::uint64_t epoch) const {
        auto it=entries_.find(key);
        return it!=entries_.end() && it->second.epoch==epoch ?
            &it->second.value : nullptr;
    }
    void trim(std::size_t active_bound) {
        if(entries_.size()>active_bound)entries_.clear();
    }
    void reset() {entries_.clear();}
    std::size_t retained_entries() const {return entries_.size();}
};

class ViewportFrameBuilder {
public:
    void prepare(Scene const& scene,bool world_surfaces_ready,
                 bool first_person_camera);
    void reset_project();
    bool is_active(std::uint64_t id) const {return active_.contains(id,epoch_);}
    std::string const* material(std::uint64_t id) const {
        auto found=materials_.find(id,epoch_);
        return found?*found:nullptr;
    }
    SceneComponentBlob const* procedural(std::uint64_t id) const {
        auto found=procedural_.find(id,epoch_);
        return found?*found:nullptr;
    }
    std::uint32_t const* surface(std::uint64_t id) const {
        return surfaces_.find(id,epoch_);
    }
    FirstPersonVisual const* first_person(std::uint64_t id) const {
        return visual_modes_.find(id,epoch_);
    }
    bool has_materials() const {return has_materials_;}
    std::size_t retained_material_slots() const {
        return materials_.retained_entries();
    }
private:
    std::uint64_t epoch_=0;
    bool has_materials_=false;
    FrameLookup<std::uint8_t> active_;
    // Non-owning views/pointers: only used before this frame's Scene is released.
    FrameLookup<std::string const*> materials_;
    FrameLookup<SceneComponentBlob const*> procedural_;
    FrameLookup<std::uint32_t> surfaces_;
    FrameLookup<FirstPersonVisual> visual_modes_;
};
} // namespace northstarcpp::viewport_detail
