#!/usr/bin/env python3
"""Convert WolvenKit CR2W JSON particle systems to NorthStar NEF8/NEF16 YPT.

Faithfully maps emitter names, counts, lifetimes, initial sizes and velocities,
available color/alpha/size/acceleration curves, and effect-event relations.
Missing texture atlases, REDengine shaders and proprietary binary resources are
NOT embedded. Physical cartridge casing is owned by WeaponEffects in the engine.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import re
import struct
import zlib

from blake3 import blake3

ROOT=pathlib.Path(__file__).resolve().parent.parent
JSON_ROOT=ROOT/"out/fx_json"
OUT=ROOT/"engine/northstarcpp/resources/sharedAssets/particles/weapon/pistol_lexington.ypt"
REFERENCE=ROOT/"engine/northstarcpp/resources/sharedAssets/particles/core.ypt"
SCHEMA="northstar.particle_dictionary.runtime.v1"

def finite(value, default=0., lo=-1.e6, hi=1.e6):
    if isinstance(value,(int,float)) and math.isfinite(value):
        return max(lo,min(hi,float(value)))
    return default

def source(path):
    body=json.loads(path.read_text(encoding="utf-8"))
    root=body["Data"]["RootChunk"]
    if root.get("$type") not in ("CParticleSystem","worldEffect"):
        raise ValueError(f"Unexpected CR2W node in {path.name}")
    return root

def evaluator(handle):
    return handle.get("Data",handle) if isinstance(handle,dict) else {}

def sample(value, components=1):
    ev=evaluator(value)
    typ=ev.get("$type","")
    def vec(v):
        if isinstance(v,dict):
            return [finite(v.get(k,0)) for k in ("X","Y","Z","W")][:components]
        if isinstance(v,(float,int)):return [finite(v)]*components
        return [0.]*components
    if "RandomUniform" in typ:
        a=vec(ev.get("min",0));b=vec(ev.get("max",0))
        return [(x+y)*.5 for x,y in zip(a,b)]
    if "Const" in typ: return vec(ev.get("value",0))
    if "StartEnd" in typ: return vec(ev.get("start",0))
    curves=ev.get("curves",{}).get("Elements",[])
    if curves: return vec(curves[0].get("Value",0))
    return vec(ev.get("value",0))

def timeline(handle,components=1):
    ev=evaluator(handle)
    typ=ev.get("$type","")
    frames=[]
    def vector(v):
        if isinstance(v,dict):
            if "Red" in v:
                return [finite(v.get(k,0),lo=0,hi=255)/255.
                        for k in ("Red","Green","Blue","Alpha")][:components]
            return [finite(v.get(k,0)) for k in ("X","Y","Z","W")][:components]
        if isinstance(v,(float,int)):return [finite(v)]*components
        return [0.]*components
    if "StartEnd" in typ:
        frames=[(0.,vector(ev.get("start",0))),(1.,vector(ev.get("end",0)))]
    elif "RandomUniform" in typ:
        frames=[(0.,sample(handle,components))]
    elif "Const" in typ:
        frames=[(0.,sample(handle,components))]
    else:
        for c in ev.get("curves",{}).get("Elements",[])[:64]:
            frames.append((finite(c.get("Point"),lo=0,hi=1),vector(c.get("Value",0))))
    if not frames:frames=[(0.,sample(handle,components))]
    return [{"time":[t,t,t,t],"value":v}
            for t,v in sorted(frames,key=lambda p:p[0])]

def property_node(name,values):
    return {"name":name,"keyframes":[{"time":[0.,0.,0.,0.],
                                      "value":[*values,0.,0.,0.][:4]}]}

def module_by_type(modules,needle):
    for record in modules:
        value=record.get("Data",{})
        if needle in value.get("$type","") and value.get("isEnabled",True):
            return value
    return None

def make_emitter(namespace,entry):
    e=entry["Data"]
    name=namespace+"_"+e["editorName"]
    mods=e.get("modules",[])
    life=module_by_type(mods,"InitializerLifeTime")
    size=module_by_type(mods,"InitializerSize")
    vel=module_by_type(mods,"InitializerVelocity")
    l=finite(sample(life.get("lifeTime",{}))[0] if life else .25,.25,.04,8.)
    initial_size=finite(sample(size.get("size",{}))[0] if size else .04,.04,.005,1.)
    raw_velocity=sample(vel.get("velocity",{}),3) if vel else [0.,1.,0.]
    magnitude=math.hypot(*raw_velocity)
    # REDengine +Y is the gun barrel axis. NVPT preview uses RAGE Z-up source.
    direction=[x/max(1.e-6,magnitude) for x in raw_velocity]
    if magnitude<1.e-6:direction=[0.,1.,0.]
    max_count=int(finite(e.get("maxParticles",1),1,1,200))
    # Native preview understands the same no-loop one-shot field.
    emit={
        "name":name,
        "one_shot":True,
        "max_particles":max_count,
        "creation_domain":{"position":property_node("position",[0.,0.,0.,0.])},
        "target_domain":{"position":property_node("position",[*direction,0.])},
        "keyframe_properties":[
            property_node("spawnRateOverTime",[float(max_count)*60.,float(max_count)*60.]),
            property_node("particleLife",[l,l]),
            property_node("speedScalar",[finite(magnitude,0,0,20.)]),
            property_node("sizeScalar",[1.])
        ]
    }
    behaviours=[{"type":"age"}]
    acc=module_by_type(mods,"ModificatorAcceleration")
    if acc:
        xyz=acc.get("acceleration",acc.get("xyz",{}))
        acceleration=sample(xyz,3)
        behaviours.append({"type":"acceleration",
            "enable_gravity":False,
            "xyz_max":{"keyframes":timeline(xyz,3)}})
    c=module_by_type(mods,"ModificatorColorOverLife")
    if c:
        rgba=timeline(c.get("color",{}),4)
        # RGB(A) curves are all carried forward for future shader evaluation.
        behaviours.append({"type":"colour","rgba_max":{"keyframes":rgba}})
    a=module_by_type(mods,"ModificatorAlphaOverLife")
    if a:
        behaviours.append({"type":"alpha","alpha":{"keyframes":
            timeline(a.get("alpha",{}))}})
    sz=module_by_type(mods,"ModificatorSizeOverLife")
    if sz:
        behaviours.append({"type":"size_over_life",
            "scale":{"keyframes":timeline(sz.get("size",{}),3)}})
    # The native CPU preview reads 'size' from whd_max; keep the evaluator
    # curves under size_over_life while preserving the actual initial size.
    behaviours.append({"type":"size",
        "whd_max":{"keyframes":[{"time":[0.,0.,0.,0.],
                   "value":[initial_size,initial_size,initial_size,0.]}]}})
    unsupported=[m.get("Data",{}).get("$type","") for m in mods
        if not any(x in m.get("Data",{}).get("$type","") for x in (
            "InitializerLifeTime","InitializerSize","InitializerVelocity",
            "ModificatorAcceleration","ModificatorColorOverLife",
            "ModificatorAlphaOverLife","ModificatorSizeOverLife"))]
    return emit,{"name":name,"behaviours":behaviours},{
        "emitter":name, "source":e["editorName"],"maxParticles":max_count,
        "lifetime_seconds":l,"initial_size":initial_size,
        "initial_speed":magnitude,"unmapped_modules":unsupported}

def make():
    muzzle=source(JSON_ROOT/"w_pistol_lexington_muzzle.particle.json")
    eject=source(JSON_ROOT/"w_pistol_lexington_eject.particle.json")
    shell=source(JSON_ROOT/"cal_9mm.particle.json")
    world=source(JSON_ROOT/"w_pistol_lexington_muzzle.effect.json")
    if len(world["events"])!=3:
        raise ValueError("Expected Lexington muzzle, eject and shell events")
    emitters=[];particles=[];audit=[]
    groups={}
    for namespace,doc in (("muzzle",muzzle),("eject",eject),("shell",shell)):
        names=[]
        for item in doc["emitters"]:
            emitter,particle,entry=make_emitter(namespace,item)
            emitters.append(emitter);particles.append(particle);audit.append(entry)
            names.append(emitter["name"])
        groups[namespace]=names
    def effect(name,names,duration):
        return {"name":name,"duration_min":duration,"duration_max":duration,
            "num_loops":1,"short_lived":True,
            "events":[{"index":i,"emitter_rule":n,"particle_rule":n,
                "start_ratio":0.,"end_ratio":1.,
                "colour_tint_max":[1.,1.,1.,1.]} for i,n in enumerate(names)]}
    # Do not include shell mesh as an ephemeral particle in the active shot:
    # real casing is instantiated from sharedAssets prefab and lives in Jolt.
    v={
        "schema":SCHEMA,
        "coordinate_space":"rage_z_up_source",
        "effects":[
            effect("pistol_shot",groups["muzzle"]+groups["eject"],.65),
            effect("muzzle_flash",groups["muzzle"],.65),
            effect("ejection_smoke",groups["eject"],.65),
            effect("shell_reference_editor_only",groups["shell"],.85)
        ],
        "emitters":emitters,
        "particles":particles,
    }
    return v,audit

def ypt_bytes(runtime,logical):
    if not REFERENCE.is_file():raise FileNotFoundError(REFERENCE)
    reference=REFERENCE.read_bytes()
    if reference[:6]!=b"NEF8\x02\x08" or reference[80:85]!=b"NEF16":
        raise ValueError("Incompatible reference NEF16 particle container")
    # The 192-byte NEF16 framing includes no source-specific strings. Its
    # authenticated body hash is constructed afresh below.
    nef16=reference[64:256]
    metadata={
        "schema":"newengine.asset.list_file.header_metadata",
        "content_kind":"particle_dictionary",
        "logical_path":logical,
        "source":"local WolvenKit CR2W JSON (redengine effect/particle)",
        "authored_by":"northstar.tools.convert_red_particle_to_ypt",
        "build_profile":"import.nef16.v1",
        "dependencies":[],
        "entries":[{
            "asset_kind":"particle_dictionary",
            "name":pathlib.PurePosixPath(logical).stem,
            "entry_ref":logical+"@"+pathlib.PurePosixPath(logical).stem,
            "dependencies":[],
            "metadata":{"runtime_body":"particle_dictionary",
                        "runtime_profile":"NEF16",
                        "source_format":"WolvenKit/CR2W JSON"},
        }],
        "warnings":["REDengine GPU shader/texture atlases not translated",
                    "Collision shell effect represented as an editor-only reference"],
    }
    json_bytes=json.dumps(runtime,ensure_ascii=False,sort_keys=True,
        separators=(",",":"),allow_nan=False).encode("utf8")
    body=b"NVPT"+struct.pack("<IQI",1,len(json_bytes),0)+json_bytes
    checksum=blake3(body).digest()
    compressor=zlib.compressobj(level=9,wbits=-15)
    packed=compressor.compress(body)+compressor.flush()
    meta=json.dumps(metadata,ensure_ascii=False,sort_keys=True,
        separators=(",",":"),allow_nan=False).encode("utf8")
    header=bytearray(64)
    header[0:4]=b"NEF8"
    header[4:6]=bytes([2,8])
    struct.pack_into("<HHHIQQ",header,6,36,7,1,1,len(packed),len(body))
    header[32:64]=checksum
    return bytes(header)+nef16+meta+packed

def decode_and_validate(data):
    assert data[:4]==b"NEF8" and data[5]==8
    assert struct.unpack_from("<H",data,6)[0]==36
    assert data[80:85]==b"NEF16"
    compressed=struct.unpack_from("<Q",data,16)[0]
    expanded=struct.unpack_from("<Q",data,24)[0]
    body=zlib.decompress(data[-compressed:],wbits=-15)
    assert len(body)==expanded
    assert blake3(body).digest()==data[32:64]
    assert body[:4]==b"NVPT"
    assert struct.unpack_from("<I",body,4)[0]==1
    assert struct.unpack_from("<Q",body,8)[0]==len(body)-20
    data=json.loads(body[20:])
    assert data["schema"]==SCHEMA and data["emitters"] and data["effects"]
    names=[e["name"] for e in data["emitters"]]
    assert len(names)==len(set(names))
    for ef in data["effects"]:
        assert all(e["emitter_rule"] in names for e in ef["events"])
    return data

def converted_variants(runtime):
    """Standalone dictionaries for editing each system without opening shot."""
    definitions=(
        ("pistol_muzzle.ypt","muzzle_","muzzle_flash"),
        ("pistol_ejection.ypt","eject_","ejection_smoke"),
        ("pistol_shell_reference.ypt","shell_","shell_reference_editor_only"),
    )
    for name,prefix,effect_name in definitions:
        effects=[e for e in runtime["effects"] if e["name"]==effect_name]
        emitters=[e for e in runtime["emitters"] if e["name"].startswith(prefix)]
        particles=[p for p in runtime["particles"] if p["name"].startswith(prefix)]
        if not effects or not emitters or len(emitters)!=len(particles):
            raise ValueError("Missing converted effect group "+prefix)
        yield name,{"schema":runtime["schema"],
                    "coordinate_space":runtime["coordinate_space"],
                    "effects":effects,"emitters":emitters,"particles":particles}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--out",type=pathlib.Path,default=OUT)
    args=parser.parse_args()
    output=args.out
    if not output.is_absolute():output=ROOT/output
    if not output.is_relative_to(ROOT):
        raise ValueError("Output must stay inside the NorthStar workspace")
    output.parent.mkdir(parents=True,exist_ok=True)
    runtime,audit=make()
    logical="particles/weapon/"+output.name
    binary=ypt_bytes(runtime,logical)
    verified=decode_and_validate(binary)
    output.write_bytes(binary)
    variants=[]
    for filename,subtree in converted_variants(runtime):
        target=output.parent/filename
        converted=ypt_bytes(subtree,"particles/weapon/"+filename)
        checked=decode_and_validate(converted)
        target.write_bytes(converted)
        variants.append({"path":str(target.relative_to(ROOT)).replace("\\\\","/"),
                         "effects":len(checked["effects"]),
                         "emitters":len(checked["emitters"]),
                         "bytes":len(converted)})
    # Audit carries only module names and scalar summaries. It does NOT
    # duplicate raw extracted CDPR/REDengine assets in Shared Content.
    report=ROOT/"out/fx_converted/pistol_lexington_conversion_audit.json"
    report.parent.mkdir(parents=True,exist_ok=True)
    report.write_text(json.dumps({
        "output":str(output.relative_to(ROOT)).replace("\\","/"),
        "effects":[e["name"] for e in verified["effects"]],
        "emitters":audit,"converted_bytes":len(binary),
        "variants":variants,
        "asset_tier":"metadata/CPU-preview",
        "gpu_texture_transcoding":False,
        "persistent_casing":"owned by engine WeaponEffects (not YPT)",
    },indent=2),encoding="utf8")
    print(f"PASS NEF8/NEF16 YPT: {output.relative_to(ROOT)} "
          f"effects={len(verified['effects'])}, "
          f"emitters={len(verified['emitters'])}, "
          f"particles={len(verified['particles'])}, bytes={len(binary)}")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
