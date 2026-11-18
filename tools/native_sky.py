#!/usr/bin/env python3
"""Extract TLOU2 Remastered PC sky & cloud textures without external extractors.

Reads native GEOMETRY_1 -> sky-layered.fx material slots and VRAM_DESC from
22 sky PAKs in common/sp-common. Preserves original GPU images as DDS/DX10,
including BC6H HDR. Also builds NorthStar NEF8/NETD texture dictionaries
for sky and clouds; native 8-bit preview of BC6H is tone-mapped, and the
original BC6H HDR DDS stays untouched beside it.

This data is proprietary game content. Keep all binaries local/private.
"""
from __future__ import annotations
import argparse, collections, ctypes, hashlib, json, math, struct, sys
from pathlib import Path
import xml.etree.ElementTree as ET

from native_pak import NaughtyDogPak, PakError
from native_ytd import unpad, encode_netd, encode_nef8, validate_ytd
from native_mesh_preview import BASE as ELLIE

PROJECT=Path(__file__).resolve().parents[1]
ARCHIVES=Path(r"C:\Users\Kayla\Documents\NorthStar\Extracted\TLOU2\archives")
OUTPUT=PROJECT/"engine/northstarcpp/resources/sharedAssets/textures/sky"

FMT={28:4,71:8,72:8,77:16,80:8,83:16,95:16,98:16,99:16}
COMPRESSED=set(FMT)-{28}

def sky_paks():
    candidates=[]
    for folder in ("common","sp-common"):
        d=ARCHIVES/folder/"pak68"
        for p in d.glob("sky-*.pak"):
            if not p.stem.endswith(("-phys","-ingame","-mat")):
                candidates.append((folder,p))
    return sorted(candidates,key=lambda v:(v[0],v[1].name))

def classify(slot):
    s=slot.lower()
    if "debuggrid" in s:return "utility"
    if any(x in s for x in ("cloud","flow","uvnoise","linearblend","breakup")):
        return "clouds"
    return "sky"

def sky_content():
    presets=[]
    unique={}
    for archive,file in sky_paks():
        pak=NaughtyDogPak(file)
        last=pak.pages[-1][0]+pak.pages[-1][1]
        images={}
        for desc in pak.typed("VRAM_DESC"):
            off=desc["offset"]+16
            offset=pak.u32(off+40)
            size=pak.u32(off+48)
            hash64=f"{pak.u64(off+56):016x}"
            fmt=pak.u32(off+72)
            mip_count=pak.u32(off+80)
            width=pak.u32(off+84);height=pak.u32(off+88)
            if fmt not in FMT:raise PakError(f"Unknown sky DXGI {fmt}")
            if size<1 or last+offset+size>len(pak.data):
                raise PakError(f"GPU sky extent invalid: {file.name}:{hash64}")
            raw=pak.data[last+offset:last+offset+size]
            unpack_fmt=98 if fmt in (95,99) else fmt
            mips=unpad(raw,width,height,mip_count,unpack_fmt)
            info={"hash64":hash64,"source_pak":archive+"/"+file.name,
                  "source_sha256":hashlib.sha256(raw).hexdigest(),
                  "original_dxgi":fmt,"width":width,"height":height,
                  "mip_count":mip_count,"raw_bytes":size,
                  "mips":mips,"roles":set(),"names":set()}
            if hash64 in unique:
                existing=unique[hash64]
                if any(existing[k]!=info[k] for k in
                       ("source_sha256","original_dxgi","width","height","mip_count")):
                    raise PakError(f"Conflicting original sky GPU hash: {hash64}")
            else:unique[hash64]=info
            images[hash64]=info

        refs=[]
        for geo in pak.typed("GEOMETRY_1"):
            start=geo["offset"];sub_count=pak.u32(start+56)
            if not 0<sub_count<=256:raise PakError("Invalid sky geometry")
            first=pak.pointer(start+88)
            seen=set()
            for i in range(sub_count):
                sub=first+192*i
                mat=pak.pointer(sub+72)
                if mat in seen:continue
                seen.add(mat)
                shader=pak.ascii(pak.pointer(mat+8))
                if "sky" not in shader.lower():
                    raise PakError("Unexpected non-sky source material: "+shader)
                table=pak.pointer(mat+32);count=pak.u32(mat+276)
                if count>128:raise PakError("Sky material has too many textures")
                for j in range(count):
                    slot_row=table+j*48
                    slot=pak.ascii(pak.pointer(slot_row))
                    reference=pak.pointer(slot_row+24)
                    native_path=pak.ascii(pak.pointer(reference))
                    hash64=f"{pak.u64(reference+8):016x}"
                    if hash64 not in images:
                        raise PakError(f"Sky material refers to external/missing texture: {hash64}")
                    role=classify(slot)
                    info=unique[hash64]
                    info["roles"].add(role)
                    info["names"].add(native_path)
                    refs.append({"slot":slot,"original_path":native_path,
                                 "hash64":hash64,"category":role,
                                 "source_shader":shader})
        if not refs:raise PakError("Sky pack without material texture slots")
        presets.append({"id":file.stem,"source":archive+"/"+file.name,
                        "size_bytes":file.stat().st_size,
                        "sha256":hashlib.sha256(pak.data).hexdigest(),
                        "bindings":refs})
    if not presets:raise PakError("No sky packs in installed game")
    return presets,unique

