#!/usr/bin/env python3
"""Apply provider-neutral engine.sky project defaults to Player and Qt Editor.
Usage: python tools/dev/integrate_project_sky_contract.py [--apply]
Dry-run validates exact anchors; --apply creates sidecar backups before writes.
"""
from __future__ import annotations
from pathlib import Path
import argparse, datetime, json

ROOT=Path(__file__).resolve().parents[2]
changes={}
def patch(rel,old,new,label):
    p=ROOT/rel
    original=changes.get(p)
    if original is None:
        original=p.read_text(encoding="utf8").replace("\r\n","\n")
    count=original.count(old)
    if count!=1:raise RuntimeError(f"{label}: expected 1 anchor, found {count} in {rel}")
    changes[p]=original.replace(old,new,1)

viewport="engine/northstarcpp/modules/viewport/"
editor="engine/northstarcpp/modules/editor/"
patch(viewport+"viewport.cpp",
      '#include "viewport_sky_layers.hpp"',
      '#include "viewport_sky_layers.hpp"\n#include <northstarcpp/sky_project_config.hpp>',
      "include project sky")
patch(viewport+"viewport.cpp",
      '    state->project_assets=std::move(assets);',
      '''    state->project_assets=std::move(assets);
    // Project switches invalidate the previous world's sky and GPU atlas.
    state->retained_sky.reset();
    state->sky_gpu_uploaded=false;
    if(!state->api->set_sky(state->renderer,nullptr))
        throw state->module->error("Cannot invalidate previous project sky");''',
      "VFS remount sky invalidation")
patch(viewport+"viewport.cpp",
      'bool Viewport::supports_gpu_sky() const noexcept {',
      '''void Viewport::set_sky_asset_reference(std::string const& drawable) {
    if(drawable.empty()) {
        state->retained_sky.reset();
        state->sky_gpu_uploaded=false;
        if(!state->api->set_sky(state->renderer,nullptr))
            throw state->module->error("Cannot clear authored project sky");
        return;
    }
    project_sky::validate_drawable_reference(drawable);
    if(!state->project_assets)
        throw std::runtime_error("Project sky needs a mounted VFS");
    // The asset system resolves the caller-supplied drawable ID, YMAT and YTD.
    // No file names or dictionary entries are selected by the renderer.
    auto decoded=state->project_assets->load_drawable_resources(drawable);
    set_sky(decoded);
}

void Viewport::configure_project_sky() {
    if(!state->project_assets)return;
    auto const profile=project_sky::load(*state->project_assets);
    set_sky_asset_reference(profile.drawable);
    set_sky_environment(SkyEnvironment(profile.settings));
}

bool Viewport::supports_gpu_sky() const noexcept {''',
      "renderer-neutral source selection")
patch(editor+"window_app_internal.hpp",
      '#include <northstarcpp/assets.hpp>',
      '#include <northstarcpp/assets.hpp>\n#include <northstarcpp/sky_project_config.hpp>',
      "bootstrap sky shared contract")
patch(editor+"window_app_state.hpp",
      '    SkyEnvironment sky_environment_;',
      '''    SkyEnvironment sky_environment_;
    // Authored project default, overrideable by engine.sky.configure().
    std::string sky_mode_="procedural";
    std::string sky_drawable_reference_;''',
      "project sky state")
patch(editor+"window_runtime_bootstrap.inc",
      '        bootstrap_stage("script.discovery", [&] {',
      '''        if(!file_path.empty() && !config.new_document)
            bootstrap_stage("sky.project", [&] {
                try {
                    AssetStore sky_assets;
                    if(config.archive_path.empty()) {
                        auto const root=script_project_root(file_path);
                        sky_assets.mount_project_assets(root,
                            discover_shared_assets(config,root));
                    } else {
                        sky_assets.mount_archive("",config.archive_path,
                            config.archive_data.empty() ?
                                config.archive_path : config.archive_data);
                    }
                    auto const profile=project_sky::load(sky_assets);
                    sky_mode_=profile.mode;
                    sky_drawable_reference_=profile.drawable;
                    sky_environment_.configure(profile.settings);
                    sky_visual_dirty_=true;
                } catch(std::exception const& error) {
                    bootstrap_warning("sky.project",
                        std::string("Sky manifest ignored: ")+error.what());
                    SkySettings defaults;
                    defaults.enabled=true;
                    sky_environment_.configure(defaults);
                    sky_mode_="procedural";
                    sky_drawable_reference_.clear();
                    sky_visual_dirty_=true;
                }
            });
        bootstrap_stage("script.discovery", [&] {''',
      "project sky defaults before scripts")
