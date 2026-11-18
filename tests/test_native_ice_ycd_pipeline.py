"""Pure synthetic ABI fixtures; no protected game assets included."""
import hashlib
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from blake3 import blake3

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from native_ice_header import IceFormatError,parse_header
from native_decoded_tracks_to_ycd import ImportErrorInvariant,build

def ice():
    b=bytearray(384);b[:8]=bytes.fromhex("020143414c1bbe5a")
    struct.pack_into("<IHH",b,8,len(b),7,3)
    struct.pack_into("<I",b,24,48)
    struct.pack_into("<f",b,28,30.)
    struct.pack_into("<ff",b,40,2.,1/30.)
    return bytes(b)

def ymt():
    root=ET.Element("Metadata");s=ET.SubElement(root,"Skeleton")
    for i,name in enumerate(("root","pelvis")):
        ET.SubElement(s,"Joint",index=str(i),name=name,parent_index=str(i-1))
    raw=ET.tostring(root,encoding="utf-8")
    encoder=zlib.compressobj(level=9,wbits=-15)
    zipped=encoder.compress(raw)+encoder.flush()
    h=bytearray(64);h[:4]=b"NEF8"
    struct.pack_into("<BBHHHIQQ",h,4,2,6,10,7,2,1,len(zipped),len(raw))
    h[32:64]=blake3(raw).digest()
    return bytes(h)+b'{"content_kind":"opaque:10"}'+zipped

class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        r=Path(self.tmp.name)
        self.source=r/"original.iceanim";self.source.write_bytes(ice())
        self.rig=r/"hero.ymt";self.rig.write_bytes(ymt())
        poses=lambda delta:[[0,0,0,0,0,0,1,1,1,1],[delta,0,0,0,0,0,1,1,1,1],[delta*2,0,0,0,0,0,1,1,1,1]]
        self.doc={"schema":"northstar.tlou2.decoded_joint_tracks.v1",
                  "decoder":{"name":"unit-test-source","verified":True},
                  "coordinate_basis":"source_native_ymt",
                  "source_file":str(self.source),
                  "source_sha256":hashlib.sha256(ice()).hexdigest(),
                  "skeleton_ymt":str(self.rig),
                  "skeleton_sha256":hashlib.sha256(ymt()).hexdigest(),
                  "skeleton_reference":"characters/test/models/hero.ymt",
                  "logical_path":"animations/qa.ycd",
                  "clips":[{"name":"qa-run","frames":3,"sample_rate_hz":30,
                            "loop":False,"static":False,"root_motion":"preserve",
                            "tracks":[{"joint":"root","samples":poses(.25)},
                                      {"joint":"pelvis","samples":poses(.05)}]}]}
    def tearDown(self):
        self.tmp.cleanup()
    def test_original_ice_timing(self):
        h=parse_header(ice())
        self.assertEqual((h["frame_count"],h["sample_rate_hz"]),(3,30))
        self.assertFalse(h["payload_decoded"])
    def test_ycd_abi(self):
        packed,info=build(self.doc)
        self.assertEqual(info["joint_tracks"],2)
        self.assertEqual(packed[:4],b"NEF8")
        size,plain=struct.unpack_from("<QQ",packed,16)
        body=zlib.decompress(packed[-size:],-15)
        self.assertEqual((len(body),blake3(body).digest()),(plain,packed[32:64]))
        version,count,table,strings,_,payload=struct.unpack_from("<IIQQQQ",body)
        self.assertEqual((version,count,table),(3,1,48))
        self.assertEqual((struct.unpack_from("<2I",body,48+16)),(2,3))
        start,length=struct.unpack_from("<QQ",body,48+40)
        self.assertEqual((length,struct.unpack_from("<2I",body,start)),(2*(4+40*3),(0,1)))
        self.assertAlmostEqual(struct.unpack_from("<f",body,start+8+2*40)[0],.25)
    def test_wrong_sha_refused(self):
        self.doc["source_sha256"]="00"*32
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_unmapped_joint_refused(self):
        self.doc["clips"][0]["tracks"][0]["joint"]="missing"
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_missing_frame_refused(self):
        self.doc["clips"][0]["tracks"][0]["samples"].pop()
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_invalid_quat_refused(self):
        self.doc["clips"][0]["tracks"][0]["samples"][0][6]=0
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_unverified_decoder_refused(self):
        self.doc["decoder"]["verified"]=False
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_in_place_root_motion_not_silent(self):
        self.doc["clips"][0]["root_motion"]="in_place"
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_coordinate_basis_required(self):
        self.doc["coordinate_basis"]="unknown"
        with self.assertRaises(ImportErrorInvariant):build(self.doc)
    def test_skeleton_source_sha_required(self):
        self.doc["skeleton_sha256"]="ff"*32
        with self.assertRaises(ImportErrorInvariant):build(self.doc)

if __name__=="__main__":
    unittest.main()
