#!/usr/bin/env python3
"""Native Isaac extraction from owned The Last of Us Part II Remastered PC PAKs.

Preserves real geometry, UV/normals, 12 source skin weights, skeleton, original
BCn source GPU blocks, original physical-character.fx material parameters and
source binding hashes. No fabricated meshes, no copying Ellie/Abby resources.
The original Naughty Dog shaders, unresolved helper bind poses and animations
are NOT represented as finished, playable engine systems.

Outputs native NEF8 YDD/YMT/YTD/YMAT to private sharedAssets/characters/isaac.
Does not commit or distribute game assets.
"""
from __future__ import annotations
import argparse, collections, hashlib, json, math, struct, sys, zlib
from pathlib import Path
from blake3 import blake3
import native_ymat as source_mat
import native_ytd as source_tex
from native_pak import NaughtyDogPak, PakError, ymt_xml
from native_mesh_preview import read_meshes, build_nvrm

ROOT=Path(__file__).resolve().parents[1]
ORIGINAL=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives\sp-common\actor97")
BASE=ROOT/"engine/northstarcpp/resources/sharedAssets/characters/isaac"
PART_NAMES=(
    "isaac-body","isaac-head",
    "isaac-seattle-jacket-body","isaac-seattle-jacket-cloth",
    "isaac-apple-eat")
RIGS={
    "main":("isaac-skel","isaac.ymt"),
    "cloth":("isaac-seattle-jacket-cloth","isaac_jacket_cloth.ymt"),
    "apple":("isaac-apple-eat","isaac_apple.ymt"),
}
PART_RIG={
    "isaac-body":"isaac.ymt",
    "isaac-head":"isaac.ymt",
    "isaac-seattle-jacket-body":"isaac.ymt",
    "isaac-seattle-jacket-cloth":"isaac_jacket_cloth.ymt",
    "isaac-apple-eat":"isaac_apple.ymt",
}
ASSET_PREFIX="characters/isaac/"
def serialized_json(obj):
    return json.dumps(obj,ensure_ascii=False,indent=2).encode("utf8")+b"\n"

def atomic(path:Path,payload:bytes,force:bool):
    if path.exists() and not force:
        raise FileExistsError(f"Refusing existing file without --force: {path}")
    path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_suffix(path.suffix+".tmp")
    tmp.write_bytes(payload)
    tmp.replace(path)

def nef8(raw:bytes,name:str,kind:int,entry_count:int=1):
    compressor=zlib.compressobj(level=9,wbits=-15)
    packed=compressor.compress(raw)+compressor.flush()
    header=bytearray(64)
    header[:4]=b"NEF8"
    struct.pack_into("<BBHHHIQQ",header,4,2,6,kind,7,1,entry_count,
                     len(packed),len(raw))
    header[32:64]=blake3(raw).digest()
    meta=serialized_json({
        "schema":"newengine.asset.list_file.header_metadata",
        "logical_path":name,
        "content_kind":("nvrm_mesh" if kind==6 else "opaque:10"),
        "source":"sp-common.psarc/actor97 original Isaac PAK",
        "status":"converted original assets with source skin/material metadata",
        "warnings":["Original shader semantics and helper bind poses require visual QA"]
    })
    return bytes(header)+meta+packed

def check_nef8(data,raw,kind):
    if data[:4]!=b"NEF8" or struct.unpack_from("<H",data,6)[0]!=kind:
        raise ValueError("NEF8 resource type mismatch")
    z=struct.unpack_from("<Q",data,16)[0]
    actual=zlib.decompress(data[-z:],-15)
    if actual!=raw or blake3(raw).digest()!=data[32:64]:
        raise ValueError("NEF8 source checksum/roundtrip mismatch")

def extract_rigs(root,paks,force,report):
    values={}
    for key,(name,output) in RIGS.items():
        rig=paks[name].skeleton()
        payload=ymt_xml(rig).replace(b"skel.ellie-skel",
                                     ("skel."+name).encode("ascii"))
        name_on_disk="models/"+output
        archive=nef8(payload,ASSET_PREFIX+name_on_disk,10)
        check_nef8(archive,payload,10)
        atomic(root/name_on_disk,archive,force)
        report["skeletons"][key]={
            "name":name_on_disk,"joint_count":rig["joint_count"],
            "authored_bind_transforms":rig["authored_transform_count"],
            "inferred_helpers":rig["reconstructed_helper_transforms"],
            "sha256":hashlib.sha256(archive).hexdigest()
        }
        values[output]=rig["joint_count"]
    return values

