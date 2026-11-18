#include "viewport_frame_builder.hpp"
#include <northstarcpp/geometry_scene.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace northstarcpp::viewport_detail {

void ViewportFrameBuilder::reset_project() {
    epoch_=0;
    has_materials_=false;
    active_.reset();
    materials_.reset();
    procedural_.reset();
    surfaces_.reset();
    visual_modes_.reset();
}

void ViewportFrameBuilder::prepare(Scene const& scene,bool world_surfaces_ready,
                                   bool first_person_camera) {
    // A changing, unbounded stream of IDs should not retain every historical
    // allocation forever. This bound is a cache retention policy, NOT an
    // entity/mesh/LOD limit: every object is still indexed this frame.
    auto const count=scene.objects.size()+scene.components.size();
    auto const budget=std::max<std::size_t>(8192,
        count>std::numeric_limits<std::size_t>::max()/4 ?
            std::numeric_limits<std::size_t>::max() : count*4);
    if(epoch_==std::numeric_limits<std::uint64_t>::max())reset_project();
    active_.trim(budget);
    materials_.trim(budget);
    procedural_.trim(budget);
    surfaces_.trim(budget);
    visual_modes_.trim(budget);
    ++epoch_;
    has_materials_=false;

    for(auto const& object:scene.objects)
        if(object.node.id)active_.set(object.node.id,1,epoch_);

    for(auto const& component:scene.components) {
        if(component.type=="northstarcpp.editor.material" &&
           component.version==1 && component.entity_id &&
           !component.payload.empty()) {
            if(component.payload.size()>1024)
                throw std::runtime_error("Oversized scene material reference");
            // Original map semantics: the last entry for a stable ID wins.
            materials_.set(component.entity_id,
                &component.payload,epoch_);
            has_materials_=true;
        }
        if(component.type==geometry_primitive_component &&
           component.entity_id && !procedural_.contains(component.entity_id,epoch_)) {
            // Original emplace semantics: first geometry component wins.
            procedural_.set(component.entity_id,&component,epoch_);
        }
        if(world_surfaces_ready &&
           component.type=="engine.world.surface" &&
           component.version==1 && component.entity_id &&
           component.payload.size()<=48) {
            std::string_view const name(component.payload);
            auto const value=name=="ground_sand"?1u:
                name=="rock_moss"?2u:
                name=="terrain_forest_floor"?3u:0u;
            if(value)surfaces_.set(component.entity_id,value,epoch_);
        }
        if(first_person_camera &&
           component.type=="engine.model.visual_mode" &&
           component.version==1 && component.entity_id &&
           component.payload.size()<=128) {
            std::istringstream input(component.payload);
            std::string mode,extra;
            FirstPersonVisual view;
            if(!(input>>mode>>view.forward>>view.drop>>view.arm_pitch) ||
               (mode!="first_person_arms" && mode!="first_person_body"))
                continue;
            // Legacy visual modes omit lateral and ADS fields.
            if(input>>view.right) {
                if(input>>extra) {
                    if(extra!="ads" && extra!="ready")continue;
                    view.aim_down_sights=extra=="ads";
                    // New optional temporal viewmodel recoil floats.
                    // Legacy payloads end after "ads"/"ready".
                    if(input>>view.recoil_back) {
                        if(!(input>>view.recoil_arm))continue;
                        if(input>>extra)continue;
                    } else if(!input.eof())continue;
                } else if(!input.eof())continue;
            } else if(!input.eof())continue;
            if(!std::isfinite(view.forward) || view.forward<0.f ||
               view.forward>3.f || !std::isfinite(view.drop) ||
               view.drop<0.f || view.drop>3.f ||
               !std::isfinite(view.arm_pitch) ||
               std::abs(view.arm_pitch)>180.f ||
               !std::isfinite(view.right) || std::abs(view.right)>1.f ||
               !std::isfinite(view.recoil_back) || view.recoil_back<0.f ||
               view.recoil_back>.06f ||
               !std::isfinite(view.recoil_arm) || view.recoil_arm<0.f ||
               view.recoil_arm>1.f)
                continue;
            view.full_body=(mode=="first_person_body");
            visual_modes_.set(component.entity_id,view,epoch_);
        }
    }
}
} // namespace northstarcpp::viewport_detail
