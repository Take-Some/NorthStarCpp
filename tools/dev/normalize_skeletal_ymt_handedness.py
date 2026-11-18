"""Offline YMT joint-name handedness correction; no engine-specific exceptions.

Only authored semantic joint names (and their parent-name references) change.
The 1,033 joint indices, transforms, clips, skin weights and YCD track IDs
are untouched. Validates that canonical right/left anchors resolve uniquely.
"""
import argparse,hashlib,json,re,struct,sys,zlib
from pathlib import Path
from blake3 import blake3
import xml.etree.ElementTree as ET


def convert(source:Path):
    data=source.read_bytes()
    assert data[:4]==b"NEF8" and data[4]==2 and data[5]>=6
    assert struct.unpack_from("<H",data,6)[0]==10
    flags=struct.unpack_from("<H",data,8)[0]
    compressed,expanded=struct.unpack_from("<QQ",data,16)
    assert flags&1 and compressed>0 and expanded<64*1024*1024
    offset=len(data)-compressed
    assert offset>=64
    metadata=data[64:offset]
    body=zlib.decompress(data[offset:],-15)
    assert len(body)==expanded
    if flags&4:assert blake3(body).digest()==data[32:64]
    source_xml=ET.fromstring(body)
    old_joint=list(source_xml.iter("Joint"))
    before=[j.attrib["name"] for j in old_joint]
    assert len(before)==1033 and len(set(before))==len(before)
    def rename(name):
        if name.startswith("l_"):return "r_"+name[2:]
        if name.startswith("r_"):return "l_"+name[2:]
        return name
    # Modify only the lexical name/parent attributes on each Joint.
    # Preserve the original XML and anchors; old physical-side labels were
    # reversed. Anchor 'right_hand=r_wrist' now points at the physical right.
    text=body.decode("utf-8")
    changed=[0]
    def joint_replace(match):
        tag=match.group(0)
        def attribute(m):
            new=rename(m.group(2))
            changed[0]+=new!=m.group(2)
            return m.group(1)+new+m.group(3)
        return re.sub(r'((?:name|parent)=")([^"]+)(")',attribute,tag)
    updated=re.sub(r'<Joint [^>]+/>',joint_replace,text)
    tree=ET.fromstring(updated)
    after=[j.get("name") for j in tree.iter("Joint")]
    assert len(after)==len(before) and len(set(after))==len(after)
    assert after==[rename(n) for n in before]
    for joint in tree.iter("Joint"):
        parent=joint.get("parent")
        index=int(joint.get("parent_index"))
        if index>=0:assert parent==after[index]
    anchors=next(tree.iter("Anchors")).attrib
    assert anchors["right_hand"]=="r_wrist" and anchors["left_hand"]=="l_wrist"
    assert anchors["eye"] in after
    raw=updated.encode("utf-8")
    codec=zlib.compressobj(6,wbits=-15)
    payload=codec.compress(raw)+codec.flush()
    header=bytearray(data[:64])
    struct.pack_into("<QQ",header,16,len(payload),len(raw))
    struct.pack_into("<H",header,8,flags|4)
    header[32:64]=blake3(raw).digest()
    packed=bytes(header)+metadata+payload
    return packed,{"joint_count":len(after),"renamed_attributes":changed[0],
        "semantic_r_shoulder_index":after.index("r_shoulder"),
        "semantic_r_wrist_index":after.index("r_wrist"),
        "sha256":hashlib.sha256(packed).hexdigest()}


if __name__=="__main__":
    p=argparse.ArgumentParser()
    p.add_argument("source",type=Path)
    p.add_argument("destination",type=Path)
    a=p.parse_args()
    try:
        assert a.source.resolve()!=a.destination.resolve()
        assert not a.destination.exists()
        value,report=convert(a.source)
        a.destination.parent.mkdir(parents=True,exist_ok=True)
        a.destination.write_bytes(value)
        check=value
        assert blake3(zlib.decompress(check[-struct.unpack_from("<Q",check,16)[0]:],-15)).digest()==check[32:64]
        print(json.dumps(report))
    except Exception as ex:
        print("YMT conversion ERROR:",ex,file=sys.stderr)
        raise SystemExit(2)
