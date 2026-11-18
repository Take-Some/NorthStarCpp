#!/usr/bin/env python3
"""Decode source physical-character.fx material bindings from Naughty Dog PAK.

Generates canonical NorthStar XML YMAT/NEF8 (not fake JSON). Preserves all
source shader parameters, original material identifiers, and source texture
binding names, with additional well-defined PBR semantic aliases.
"""
from __future__ import annotations
import argparse, hashlib, json, math, re, struct, sys, xml.etree.ElementTree as ET
from pathlib import Path
import zlib
from blake3 import blake3
from native_pak import NaughtyDogPak, PakError
from native_mesh_preview import BASE

YMAT="characters/ellie/materials/ellie.ymat"
YTD="characters/ellie/textures/ellie.ytd"

def extract_one(pak, addr, available):
    uid=pak.u64(addr+16)
    key=f"mat_{uid:016x}_{pak.path.stem}"
    source=pak.ascii(pak.pointer(addr))
    shader=pak.ascii(pak.pointer(addr+8))
    pc=pak.u32(addr+272);tc=pak.u32(addr+276)
    if not 0<=pc<=1024 or not 0<=tc<=1024:
        raise PakError("Unsupported source material parameter/texture count")
    tex_start=pak.pointer(addr+32);param_start=pak.pointer(addr+24)
    textures=[];parameters=[]
    for k in range(tc):
        row=tex_start+48*k
        name=pak.ascii(pak.pointer(row))
        obj=pak.pointer(row+24)
        rawpath=pak.ascii(pak.pointer(obj))
        sourcehash=f"{pak.u64(obj+8):016x}"
        if sourcehash not in available:
            raise PakError(f"Missing source texture payload {pak.path.name}@{sourcehash}")
        if not name or re.search(r"[@/\\:]",name):
            raise PakError(f"Unsafe source shader texture name {name!r}")
        textures.append({"name":name,"hash64":sourcehash,
                         "rawpath":rawpath})
    if len({x["name"] for x in textures})!=len(textures):
        raise PakError("Duplicate source texture binding name")
    for k in range(pc):
        row=param_start+k*24
        name=pak.ascii(pak.pointer(row))
        offset=pak.pointer(row+8)
        n=pak.u32(row+16)
        if n not in (1,2,3,4) or re.search(r"[@/\\:]",name):
            raise PakError("Source shader parameter invalid")
        values=pak.unpack("<"+str(n)+"f",offset)
        if any(not math.isfinite(v) for v in values):
            raise PakError("Source material parameter nonfinite")
        parameters.append({"name":name,"values":list(values)})
    if len({p["name"] for p in parameters})!=len(parameters):
        raise PakError("Duplicate source shader parameter name")
    return {"uuid":f"{uid:016x}","name":key,"source":source,
            "shader":shader,"textures":textures,"params":parameters,
            "source_pak":pak.path.name}

def all_materials(source:Path,available):
    by_uuid={};parts=0;instances=0
    for filepath in sorted(source.glob("ellie-*.pak")):
        pak=NaughtyDogPak(filepath)
        geometry=pak.typed("GEOMETRY_1")
        if not geometry:continue
        g=geometry[0]["offset"];first=pak.pointer(g+88)
        local=set()
        for i in range(pak.u32(g+56)):
            submesh=first+i*192
            mat=pak.pointer(submesh+72)
            if mat in local:continue
            local.add(mat);instances+=1
            material=extract_one(pak,mat,available)
            if material["name"] in by_uuid:
                previous=by_uuid[material["uuid"]]
                def ref_signature(o):
                    return (o["source"],o["shader"],
                            [(x["name"],x["hash64"]) for x in o["textures"]],
                            [(x["name"],x["values"]) for x in o["params"]])
                if ref_signature(previous)!=ref_signature(material):
                    raise PakError(f"Conflicting source material UUID {material['uuid']}")
            else:by_uuid[material["name"]]=material
    if not by_uuid:raise PakError("No original material bindings")
    return list(by_uuid.values()),instances