def collect_vram(root,paks,force,report):
    src=root/"source/native_vram"
    sources=[]
    for name in PART_NAMES:
        pak=paks[name];records=pak.typed("VRAM_DESC")
        if not records:continue
        base=pak.pages[-1][0]+pak.pages[-1][1]
        combined=bytearray();entries=[]
        for index,desc in enumerate(records):
            offset=desc["offset"]+16
            start=base+pak.u32(offset+40)
            size=pak.u32(offset+48)
            width=pak.u32(offset+84)
            height=pak.u32(offset+88)
            if not 1<=size<=128*1024*1024 or start+size>len(pak.data):
                raise PakError(f"{name}: VRAM source extent invalid")
            if not 1<=width<=16384 or not 1<=height<=16384:
                raise PakError(f"{name}: VRAM dimensions invalid")
            data=pak.data[start:start+size]
            entries.append({
                "index":index,"hash64":f"{pak.u64(offset+56):016x}",
                "dxgi":pak.u32(offset+72),
                "width":width,"height":height,"mips":pak.u32(offset+80),
                "blob_offset":len(combined),"blob_bytes":size,
                "sha256":hashlib.sha256(data).hexdigest(),
                "source_file_offset":start,
            })
            combined.extend(data)
        output=src/(name+".vram")
        atomic(output,bytes(combined),force)
        sources.append({
            "pak":name+".pak","vram_blob":str(output),
            "gpu_blocks":len(entries),"total_bytes":len(combined),
            "entries":entries,
        })
    manifest={"schema":"northstar.source.pak.vram.v1",
              "status":"original_GPU_blocks_not_YTD",
              "sources":sources}
    atomic(src/"vram.manifest.json",serialized_json(manifest),force)
    tex,duplicates=source_tex.load_sources(src/"vram.manifest.json")
    report["vram"]={
        "source_descriptors":sum(len(x["entries"]) for x in sources),
        "unique_hashes":len(tex),"duplicates":duplicates,
        "formats":dict(collections.Counter(x["dxgi"] for x in tex)),
    }
    return tex,duplicates

def material_list(paks,hashes):
    by_name={}
    instances=0
    for name in PART_NAMES:
        pak=paks[name]
        desc=pak.typed("GEOMETRY_1")
        if not desc:continue
        base=desc[0]["offset"]
        table=pak.pointer(base+88)
        seen=set()
        for i in range(pak.u32(base+56)):
            mat=pak.pointer(table+i*192+72)
            if mat in seen:continue
            seen.add(mat);instances+=1
            item=source_mat.extract_one(pak,mat,hashes)
            item["name"]+=f"_{mat:08x}"
            name=item["name"]
            if name in by_name:
                if item!=by_name[name]:
                    raise PakError(f"Source material name collision: {name}")
            else:by_name[name]=item
    return list(by_name.values()),instances

def colorspace(tex,materials):
    purpose=collections.defaultdict(set)
    for mat in materials:
        for ref in mat["textures"]:
            name=ref["name"].lower()
            if any(n in name for n in ("basecolor","albedo","diffuse","emissive")):
                category="color"
            elif any(n in name for n in ("normal","mask","rough","metal",
                                          "specular","ao0","occlusion","curvature")):
                category="data"
            else:category="unknown"
            purpose[ref["hash64"]].add(category)
    stats=collections.Counter()
    for t in tex:
        dxgi=t["dxgi"]
        tags=purpose[t["hash64"]]
        if dxgi==72:
            t["colorspace"]=2;kind="explicit_srgb"
        elif dxgi in (71,77,98) and "color" in tags and "data" not in tags:
            t["colorspace"]=2;kind="inferred_srgb"
        else:
            t["colorspace"]=1;kind="linear_or_ambiguous"
        stats[kind]+=1
    return dict(stats)

def native_geometry(meshes,logical,rig_path,rig_bones):
    if not meshes:raise PakError("Isaac native drawable has no original geometry")
    for mesh in meshes:
        skin=mesh.get("skin")
        if skin is None:raise PakError("Source Isaac skin influences missing")
        for idx,(joints,weights) in enumerate(zip(skin["joints"],skin["weights"])):
            if any(w<0 or not math.isfinite(w) for w in weights):
                raise PakError("Invalid Isaac skin weights")
            if sum(weights)<.98 or sum(weights)>1.02:
                raise PakError(f"Isaac skin weights not normalized: {idx}")
            if any(j>=rig_bones for j,w in zip(joints,weights) if w>0):
                raise PakError(f"Skin bone outside {rig_path} ({rig_bones} joints)")
    body,old=build_nvrm(meshes,logical)
    size=struct.unpack_from("<I",body,8)[0]
    meta=json.loads(body[20:20+size])
    meta["material_library"]=ASSET_PREFIX+"materials/isaac.ymat"
    for mesh in meta["meshes"]:
        mesh["skin_binding"]["skeleton"]=ASSET_PREFIX+"models/"+rig_path
        mesh["skin_binding"]["runtime_skinning_enabled"]=False
    encoded=json.dumps(meta,separators=(",",":"),ensure_ascii=False).encode("utf8")
    vertex_data=body[20+size:]
    raw=b"NVRM"+struct.pack("<IIQ",1,len(encoded),len(vertex_data))+encoded+vertex_data
    data=nef8(raw,ASSET_PREFIX+logical,6)
    check_nef8(data,raw,6)
    return data,meta

