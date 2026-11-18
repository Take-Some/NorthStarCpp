#include "../engine/northstarcpp/modules/viewport/viewport_frame_scratch.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

#define CHECK(v) do {if(!(v)) throw std::runtime_error(#v);}while(false)

int main() {
    using northstarcpp::viewport_detail::ViewportFrameScratch;
    ViewportFrameScratch scratch;
    scratch.begin_frame();
    auto& first=scratch.material_vertices("material/first");
    first.resize(128);
    auto const* original_vertices=first.data();
    CHECK(original_vertices!=nullptr);
    auto& second=scratch.material_vertices("material/second");
    second.resize(32);
    CHECK(scratch.active_material_draws==2);
    CHECK(scratch.material_vertices("material/first").size()==128);
    CHECK(scratch.material_indices.size()==2);
    auto const* original_lookup=&*scratch.material_indices.find("material/first");

    scratch.pick_triangles.resize(36);
    scratch.pick_cube_entities.resize(16);
    scratch.gpu_materials.resize(2);
    scratch.gpu_materials_v2.resize(2);
    auto const* pick_data=scratch.pick_triangles.data();
    auto const* gpu_data=scratch.gpu_materials.data();
    scratch.material_keys.push_back(scratch.material_draws[0].first.c_str());

    scratch.begin_frame();
    CHECK(scratch.active_material_draws==0);
    CHECK(scratch.material_indices.size()==2);
    CHECK(&*scratch.material_indices.find("material/first")==original_lookup);
    CHECK(scratch.material_keys.empty());
    CHECK(scratch.pick_triangles.empty());
    CHECK(scratch.gpu_materials.empty());

    // Material names may change across project/scene transitions; old
    // geometry must never appear, but retained vertex capacity is reused.
    auto& replaced=scratch.material_vertices("material/replaced");
    CHECK(replaced.empty());
    replaced.resize(128);
    CHECK(replaced.data()==original_vertices);
    CHECK(scratch.active_material_draws==1);
    CHECK(scratch.material_draws[0].first=="material/replaced");
    scratch.material_keys.push_back(scratch.material_draws[0].first.c_str());
    CHECK(std::strcmp(scratch.material_keys[0],"material/replaced")==0);

    scratch.pick_triangles.resize(36);
    scratch.gpu_materials.resize(2);
    CHECK(scratch.pick_triangles.data()==pick_data);
    CHECK(scratch.gpu_materials.data()==gpu_data);

    // Empty frame does not submit stale material batches from earlier draws.
    scratch.begin_frame();
    CHECK(scratch.active_material_draws==0);
    CHECK(scratch.material_keys.empty());
    CHECK(scratch.gpu_materials_v2.empty());
    auto& restored=scratch.material_vertices("material/first");
    CHECK(restored.empty());
    restored.resize(128);
    CHECK(restored.data()==original_vertices);
    CHECK(&*scratch.material_indices.find("material/first")==original_lookup);
    CHECK(scratch.active_material_draws==1);

    // A new project must not retain another project's reference cache.
    scratch.reset_project();
    CHECK(scratch.material_indices.empty());
    CHECK(scratch.material_draws.empty());
    CHECK(scratch.active_material_draws==0);
    CHECK(scratch.material_keys.empty());
    CHECK(scratch.material_vertices("new/project").empty());
    CHECK(scratch.active_material_draws==1);
}