def make_xml(materials):
    root=ET.Element("YmatMaterialLibrary",
        {"version":"2","schema":"northstarcpp.ymat.material_library.v2"})
    slot_aliases={
        "BaseColor01":"base_color", "Normal01":"normal",
        "Ao01":"ao", "Roughness01":"roughness",
        "Metallic01":"metallic","Transparency01":"opacity",
    }
    for source in materials:
        material=ET.SubElement(root,"Material",{
            "name":source["name"],"shader":"pbr.default"})
        is_masked=any("Transparency01" in t["name"] for t in source["textures"])
        ET.SubElement(material,"Surface",{
            "blend":"masked" if is_masked else "opaque",
            "two_sided":"true" if is_masked else "false",
            "alpha_cutoff":"0.5" if is_masked else "0"})
        tex_root=ET.SubElement(material,"Textures")
        alias_seen=set()
        for original,canonical in slot_aliases.items():
            for tex in source["textures"]:
                if canonical in alias_seen:break
                if original in tex["name"]:
                    ET.SubElement(tex_root,"Texture",{
                        "slot":canonical,"ref":f"{YTD}@tex_{tex['hash64']}"})
                    alias_seen.add(canonical)
                    break
        # Default PBR eye color uses original sclera where available.
        if "base_color" not in alias_seen:
            for candidate in ("ScleraColorMap","IrisColor01Map"):
                tex=next((t for t in source["textures"] if candidate in t["name"]),None)
                if tex:
                    ET.SubElement(tex_root,"Texture",{"slot":"base_color","ref":f"{YTD}@tex_{tex['hash64']}"})
                    alias_seen.add("base_color")
                    break
        for tex in source["textures"]:
            ET.SubElement(tex_root,"Texture",{
                "slot":tex["name"],"ref":f"{YTD}@tex_{tex['hash64']}"})
        ps=ET.SubElement(material,"Params")
        for p in source["params"]:
            values=p["values"];n=len(values)
            ET.SubElement(ps,"Param",{
                "name":p["name"],
                "type":("float" if n==1 else "float"+str(n)),
                "value":",".join(format(v,".9g") for v in values)})
        # Native PBR adapter parameters derived from original physical-character.fx
        # source values. Preserve all original g_* parameters above.
        param_source={item["name"]:item["values"] for item in source["params"]}
        def pbr_param(name,values):
            values=list(values)
            ET.SubElement(ps,"Param",{
                "name":name,
                "type":"float" if len(values)==1 else "float"+str(len(values)),
                "value":",".join(format(v,".9g") for v in values)})
        base_param=param_source.get("g_baseColor01")
        if (base_param and len(base_param)>=3 and
            all(0<=v<=1 for v in base_param[:3])):
            pbr_param("base_color",base_param[:3])
        rough_param=param_source.get("g_roughness01")
        has_eye=any("IrisColor" in t["name"] or
                    "ScleraColor" in t["name"] for t in source["textures"])
        if has_eye:
            pbr_param("roughness",[.12])  # PBR glassy eye approximation
        elif (rough_param and len(rough_param)==1 and
              0<=rough_param[0]<=1):
            pbr_param("roughness",rough_param)
        ext=ET.SubElement(material,"Extensions")
        for name,value in (
            ("source_shader",source["shader"]),
            ("source_material",source["source"]),
            ("source_uuid",source["uuid"]),
            ("source_pak",source["source_pak"])):
            ET.SubElement(ext,"Property",{"name":name}).text=value
    return ET.tostring(root,encoding="utf-8",xml_declaration=True)


def pack_ymat(body,count):
    if not 0<count<=65536 or len(body)>=64*1024*1024:
        raise ValueError("Invalid source YMAT size")
    zipper=zlib.compressobj(9,wbits=-15)
    compressed=zipper.compress(body)+zipper.flush()
    head=bytearray(128);head[:4]=b"NEF8"
    struct.pack_into("<HHHHH",head,4,1,128,4,1,1)
    struct.pack_into("<QQQQ",head,16,128,len(compressed),len(body),count)
    struct.pack_into("<Q",head,112,1)
    head[64:96]=blake3(body).digest()
    return bytes(head)+compressed


def validate(packed,count,expected_textures):
    if packed[:4]!=b"NEF8" or struct.unpack_from("<H",packed,8)[0]!=4:
        raise ValueError("Not a native YMAT kind4")
    csize,expanded,num=struct.unpack_from("<QQQ",packed,24)
    if num!=count:raise ValueError("YMAT count mismatch")
    blob=zlib.decompress(packed[128:128+csize],-15)
    if len(blob)!=expanded or blake3(blob).digest()!=packed[64:96]:
        raise ValueError("YMAT BLAKE3 mismatch")
    lib=ET.fromstring(blob)
    if lib.tag!="YmatMaterialLibrary" or lib.attrib["version"]!="2":
        raise ValueError("YMAT XML schema mismatch")
    entries=lib.findall("Material")
    if len(entries)!=count:raise ValueError("YMAT XML entry count mismatch")
    for entry in entries:
        refs=entry.findall("Textures/Texture")
        names=[x.attrib["slot"] for x in refs]
        if len(names)!=len(set(names)):raise ValueError("Duplicate material slot")
        for tex in refs:
            ref=tex.attrib["ref"]
            if not ref.startswith(YTD+"@tex_") or ref.rsplit("_",1)[-1] not in expected_textures:
                raise ValueError("Material references missing source texture")
    return {"materials":count,"source_tex_refs":sum(len(e.findall("Textures/Texture")) for e in entries),
            "nef8_bytes":len(packed)}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source",type=Path,default=BASE/"source/actor97")
    p.add_argument("--textures",type=Path,default=BASE/"source/native-ellie-textures-report.json")
    p.add_argument("--output",type=Path,default=BASE/"materials/ellie.ymat")
    p.add_argument("--report",type=Path,default=BASE/"source/native-ellie-materials-report.json")
    p.add_argument("--force",action="store_true")
    a=p.parse_args()
    try:
        ytd=json.loads(a.textures.read_text("utf8"))
        available={t["hash64"] for t in ytd["texture_manifest"]}
        mats,instances=all_materials(a.source,available)
        packed=pack_ymat(make_xml(mats),len(mats))
        validation=validate(packed,len(mats),available)
        if a.output.exists() and not a.force:
            raise FileExistsError("YMAT exists: pass --force")
        a.output.parent.mkdir(parents=True,exist_ok=True)
        tmp=a.output.with_suffix(".ymat.tmp");tmp.write_bytes(packed);tmp.replace(a.output)
        report={
            "status":"native_source_materials_YMAT_XML",
            "material_instances_in_source_PAKs":instances,
            "original_shader":"physical-character.fx",
            "engine_shader":"pbr.default with original shader name in XML extensions",
            "native_ymat":str(a.output),
            "sha256":hashlib.sha256(packed).hexdigest(),
            "validation":validation,
            "materials":mats,
        }
        a.report.write_text(json.dumps(report,indent=2,ensure_ascii=False)+"\n",encoding="utf8")
        print(json.dumps({"file":str(a.output),"source_instances":instances,**validation},indent=2))
        return 0
    except (OSError,ValueError,KeyError,ET.ParseError,zlib.error) as exc:
        print("NATIVE YMAT FAILED:",exc,file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
