#!/usr/bin/env python3
"""Synchronize public engine.sky.configure() with project sky source/mode."""
from pathlib import Path
import datetime,sys
ROOT=Path(__file__).resolve().parents[2]
changes={}
def change(relative,old,new):
    path=ROOT/relative
    text=changes.get(path,path.read_text(encoding="utf8").replace("\r\n","\n"))
    count=text.count(old)
    if count!=1:raise RuntimeError(f"{relative}: expected 1 anchor, got {count}: {old[:45]}")
    changes[path]=text.replace(old,new,1)

base="engine/northstarcpp/modules/editor/"
for name in ("window_script_bindings_host.cpp","window_runtime_script_bindings.inc"):
    rel=base+name
    change(rel,
        'static constexpr std::array<char const*,17> allowed{',
        'static constexpr std::array<char const*,19> allowed{')
    change(rel,
        '"haze","sunIntensity","moonIntensity","preset"};',
        '"haze","sunIntensity","moonIntensity","preset","mode","drawable"};')
    change(rel,
        '            flag("enabled",next.enabled);',
        '''            auto requested_mode=tree.get_optional<std::string>("mode");
            auto requested_drawable=tree.get_optional<std::string>("drawable");
            auto mode=requested_mode?*requested_mode:sky_mode_;
            auto drawable=requested_drawable?*requested_drawable:
                sky_drawable_reference_;
            if(mode!="procedural" && mode!="drawable" &&
               mode!="hybrid" && mode!="disabled")
                throw std::invalid_argument("Unknown engine.sky mode");
            if(mode=="disabled" || mode=="procedural")drawable.clear();
            if(mode=="drawable" && drawable.empty())
                throw std::invalid_argument(
                    "engine.sky drawable mode requires a YDD/YDR reference");
            if(!drawable.empty())
                project_sky::validate_drawable_reference(drawable);
            flag("enabled",next.enabled);''')
    change(rel,
        '''            sky_environment_.configure(next);
            sky_visual_dirty_=true;''',
        '''            if(requested_mode) {
                // Modes select presentation, independent of atmospheric
                // numeric settings. Procedural is the asset-free default.
                next.enabled=mode=="procedural" || mode=="hybrid";
            }
            SkyEnvironment::validate(next);
            if(graphics && drawable!=sky_drawable_reference_) {
                try {
                    graphics->set_sky_asset_reference(drawable);
                } catch(std::exception const& error) {
                    // Bad optional art never stops the simulation or editor.
                    std::fprintf(stderr,
                        "[WARN] [engine.sky] Drawable rejected: %s\\n",
                        error.what());
                    graphics->set_sky_asset_reference({});
                    drawable.clear();
                    mode="procedural";
                    next.enabled=true;
                }
            }
            sky_mode_=std::move(mode);
            sky_drawable_reference_=std::move(drawable);
            sky_environment_.configure(next);
            sky_visual_dirty_=true;''')
    change(rel,
        '''            sky_environment_.reset();
            sky_visual_dirty_=true;''',
        '''            sky_environment_.reset();
            sky_mode_="disabled";
            sky_drawable_reference_.clear();
            if(graphics)graphics->set_sky_asset_reference({});
            sky_visual_dirty_=true;''')
    change(rel,
        '''                ",\\"preset\\":\\""+cfg.preset+"\\""+''',
        '''                ",\\"preset\\":\\""+cfg.preset+"\\""+
                ",\\"mode\\":"+script_json_quote(sky_mode_)+
                ",\\"drawable\\":"+script_json_quote(sky_drawable_reference_)+''')

jsbase="engine/northstarcpp/resources/sharedAssets/api/"
for rel in (jsbase+"sky/sky.js",jsbase+"northstar/bootstrap.js"):
    change(rel,
        '''                const range=ranges[name];
                if(!range)throw new RangeError("Unknown Sky property: "+name);''',
        '''                if(name==="mode") {
                    if(!["procedural","drawable","hybrid","disabled"].includes(value))
                        throw new RangeError("Invalid Sky mode");
                    continue;
                }
                if(name==="drawable" || name==="preset") {
                    if(typeof value!=="string" || value.length>512)
                        throw new TypeError("Sky."+name+" must be a string");
                    continue;
                }
                const range=ranges[name];
                if(!range)throw new RangeError("Unknown Sky property: "+name);''')
rel=jsbase+"types/northstar-engine.d.ts"
change(rel,
       '''    export interface SkySettings {
        enabled?:boolean;paused?:boolean;''',
       '''    export interface SkySettings {
        /** Project-authored source selection, shared by Editor and Player. */
        mode?:"disabled"|"procedural"|"drawable"|"hybrid";
        drawable?:string; // VFS asset reference, e.g. worlds/sky.ydd@layer
        preset?:string;
        enabled?:boolean;paused?:boolean;''')
manifest=ROOT/"Projects/AbbyPistolDemo/northstarcpp.project.json"
def main():
    apply="--apply" in sys.argv
    print("Validated",len(changes),"engine.sky API source files")
    for p in changes:print("PATCH",p.relative_to(ROOT))
    if not apply:return
    stamp=datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    for p,new in changes.items():
        b=p.read_bytes()
        p.with_name(p.name+".before-sky-api-"+stamp+".bak").write_bytes(b)
        payload=new.replace("\n","\r\n") if b"\r\n" in b else new
        p.write_bytes(payload.encode("utf8"))
    print("Applied sky mode/drawable to native bindings, JS facade and types")
if __name__=="__main__":main()
