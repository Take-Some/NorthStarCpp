"""Synthetic-only end-to-end test of the native YCD install gate."""
import hashlib,json,struct,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from blake3 import blake3
import zlib

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
sys.path.insert(0,str(HERE))
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/"tools"))
from test_native_ice_ycd_pipeline import ice,ymt
from tools.dev import convert_native_animation_ycd as batch

def sha(b):return hashlib.sha256(b).hexdigest()

class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix="northstar-native-import-test-")
        self.base=Path(self.temp.name)
        self.decode=self.base/"decoded"/"ellie"
        self.decode.mkdir(parents=True)
        self.shared=self.base/"shared"
        path=self.shared/"ellie"/"models"/"ellie.ymt"
        path.parent.mkdir(parents=True)
        path.write_bytes(ymt())
        self.rig=path
        self.source=self.base/"original.iceanim"
        self.source.write_bytes(ice())
        root_samples=[[0,0,0,0,0,0,1,1,1,1],
                      [.1,0,0,0,0,0,1,1,1,1],
                      [.2,0,0,0,0,0,1,1,1,1]]
        self.document={
            "schema":"northstar.tlou2.decoded_joint_tracks.v1",
            "character":"ellie",
            "decoder":{"name":"synthetic-validation-only","verified":True},
            "coordinate_basis":"source_native_ymt",
            "source_file":str(self.source),
            "source_sha256":sha(ice()),
            "skeleton_ymt":str(path),
            "skeleton_sha256":sha(ymt()),
            "skeleton_reference":"characters/ellie/models/ellie.ymt",
            "logical_path":"animations/npcs/native/ellie/native-test.ycd",
            "clips":[{
                "name":"native-test","frames":3,"sample_rate_hz":30.,
                "loop":False,"static":False,"root_motion":"preserve",
                "tracks":[{"joint":"root","samples":root_samples}]
            }]
        }
        self.manifest=self.decode/"native-test.json"
        self.manifest.write_text(json.dumps(self.document),encoding="utf8")
        self.output=self.base/"private"
        self.project=self.base/"project"
        self.shared_patch=patch.object(batch,"SHARED",self.shared)
        self.shared_patch.start()
    def tearDown(self):
        self.shared_patch.stop()
        self.temp.cleanup()
    def test_full_roundtrip_and_idempotent_project_install(self):
        report=batch.run(self.decode.parent,self.output,self.project,True,True)
        self.assertEqual((report["validated"],report["installed"],report["rejected"]),(1,1,0))
        a=self.output/"ellie"/"native-test.ycd"
        b=self.project/"ellie"/"native-test.ycd"
        self.assertEqual(a.read_bytes(),b.read_bytes())
        self.assertEqual(batch.verify_ycd(a.read_bytes(),"native-test")["frames"],3)
        second=batch.run(self.decode.parent,self.output,self.project,True,True)
        self.assertEqual(second["clips"][0]["project"],"unchanged")
    def test_refuses_overwrite_modified_project(self):
        self.project.joinpath("ellie").mkdir(parents=True)
        changed=self.project/"ellie"/"native-test.ycd"
        changed.write_bytes(b"modified")
        report=batch.run(self.decode.parent,self.output,self.project,True,True)
        self.assertEqual(report["rejected"],1)
        self.assertEqual(changed.read_bytes(),b"modified")
    def test_unknown_source_cannot_be_converted(self):
        self.document["source_file"]=str(self.base/"untrusted.stm")
        self.manifest.write_text(json.dumps(self.document),encoding="utf8")
        report=batch.run(self.decode.parent,self.output,self.project,True,True)
        self.assertEqual((report["validated"],report["rejected"]),(0,1))
    def test_hash_mismatch_blocks_install(self):
        self.document["source_sha256"]="00"*32
        self.manifest.write_text(json.dumps(self.document),encoding="utf8")
        report=batch.run(self.decode.parent,self.output,self.project,True,True)
        self.assertEqual((report["installed"],report["rejected"]),(0,1))
    def test_no_verified_decodes_means_no_output(self):
        report=batch.run(self.base/"none",self.output,self.project,True,True)
        self.assertEqual((report["inputs"],report["installed"]), (0,0))
        self.assertFalse(self.project.exists())

if __name__=="__main__":unittest.main()
