"""Regressions for fail-closed V8 release security and isolated QA."""
import json
from pathlib import Path
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
from release_readiness_gate import (verified_v8_attestation, verified_clean_machine_report)
from v8_provider.qa_isolate_runtimes import create_stage
from v8_provider.preflight_stable_build import check_pin

class ReleaseGateTest(unittest.TestCase):
    def test_unsigned_self_approved_json_cannot_authorize_release(self):
        with tempfile.TemporaryDirectory() as work:
            base=Path(work)
            provider=base/"scripting_v8.dll"
            provider.write_bytes(b"untrusted-unverified")
            file=base/"security.json"
            file.write_text(json.dumps({
                "approved":True,
                "v8_version":"15.8.0.0",
                "provider_sha256":"0"*64,
                "verified_stable_branch":True,
                "source_revision":"1"*40,
                "valid_until":"2999-01-01T00:00:00Z"
            }))
            passed,reason=verified_v8_attestation(file,"15.8.0.0",provider)
            self.assertFalse(passed)
            self.assertIn("signature",reason)

    def test_unsigned_clean_machine_json_cannot_approve_release(self):
        with tempfile.TemporaryDirectory() as work:
            base=Path(work)
            package=base/"package"
            package.mkdir()
            (package/"manifest.json").write_text('{"engine":"NorthStarCpp"}',encoding="utf-8")
            report=base/"qa-evidence.json"
            report.write_text(json.dumps({
                "schema":1,
                "classification":"unsigned_clean_windows_qa_evidence",
                "qa_passed":True,
                "tested_with_developer_path":False,
                "package_manifest_sha256":"0"*64,
                "checks":{
                    "independent_virtual_machine":True,
                    "developer_toolchains_absent":True,
                    "manifest_sha256":True,
                    "native_editor":True,
                    "qt_tools":True,
                    "source_project_gameplay":True,
                    "archive_project_gameplay":True
                }
            }),encoding="utf-8")
            allowed,reason=verified_clean_machine_report(report,package)
            self.assertFalse(allowed)
            self.assertTrue("signed" in reason.lower() or
                            "signature" in reason.lower())

    def test_v8_source_pin_must_be_official_stable_branch(self):
        with tempfile.TemporaryDirectory() as work:
            base=Path(work)/"source-pin.json"
            doc={"purpose":"source_pin_not_a_security_approval",
                 "source_git":"https://chromium.googlesource.com/v8/v8.git",
                 "v8_stable_branch":"refs/branch-heads/15.8",
                 "v8_stable_tip_commit":"a"*40}
            base.write_text(json.dumps(doc),encoding="utf-8")
            self.assertEqual(check_pin(base)["v8_stable_tip_commit"],"a"*40)
            doc["v8_stable_branch"]="refs/heads/main"
            base.write_text(json.dumps(doc),encoding="utf-8")
            with self.assertRaisesRegex(ValueError,"Unsupported|unsupported|malformed"):
                check_pin(base)

    def test_qa_stager_never_overwrites_existing_package(self):
        with tempfile.TemporaryDirectory() as work:
            base=Path(work)
            target=base/"out"/"qa-process-isolated"
            target.mkdir(parents=True)
            with self.assertRaisesRegex(RuntimeError,"Refusing to overwrite"):
                create_stage(base,base,target,base/"objdump.exe")

    def test_qa_stager_refuses_dist_target(self):
        with tempfile.TemporaryDirectory() as work:
            base=Path(work)
            target=base/"dist"/"release"
            with self.assertRaisesRegex(RuntimeError,"dist"):
                create_stage(base,base,target,base/"objdump.exe")


if __name__=="__main__":
    unittest.main()