def dds_dx10(fmt,mips):
    w,h,_=mips[0]
    header=bytearray(148)
    header[:4]=b"DDS "
    struct.pack_into("<I",header,4,124)
    struct.pack_into("<I",header,8,0x000A1007)
    struct.pack_into("<II",header,12,h,w)
    if fmt==28:
        line=w*4
    else:
        line=((w+3)//4)*FMT[fmt]*((h+3)//4)
    struct.pack_into("<I",header,20,line)
    struct.pack_into("<I",header,28,len(mips))
    struct.pack_into("<I",header,76,32)
    struct.pack_into("<I",header,80,4)
    header[84:88]=b"DX10"
    struct.pack_into("<I",header,108,0x1000 | (0x400008 if len(mips)>1 else 0))
    struct.pack_into("<IIIII",header,128,fmt,3,0,1,0)
    return bytes(header)+b"".join(v[2] for v in mips)

class HDRDecoder:
    def __init__(self,path):
        self.lib=ctypes.CDLL(str(path))
        self.fn=self.lib.ns_tlou_bc6h_decompress
        self.fn.argtypes=[ctypes.POINTER(ctypes.c_ubyte),
                          ctypes.c_uint32,ctypes.c_uint32,
                          ctypes.POINTER(ctypes.c_float),ctypes.c_uint64]
        self.fn.restype=ctypes.c_int

    def rgba8(self,width,height,compressed):
        input_bytes=(ctypes.c_ubyte*len(compressed)).from_buffer_copy(compressed)
        decoded=(ctypes.c_float*(width*height*3))()
        code=self.fn(input_bytes,width,height,decoded,len(compressed))
        if code:
            raise PakError(f"BC6H source decode failed: {code}")
        # Reinhard color mapping for preview only, from true float HDR pixels.
        pixels=bytearray(width*height*4)
        for i in range(width*height):
            for c in range(3):
                value=max(0.,float(decoded[i*3+c]))
                mapped=value/(1.+value)
                pixels[i*4+c]=min(255,int(mapped**(1/2.2)*255+0.5))
            pixels[i*4+3]=255
        return bytes(pixels)

def prepare_native(hash64,tex,decoder):
    dxgi=tex["original_dxgi"]
    if dxgi==95:
        return {
            "name":"tex_"+hash64,"hash64":hash64,"dxgi":28,
            "width":tex["width"],"height":tex["height"],
            "original_dxgi":dxgi,"source_pak":tex["source_pak"],
            "colorspace":2,"derived_preview":True,
            "mips":[(w,h,decoder.rgba8(w,h,data)) for w,h,data in tex["mips"]]
        }
    # Treat BC7 SRGB as BC7 encoded blocks with explicit SRGB flag.
    fmt=98 if dxgi==99 else dxgi
    slots=" ".join(tex["names"]).lower()
    # Explicit source SRGB always takes precedence. Other gamma selection
    # depends on channel semantics, not merely compressed format.
    colorspace=2 if dxgi in (72,99) or any(
        x in slots for x in ("sky/hdr/","fogcolor","mipfog","skycolor")) else 1
    return {
        "name":"tex_"+hash64,"hash64":hash64,"dxgi":fmt,
        "width":tex["width"],"height":tex["height"],
        "original_dxgi":dxgi,"source_pak":tex["source_pak"],
        "colorspace":colorspace,"derived_preview":False,
        "mips":tex["mips"]
    }

def sky_xml(presets):
    root=ET.Element("NorthStarSkySources",{
        "version":"1","sourceGame":"The Last of Us Part II Remastered PC",
        "sourceShader":"sky-layered.fx",
        "note":"Source shader bindings only; not a runtime engine.sky preset schema"})
    for p in presets:
        el=ET.SubElement(root,"Preset",{"id":p["id"],"source":p["source"]})
        for binding in p["bindings"]:
            category=binding["category"]
            if category=="sky":
                dictionary="textures/sky/sky.ytd"
            elif category=="clouds":
                dictionary="textures/sky/clouds.ytd"
            else:dictionary=""
            attr={
                "slot":binding["slot"],"category":category,
                "originalPath":binding["original_path"],
                "hash64":binding["hash64"]
            }
            if dictionary:
                attr["texture"]=dictionary+"@tex_"+binding["hash64"]
            ET.SubElement(el,"Binding",attr)
    return ET.tostring(root,encoding="utf-8",xml_declaration=True)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--output",type=Path,default=OUTPUT)
    ap.add_argument("--bc6h-bridge",type=Path,
                    default=PROJECT/"out/sky-tools/bc6h_bridge.dll")
    ap.add_argument("--force",action="store_true")
    args=ap.parse_args()
    try:
        presets,originals=sky_content()
        decoder=HDRDecoder(args.bc6h_bridge)
        out=args.output
        dds_dir=out/"source/dds"
        dds_dir.mkdir(parents=True,exist_ok=True)
        buckets={"sky":[],"clouds":[]}
        records=[]
        for hash64,tex in sorted(originals.items()):
            roles=sorted(tex["roles"])
            encoded_dds=dds_dx10(tex["original_dxgi"],tex["mips"])
            path=dds_dir/("tex_"+hash64+".dds")
            if path.exists() and not args.force:
                if hashlib.sha256(path.read_bytes()).digest()!=hashlib.sha256(encoded_dds).digest():
                    raise FileExistsError(f"Existing original DDS differs: {path}")
            else:
                tmp=path.with_suffix(".dds.tmp")
                tmp.write_bytes(encoded_dds)
                tmp.replace(path)
            for role in roles:
                if role not in buckets:continue
                native=prepare_native(hash64,tex,decoder)
                buckets[role].append(native)
            records.append({
                "hash64":hash64,"source":tex["source_pak"],
                "original_dxgi":tex["original_dxgi"],
                "width":tex["width"],"height":tex["height"],
                "mip_count":tex["mip_count"],"roles":roles,
                "original_shader_paths":sorted(tex["names"]),
                "source_raw_vram_sha256":tex["source_sha256"],
                "lossless_dds":str(path),
                "dds_sha256":hashlib.sha256(encoded_dds).hexdigest(),
            })
        built={}
        for role,items in buckets.items():
            if not items:raise PakError("No "+role+" textures found")
            name=role+".ytd"
            destination=out/name
            packed=encode_nef8(encode_netd(items),len(items),
                               "textures/sky/"+name)
            validation=validate_ytd(packed,len(items))
            if destination.exists() and not args.force:
                raise FileExistsError(f"Native sky YTD already exists: {destination}")
            destination.parent.mkdir(parents=True,exist_ok=True)
            tmp=destination.with_suffix(".ytd.tmp")
            tmp.write_bytes(packed)
            tmp.replace(destination)
            built[role]={
                "file":str(destination),"sha256":hashlib.sha256(packed).hexdigest(),
                "source_textures":len(items),
                "bc6h_ldr_previews":sum(x["derived_preview"] for x in items),
                **validation
            }
        xml=sky_xml(presets)
        ET.fromstring(xml)
        (out/"sky_sources.xml").write_bytes(xml)
        report={
            "schema":"northstar.tlou2.sky.import.v1",
            "status":"source_sky_and_cloud_textures_extracted",
            "source_game":"The Last of Us Part II Remastered PC",
            "original_sky_paks":len(presets),
            "source_vram_descriptors":sum(
                len(NaughtyDogPak(ARCHIVES/p["source"].split("/")[0]/"pak68"/p["source"].split("/")[1]).typed("VRAM_DESC"))
                for p in presets),
            "source_unique_textures":len(originals),
            "native":built,"presets":presets,"original_textures":records,
            "limitations":[
                "These are 64-128px local PAK mip variants, not high-res texture dictionaries.",
                "Original BC6H HDR images are preserved losslessly in DDS DX10.",
                "Native NETD LDR sky maps from BC6H are decoded/tonemapped previews.",
                "sky-layered.fx shader and runtime Engine Sky integration not implemented.",
                "Do not publish proprietary derived DDS/YTD in public repository or NSP."
            ]
        }
        (out/"source/sky.manifest.json").write_text(
            json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        print(json.dumps({
            "sky_paks":len(presets),"unique_original_textures":len(originals),
            "dds_files":len(records),"native":built,
            "presets_xml":str(out/"sky_sources.xml"),
            "report":str(out/"source/sky.manifest.json")
        },ensure_ascii=False,indent=2))
        return 0
    except (ValueError,OSError,KeyError,AttributeError) as err:
        print("NATIVE SKY EXTRACT FAILED:",err,file=sys.stderr)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