def write_meshes(root,paks,rig_count,force,report):
    cache={}
    for name in PART_NAMES:
        pak=paks[name]
        cache[name]=read_meshes(pak,0)
        # Disambiguate equal material UUIDs with distinct native PAK offsets
        # (e.g. Isaac beard gray versus dithered material instances).
        source_geometry=pak.typed("GEOMETRY_1")[0]["offset"]
        source_first=pak.pointer(source_geometry+88)
        material_names=[]
        for i in range(pak.u32(source_geometry+56)):
            sm=source_first+i*192
            label=pak.ascii(pak.pointer(sm+32)).split("|")[-1]
            found=__import__("re").search(r"LODShape(\d+)",label,__import__("re").I)
            lod=int(found.group(1)) if found else 0
            if lod>0:continue
            source_material=pak.pointer(sm+72)
            material_names.append(f"mat_{pak.u64(source_material+16):016x}_{pak.path.stem}_{source_material:08x}")
        if len(material_names)!=len(cache[name]):
            raise PakError("Isaac source material/submesh alignment failed")
        for mesh,mat_name in zip(cache[name],material_names):
            mesh["material_name"]=mat_name
        parts=cache[name];ref=PART_RIG[name]
        target="models/parts/"+name+".ydd"
        payload,meta=native_geometry(parts,target,ref,rig_count[ref])
        atomic(root/target,payload,force)
        report["parts"][name]={
            "path":target,"submeshes":len(parts),
            "vertices":sum(len(m["position"]) for m in parts),
            "triangles":sum(len(m["indices"])//3 for m in parts),
            "rig":ref,"sha256":hashlib.sha256(payload).hexdigest(),
        }
    for target,names in {
        "models/isaac.ydd":("isaac-body","isaac-head"),
        "models/isaac_seattle_jacket_base.ydd":
            ("isaac-seattle-jacket-body","isaac-head")
    }.items():
        meshes=[m for key in names for m in cache[key]]
        payload,meta=native_geometry(meshes,target,"isaac.ymt",rig_count["isaac.ymt"])
        atomic(root/target,payload,force)
        report["assembled"][target]={
            "sources":list(names),"submeshes":len(meshes),
            "vertices":sum(len(m["position"]) for m in meshes),
            "triangles":sum(len(m["indices"])//3 for m in meshes),
            "sha256":hashlib.sha256(payload).hexdigest()
        }
    return cache

def run():
    arg=argparse.ArgumentParser(description=__doc__)
    arg.add_argument("--source",type=Path,default=ORIGINAL)
    arg.add_argument("--output",type=Path,default=BASE)
    arg.add_argument("--force",action="store_true")
    arg.add_argument("--preserve-fullres",action="store_true",
        help="Keep existing verified texturedict3 Isaac YTD; rebuild meshes and YMAT only")
    a=arg.parse_args()
    try:
        existing_ytd=a.output/"textures/isaac.ytd"
        # Fail before touching ANY output if a high-resolution asset exists.
        if (existing_ytd.exists() and existing_ytd.stat().st_size>2*1024*1024
            and not a.preserve_fullres):
            raise ValueError("Full-resolution Isaac YTD is protected: use --preserve-fullres")
        if a.preserve_fullres and not existing_ytd.is_file():
            raise FileNotFoundError("No full-resolution Isaac YTD to preserve")
        needed=sorted(set(PART_NAMES)|{v[0] for v in RIGS.values()})
        paks={name:NaughtyDogPak(a.source/(name+".pak")) for name in needed}
        report={
            "schema":"northstar.tlou2.isaac.native_import.v1",
            "game":"The Last of Us Part II Remastered PC",
            "status":"native_geometry_textures_materials_and_partial_skeleton",
            "source_paks":[{
                "filename":name+".pak",
                "source":str(a.source/(name+".pak")),
                "sha256":hashlib.sha256(paks[name].data).hexdigest()
            } for name in needed],
            "skeletons":{},"parts":{},"assembled":{}
        }
        # Work with untouched original PAK files; only output to sharedAssets.
        rigs=extract_rigs(a.output,paks,a.force,report)
        tex,duplicates=collect_vram(a.output,paks,a.force,report)
        available={t["hash64"] for t in tex}
        mats,instances=material_list(paks,available)
        report["vram"]["colorspace"]=colorspace(tex,mats)
        if a.preserve_fullres:
            ytd=existing_ytd.read_bytes()
            ytd_info=source_tex.validate_ytd(ytd,len(tex))
            if ytd_info["mip_levels"]<=sum(len(t["mips"]) for t in tex):
                raise ValueError("Preserved Isaac YTD does not include streaming mips")
            fullres_report=a.output/"source/fullres-texture-report.json"
            manifest=json.loads(fullres_report.read_text("utf8"))
            if manifest.get("native",{}).get("sha256")!=hashlib.sha256(ytd).hexdigest():
                raise ValueError("Isaac full-resolution YTD checksum not documented")
            notes="Preserved original validated full-resolution texturedict3 mip chains"
        else:
            ytd=source_tex.encode_nef8(
                source_tex.encode_netd(tex),len(tex),
                ASSET_PREFIX+"textures/isaac.ytd")
            ytd_info=source_tex.validate_ytd(ytd,len(tex))
            atomic(existing_ytd,ytd,a.force)
            notes="Original local PAK mip variants; not full-resolution"
        report["textures"]={
            **ytd_info,"sha256":hashlib.sha256(ytd).hexdigest(),
            "notes":notes,
        }
        # Adapt existing XML YMAT generation in-process without changing
        # the Ellie converter or modifying any source game material.
        source_mat.YTD=ASSET_PREFIX+"textures/isaac.ytd"
        source_mat.YMAT=ASSET_PREFIX+"materials/isaac.ymat"
        xml=source_mat.make_xml(mats)
        ymat=source_mat.pack_ymat(xml,len(mats))
        ymat_info=source_mat.validate(ymat,len(mats),available)
        atomic(a.output/"materials/isaac.ymat",ymat,a.force)
        report["materials"]={
            **ymat_info,"source_material_instances":instances,
            "sha256":hashlib.sha256(ymat).hexdigest(),
            "shader":"physical-character.fx source parameters + PBR adapter",
        }
        cache=write_meshes(a.output,paks,rigs,a.force,report)
        manifest={
            "schema":"northstar.character.v1",
            "id":"characters/isaac","display_name":"Isaac",
            "model":ASSET_PREFIX+"models/isaac.ydd",
            "skeleton":ASSET_PREFIX+"models/isaac.ymt",
            "appearance":{
                "material":ASSET_PREFIX+"materials/isaac.ymat",
                "textures":ASSET_PREFIX+"textures/isaac.ytd"
            },"animations":[]
        }
        atomic(a.output/"isaac.character.json",serialized_json(manifest),a.force)
        prefab=(f'NorthStarCpp.Scene 3\nscene "Isaac - Original PC Mesh"\n'
                f'streaming 150 220 1\nstream_budget 64 64\nlayer "Default" 1\n'
                f'entity 1 0 "Isaac" "Default" "" "" '
                f'"characters/isaac/models/isaac.ydd" 0 0 0 1.00 67A0C0 '
                f'0 0 0 0 0 1 1 1 0 1000000 1 0\n').encode("utf8")
        atomic(ROOT/"engine/northstarcpp/resources/sharedAssets/prefabs/characters/isaac.nprefab",
               prefab,a.force)
        report["limits"]=[
            "Main model uses original isaac-body+isaac-head LOD0; no dummy geometry",
            "Jacket cloth uses its own 253-bone rig; exported separately, not attached to main skeleton",
            "Isaac main skeleton 757 joints includes inferred secondary bind poses",
            "No original runtime ICE animation clips have been retargeted to YCD",
            "All 12 original bone influences retained in NVRM; runtime uses up to 8",
            "Texture dictionary retains validated full-resolution streaming mips only with --preserve-fullres",
            "Source physical-character.fx shaders approximated by PBR adapter; viewport QA pending",
            "Imported game assets must NOT be pushed to public repository or distributed"
        ]
        atomic(a.output/"source/native-isaac-report.json",
               serialized_json(report),a.force)
        print(json.dumps({
            "model":str(a.output/"models/isaac.ydd"),
            "rigs":report["skeletons"],
            "primary":report["assembled"]["models/isaac.ydd"],
            "variant":report["assembled"]["models/isaac_seattle_jacket_base.ydd"],
            "texture":report["textures"],
            "materials":report["materials"],
            "exported_parts":len(report["parts"]),
            "prefab":"prefabs/characters/isaac.nprefab",
            "animation_ycd":False,
            "skinning_visual_qa":False,
        },ensure_ascii=False,indent=2))
        return 0
    except (OSError,ValueError,KeyError,struct.error) as err:
        print("NATIVE ISAAC CONVERSION ERROR:",err,file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(run())
