#!/usr/bin/env python3
"""Export original Ellie PAK components to separate native YDD static previews.

Geometry is extracted from source, no invented vertices. Native YDD preview
does not yet preserve skeleton weights, original materials, or YCD animation.
"""
from pathlib import Path
import argparse, hashlib, json, sys
from native_pak import NaughtyDogPak
from native_mesh_preview import read_meshes, build_nvrm, pack_nef8_v2, BASE

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source",type=Path,default=BASE/"source/actor97")
    parser.add_argument("--output",type=Path,default=BASE/"models/parts")
    parser.add_argument("--report",type=Path,default=BASE/"source/native-ellie-mesh-parts-report.json")
    parser.add_argument("--overwrite",action="store_true")
    a=parser.parse_args()
    report={"status":"original_geometry_static_preview_not_skinned","files":[],"no_geometry":[]}
    try:
        for source in sorted(a.source.glob("ellie-*.pak")):
            pak=NaughtyDogPak(source)
            if not pak.typed("GEOMETRY_1"):
                report["no_geometry"].append(source.name)
                continue
            meshes=read_meshes(pak)
            if not meshes:
                raise ValueError(f"No LOD0 submeshes: {source}")
            dest=a.output/(source.stem+".ydd")
            if dest.exists() and not a.overwrite:
                raise FileExistsError(f"Output already exists: {dest}")
            body,metadata=build_nvrm(meshes,source.stem)
            binary=pack_nef8_v2(body,"characters/ellie/models/parts/"+dest.name)
            dest.parent.mkdir(parents=True,exist_ok=True)
            temporary=dest.with_suffix(".ydd.tmp")
            temporary.write_bytes(binary)
            temporary.replace(dest)
            report["files"].append({
                "source":source.name,
                "path":str(dest),
                "meshes":len(meshes),
                "vertices":sum(len(m["position"]) for m in meshes),
                "triangles":sum(len(m["indices"])//3 for m in meshes),
                "skinned_stream_meshes":sum(m["skin"] is not None for m in meshes),
                "sha256":hashlib.sha256(binary).hexdigest()
            })
        report["total_native_parts"]=sum(m["meshes"] for m in report["files"])
        report["total_native_vertices"]=sum(m["vertices"] for m in report["files"])
        report["total_native_triangles"]=sum(m["triangles"] for m in report["files"])
        report["static_only"]=True
        report["skin_weights_preserved"]=all(f["skinned_stream_meshes"] == f["meshes"] for f in report["files"])
        report["runtime_skinning_enabled"]=False
        report["native_ymat_references_emitted"]=all(f["skinned_stream_meshes"] == f["meshes"] for f in report["files"])
        report["materials_preserved"]=False  # Original physical-character.fx shader is not recompiled
        report["material_semantics_note"]="Original source material parameters are separately preserved in XML YMAT; rendering uses pbr.default"
        report["runtime_skinning_enabled"]=False
        a.report.parent.mkdir(parents=True,exist_ok=True)
        a.report.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")
        print(json.dumps(report,ensure_ascii=False,indent=2))
        return 0
    except (OSError,ValueError,KeyError) as e:
        print(f"Native geometry batch failed: {e}",file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
