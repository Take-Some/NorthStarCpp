#!/usr/bin/env python3
"""Audit ownership and *actual compiled* original NorthStarCpp foundation units.

This does not claim that historical PS3/Xbox/editor tools compile into
Windows runtime. Each migrated translation unit must be explicitly listed.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
from collections import Counter

ROOT=Path(__file__).resolve().parents[1]
MODULES=ROOT/"engine/northstarcpp/modules"
BASE=MODULES/"core/northstarcpp"
RUNTIME_SYSTEMS={"core","animation","collision","physics","render",
    "sound","input","network","engine","game","ui","rtp","fastdelegate"}
REQUIRED={"geometry_primitives.cpp","geometry_utils.cpp",
    "strings_functions.cpp","buffer_string.cpp",
    "fs_path.cpp","fs_path_win.cpp","timing_functions_win.cpp",
    "math_aabb.cpp","math_frustum.cpp","math_obb.cpp",
    "math_quaternion.cpp","math_sphere.cpp"}

def audit()->dict:
    cmake=(ROOT/"CMakeLists.txt").read_text(encoding="utf-8")
    engine=(ROOT/"engine/northstarcpp/CMakeLists.txt").read_text(encoding="utf-8")
    sources=sorted(BASE.rglob("*.cpp"))
    # Validate original translation units *now compiled in first-level modules*.
    # Merely retaining a file in the archive is not sufficient.
    module_root=ROOT/"engine/northstarcpp/modules"
    promoted={
        "geometry_primitives.cpp":"geometry/geometry_primitives.cpp",
        "geometry_utils.cpp":"geometry/geometry_utils.cpp",
        "timing_functions_win.cpp":"time/timing_functions_win.cpp",
        "math_aabb.cpp":"core/math_aabb.cpp",
        "math_aabb_plane.cpp":"core/math_aabb_plane.cpp",
        "math_constants.cpp":"core/math_constants.cpp",
        "math_cuboid.cpp":"core/math_cuboid.cpp",
        "math_cylinder.cpp":"core/math_cylinder.cpp",
        "math_float3.cpp":"core/math_float3.cpp",
        "math_float4x4.cpp":"core/math_float4x4.cpp",
        "math_frustum.cpp":"core/math_frustum.cpp",
        "math_obb.cpp":"core/math_obb.cpp",
        "math_quaternion.cpp":"core/math_quaternion.cpp",
        "math_sphere.cpp":"core/math_sphere.cpp",
        "strings_functions.cpp":"core/strings_functions.cpp",
        "buffer_string.cpp":"core/buffer_string.cpp",
        "fs_path.cpp":"core/fs_path.cpp",
        "fs_path_win.cpp":"core/fs_path_win.cpp",
    }
    compiled=set()
    errors=[]
    for name,relative in promoted.items():
        if not (module_root/relative).is_file():
            errors.append("Missing first-class original source: "+relative)
            continue
        ref=(chr(36)+"{northstarcpp_math_dir}/"+name if relative.startswith("core/")
             else "engine/northstarcpp/modules/"+relative)
        if ref not in cmake:
            errors.append("Original source not compiled by native module: "+relative)
        else:
            compiled.add(name)
    if not REQUIRED <= compiled:
        errors.append("Original core units not compiled: "+str(sorted(REQUIRED-compiled)))
    pending=list(sources)
    counts=Counter(p.relative_to(BASE).parts[0] for p in sources)
    if len(sources)<1000:
        errors.append("Unported historical archive unexpectedly shrunk")
    for relative in (
        "core/math_aabb.cpp","geometry/geometry_utils.cpp",
        "collision/ray_triangle_core.cpp","time/timing_functions_win.cpp",
        "time/frame_clock.cpp","models/models.cpp",
        "animation/animation.cpp","ui/ui_quad.h"):
        if not (module_root/relative).is_file():
            errors.append("Missing first-class engine input: "+relative)
    if not ("target_link_libraries(northstarcpp_runtime PUBLIC northstarcpp_math"
            in engine and "northstarcpp_core" in engine):
        errors.append("Original foundation is not linked directly to engine runtime")
    if 'install(DIRECTORY "${NORTHSTARCPP_PUBLIC_INCLUDE_DIR}/northstarcpp"' not in cmake:
        errors.append("Maintained API headers are not installed into SDK")
    if (ROOT/"tests/foundation_original_smoke.cpp").is_file() is False:
        errors.append("Missing real linked-original-source smoke test")

    # Prevent accidentally reverting to a copied algorithm or an API wrapper.
    provider=(ROOT/"engine/northstarcpp/modules/geometry/geometry.cpp"
              ).read_text(encoding="utf-8")
    for call in ("create_cube(","create_cone(","create_cylinder(",
                 "create_ellipsoid(","create_ring(","create_torus("):
        if call not in provider:
            errors.append("Missing original engine.geometry call: "+call)
    if "northstarcpp_builtin_module(northstarcpp_runtime geometry geometry" not in engine:
        errors.append("Original engine.geometry builtin registration missing")
    if not re.search(r'target_link_libraries[(]northstarcpp_geometry PRIVATE'
                     r'[^)]*northstarcpp_core',engine,re.S):
        errors.append("Native external geometry provider lost foundation_core")
    if not (ROOT/"tests/geometry_foundation_source_parity.cpp").exists():
        errors.append("Missing builtin/external exact parity regression")

    # Source ownership is distinct from runtime integration status.
    provider_plan={
      "core":("partial_native","engine.time, engine.assets, engine.logger, engine.scripting"),
      "animation":("partial_native","scene transform transition curve"),
      "collision":("pending","engine.collision, engine.physics"),
      "physics":("pending","engine.physics (optional ODE provider)"),
      "render":("pending","engine.render"),
      "sound":("pending","engine.audio"),
      "input":("pending","engine.input"),
      "network":("pending","engine.network"),
      "ui":("pending","engine.ui"),
      "rtp":("pending","engine.animation"),
      "game":("pending","engine.gameplay"),
      "engine":("pending","host services; no duplicate engine main loop"),
    }
    runtime_plan={
        name:{"cpp":counts[name],"status":status,"contracts":api}
        for name,(status,api) in provider_plan.items()
    }
    tool_families=(
       "editor","maya","maya_animation","shader_generator",
       "shader_compiler","xbox_debug_client","test","test_form",
       "test_animations","texture_compressor_test","graphics_benchmark")
    return {
        "source_directory":str(BASE.relative_to(ROOT)).replace(chr(92),"/"),
        "promoted_native_sources":dict(sorted(promoted.items())),
        "total_cpp":len(sources),"compiled_original_cpp":len(compiled),
        "compiled_names":sorted(compiled),"pending_cpp":len(pending),
        "platform_specific_pending_cpp":sum(
            any(x in p.name.lower() for x in
                ("_ps3","_xbox360","_win_xbox360")) for p in pending),
        "runtime_subsystem_candidates":sum(counts[k] for k in RUNTIME_SYSTEMS),
        "cpp_by_subsystem":dict(sorted(counts.items())),
        "native_geometry_provider":{
            "capability":"engine.geometry",
            "api_version":1,"provider_version":"1.1.0",
            "modes":["builtin","external"],
            "implementation":"modules/geometry/geometry_utils.cpp",
            "verified_parity_test":"northstarcpp_foundation_source_builtin/external"
        },
        "runtime_provider_migration":runtime_plan,
        "tool_only_cpp":sum(counts[name] for name in tool_families),
        "errors":errors,
    }

if __name__=="__main__":
    parser=argparse.ArgumentParser()
    parser.add_argument("--json",action="store_true")
    args=parser.parse_args()
    report=audit()
    if args.json:print(json.dumps(report,indent=2,ensure_ascii=False))
    else:
        print("Original compiled cpp:",report["compiled_original_cpp"])
        print("Other foundation translation units pending:",report["pending_cpp"])
        print("Per subsystem:",report["cpp_by_subsystem"])
        for error in report["errors"]:print("ERROR:",error)
    raise SystemExit(1 if report["errors"] else 0)