patch(editor+"window_app_internal.hpp",
      '''            if (fps_assets) bootstrap_stage("renderer.skydome", [&] {
                bool skydome_available = false;
                try {
                    (void)fps_assets->size("models/skydome.ydd");
                    skydome_available = true;
                } catch (std::runtime_error const&) {
                    // Older FPS projects contain no authored sky.
                }
                if (skydome_available) {
                    try {
                        auto sky = fps_assets->request_drawable_resources(
                            "models/skydome.ydd@skydome_high", AssetPriority::critical).get();
                        graphics->set_sky(*sky);
                    } catch(std::exception const& e) {
                        bootstrap_warning("renderer.skydome",
                            std::string("Skipping invalid skydome asset: ")+e.what());
                    }
                }
            });''',
      '''            bootstrap_stage("renderer.sky", [&] {
                try {
                    graphics->set_sky_asset_reference(sky_drawable_reference_);
                } catch(std::exception const& error) {
                    bootstrap_warning("renderer.sky",
                        std::string("Authored sky resource unavailable: ")+
                        error.what());
                    // Preserve a working procedural sky on invalid optional art.
                    graphics->set_sky_asset_reference({});
                }
                // Runtime scripts have already run and may have modified the
                // project defaults. The same engine.sky applies in every backend.
                graphics->set_sky_environment(sky_environment_);
                sky_visual_dirty_=false;
            });''',
      "remove hardcoded skydome loader")
patch(editor+"qt_editor_viewport.cpp",
      '        try {gpu_->renderer->set_asset_sources(asset_root_);}',
      '''        try {
            gpu_->renderer->set_asset_sources(asset_root_);
            gpu_->renderer->configure_project_sky();
        }''',
      "Qt remount sky")
patch(editor+"qt_editor_viewport.cpp",
      '''        if(!asset_root_.empty())
            renderer->renderer->set_asset_sources(asset_root_);''',
      '''        if(!asset_root_.empty()) {
            renderer->renderer->set_asset_sources(asset_root_);
            renderer->renderer->configure_project_sky();
        } else {
            // Blank/new projects also render the default procedural atmosphere.
            SkySettings fallback;
            fallback.enabled=true;
            renderer->renderer->set_sky_environment(SkyEnvironment(fallback));
        }''',
      "Qt viewport initial sky")
patch(editor+"qt_editor_viewport.cpp",
      '#include "../viewport/editor_grid.hpp"',
      '#include "../viewport/editor_grid.hpp"\n#include <northstarcpp/sky_environment.hpp>',
      "Qt default atmosphere type")
patch("engine/northstarcpp/modules/render/vulkan/renderer_materials.cpp",
      '''        if(sky && gpu_sky_uniforms_.flags[0]>.5f) {
            constexpr std::array<std::array<float,2>,6> corners{{''',
      '''        if(sky) {
            bool const gpu_atmosphere=gpu_sky_uniforms_.flags[0]>.5f;
            constexpr std::array<std::array<float,2>,6> corners{{''',
      "Vulkan static and atmospheric sky always full screen")
patch("engine/northstarcpp/modules/render/vulkan/renderer_materials.cpp",
      '''                out.uv[0]=xy[0]*.5f+.5f;
                out.uv[1]=xy[1]*.5f+.5f;
                // Sky-only interpolation channel: camera world origin.
                // The fragment shader uses it to keep 3D cloud patterns
                // stationary when the player traverses the scene.
                out.normal[0]=frame.camera_x;
                out.normal[1]=frame.camera_y;
                out.normal[2]=frame.camera_z;''',
      '''                if(gpu_atmosphere) {
                    out.uv[0]=xy[0]*.5f+.5f;
                    out.uv[1]=xy[1]*.5f+.5f;
                } else {
                    // An authored YDD sky can be only a few metres across.
                    // Sample its panorama against a camera-relative view ray:
                    // never clip a physical dome against the near plane.
                    constexpr float pi=3.14159265358979323846f;
                    float const length=std::sqrt(
                        out.world[0]*out.world[0]+out.world[1]*out.world[1]+
                        out.world[2]*out.world[2]);
                    float const longitude=std::atan2(out.world[0],out.world[2]);
                    float const latitude=std::asin(std::clamp(
                        out.world[1]/std::max(length,1.e-6f),-1.f,1.f));
                    out.uv[0]=.5f+longitude/(2.f*pi);
                    out.uv[1]=.5f-latitude/pi;
                }
                // Sky-only interpolation channel: camera world origin.
                // The fragment shader uses it to keep 3D cloud patterns
                // stationary when the player traverses the scene.
                out.normal[0]=frame.camera_x;
                out.normal[1]=frame.camera_y;
                out.normal[2]=frame.camera_z;''',
      "Vulkan spherical authored sky sampling")

# A project decides whether it uses an authored skydome: this is data, not C++.
manifest=ROOT/"Projects/AbbyPistolDemo/northstarcpp.project.json"
project=json.loads(manifest.read_text(encoding="utf8"))
project["sky"]={"mode":"hybrid",
                "drawable":"models/skydome.ydd@skydome_high",
                "sun":True,"clouds":True,"timeOfDay":12.0}
changes[manifest]=json.dumps(project,ensure_ascii=False,indent=2)+"\n"

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--apply",action="store_true")
    args=parser.parse_args()
    print(f"Validated {len(changes)} source files; hardcoded sky loader removed.")
    for p in changes:print("PATCH",p.relative_to(ROOT).as_posix())
    if not args.apply:return
    stamp=datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for path,text in changes.items():
        original=path.read_bytes()
        backup=path.with_name(path.name+".before-sky-contract-"+stamp+".bak")
        backup.write_bytes(original)
        normalized=text.replace("\n","\r\n") if b"\r\n" in original else text
        path.write_bytes(normalized.encode("utf8"))
    print("Applied. Backups retained; no commit/push performed.")
if __name__=="__main__":
    main()
