"""Self-tests for the NorthStarCpp QA reporting and orchestration contract."""
import argparse
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET
import json

from tools import qa_runner as qa


class QAReporterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.logs = self.root / "logs"

    def test_canonical_manifest_maps_capabilities_without_duplicate_tests(self):
        manifest = qa.read_manifest(qa.DEFAULT_MANIFEST)
        mapped = [t for obj in manifest["capabilities"].values()
                  for t in obj["tests"]]
        self.assertGreaterEqual(len(manifest["capabilities"]), 15)
        self.assertEqual(len(mapped), len(set(mapped)))
        self.assertIn("northstarcpp_shared_assets", mapped)
        self.assertIn("northstarcpp_capability_swap", mapped)
        self.assertIn("northstarcpp_fps_ui", mapped)

    def test_failing_case_is_not_reported_as_success(self):
        summary = qa.summarize([
            qa.case("test_a", "ecs", "passed"),
            qa.case("test_b", "physics", "failed", details="Injected collision failure")
        ], {"ecs":{"tests":["test_a"]}, "physics":{"tests":["test_b"]}},
            ["test_a","test_b"], "full", ["test_a","test_b"], "run-1", self.root,
            "start","end",1.0,"reused")
        self.assertEqual("failed", summary["status"])
        self.assertEqual("failed", summary["capabilities"]["physics"]["status"])
        self.assertEqual("passed", summary["capabilities"]["ecs"]["status"])

    def test_unreported_native_test_fails_run(self):
        summary = qa.summarize([
            qa.case("test_a","ecs","passed")
        ], {"ecs":{"tests":["test_a","missing_test"]}},
           ["test_a","missing_test"],"full",["test_a","missing_test"],
           "run-2",self.root,"start","end",1.0,"reused")
        self.assertEqual("failed", summary["status"])
        self.assertEqual(["missing_test"], summary["unreported_tests"])

    def test_html_report_escapes_untrusted_output(self):
        report = self.root / "html"
        report.mkdir()
        summary = qa.summarize([
            qa.case("x<script>alert(1)</script>", "modules", "failed",
                    details="<img src=x onerror=alert(1)>")
        ], {"modules":{"tests":["x<script>alert(1)</script>"]}},
           ["x<script>alert(1)</script>"],"full",
           ["x<script>alert(1)</script>"],"run-3",self.root,
           "start","end",1.0,"reused")
        qa.write_html(report,summary,[])
        content = (report / "report.html").read_text("utf-8")
        self.assertNotIn("<script>alert(1)</script>",content)
        self.assertNotIn("<img src=x",content)
        self.assertIn("&lt;img",content)

    def test_junit_contains_failure_and_skipped(self):
        qa.write_junit(self.root, [
            qa.case("unit.test", "ecs", "passed",0.1),
            qa.case("fault.test", "physics", "failed",0.2, "fault"),
            qa.case("future.test", "renderer", "skipped",0.0, "not ready"),
        ],0.3)
        tree = ET.parse(self.root / "junit.xml")
        root = tree.getroot()
        self.assertEqual("3",root.get("tests"))
        self.assertEqual("1",root.get("failures"))
        self.assertEqual("1",root.get("skipped"))
        self.assertIsNotNone(root.find(".//failure"))

    def test_process_failure_has_real_exit_code_and_log(self):
        code, seconds, log, output = qa.command(
            [qa.sys.executable, "-c", "import sys; print('SENTINEL'); sys.exit(17)"],
            self.root, 15, self.logs, "expected-failure")
        self.assertEqual(17,code)
        self.assertIn("SENTINEL",output)
        self.assertTrue((self.logs/log).is_file())

    def test_report_path_sanitization_and_no_overwrite(self):
        with self.assertRaises(qa.QAConfigurationError):
            qa.safe_report_dir(self.root,"../escape")
        qa.safe_report_dir(self.root,"qa-test-123")
        with self.assertRaises(FileExistsError):
            qa.safe_report_dir(self.root,"qa-test-123")

    def test_ctest_junit_parser_keeps_errors(self):
        xml = self.root / "ctest.xml"
        suite = ET.Element("testsuite")
        failed = ET.SubElement(suite,"testcase",{"name":"native.failure","time":"0.35"})
        ET.SubElement(failed,"failure",{"message":"invalid ABI"}).text = "Bad physics provider"
        ET.SubElement(suite,"testcase",{"name":"native.success","time":"0.03"})
        ET.ElementTree(suite).write(xml,encoding="utf-8",xml_declaration=True)
        items = qa.parse_ctest_junit(xml,{"native.failure":"physics",
            "native.success":"ecs"},["native.failure","native.success"],self.logs)
        self.assertEqual(["failed","passed"],[x["status"] for x in items])
        self.assertIn("Bad physics provider",items[0]["details"])

    def test_manifest_rejects_duplicate_native_cases(self):
        config={"schema":1, "capabilities":{"a":{"tests":["one"]},
                "b":{"tests":["one"]}}}
        path=self.root/"invalid.json"
        path.write_text(json.dumps(config),encoding="utf-8")
        with self.assertRaises(qa.QAConfigurationError):
            qa.read_manifest(path)


if __name__ == "__main__":
    unittest.main()
