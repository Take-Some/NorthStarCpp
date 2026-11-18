"""Role-based NorthStarCpp 0.7.0 distribution regression tests."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.split_distribution import assemble, SplitError, verify_component
from tools.build_game import GameBuildError, selected_plugins

CAPABILITIES = (
    "ecs", "physics", "render", "audio", "input", "assets", "logger", "perlin",
)


def sha(blob: bytes) -> str:
    return hashlib.sha256(blob).hexdigest()


class SplitDistributionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / "verified-qa"
        self.source.mkdir()
        registry = {
            "engine": "NorthStarCpp", "engine_version": "0.7.0",
            "host_abi": 1,
            "plugins": [
                {"id": "test." + name, "capability": "engine." + name,
                 "provider": "jolt" if name == "physics" else "test",
                 "host_abi": 1, "default": True,
                 "kind": "external", "path": "bin/" + name + ".dll"}
                for name in CAPABILITIES
            ],
        }
        files = {
            "bin/northstarcpp_player.exe": b"test-player",
            "bin/bugtrap.exe": b"test-bugtrap",
            "bin/northstarcpp_editor_qt.exe": b"test-editor",
            "bin/qt_tools/northstarcpp_editor_qt.exe": b"test-qt",
            "bin/bootstrap.json": b"{}",
            "bin/archive/northstar_shared.nsp": b"test-nsp",
            "sdk/include/northstarcpp/math.hpp": b"// sdk",
            "documentation/README.md": b"docs",
            "licenses/jolt.txt": b"licence",
            "northstarcpp.plugins.json": json.dumps(registry).encode(),
            "native.runtime.json": json.dumps({
                "schema": 1, "runtime": "MinGW-w64", "files": {}
            }).encode(),
        }
        files.update({"bin/" + name + ".dll": b"provider-" + name.encode()
                      for name in CAPABILITIES})
        for relative, blob in files.items():
            dest = self.source / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(blob)
        manifest = {
            "schema_version": 1, "engine": "NorthStarCpp", "version": "0.7.0",
            "build_type": "Release", "target": "windows-x64", "host_abi": 1,
            "tests_passed": True, "installed_runtime_smoke": True,
            "installed_sdk_consumer_smoke": True, "plugins": registry["plugins"],
            "imports": {
                "bin/northstarcpp_player.exe": [
                    {"name": "ecs.dll", "status": "bundled"}
                ]
            },
            "files": {path: {"size": len(blob), "sha256": sha(blob)}
                      for path, blob in files.items()},
        }
        (self.source / "manifest.json").write_text(json.dumps(manifest))
        self.output = self.root / "components"

    def split(self):
        with patch("tools.build_dist.verify_manifest",
                   side_effect=lambda path: json.loads(
                       (path / "manifest.json").read_text())):
            return assemble(self.source, self.output, approved=False,
                            zip_packages=False)

    def test_distinct_runtime_and_sdk_roles(self):
        result = self.split()
        engine = Path(result["engine"])
        client = Path(result["game_client"])
        self.assertTrue((engine / "bin/northstarcpp_editor_qt.exe").is_file())
        self.assertTrue((engine / "sdk/include/northstarcpp/math.hpp").is_file())
        self.assertFalse((engine / "bin/northstarcpp_player.exe").exists())
        self.assertTrue((client / "bin/northstarcpp_player.exe").is_file())
        self.assertTrue((client / "bin/archive/northstar_shared.nsp").is_file())
        self.assertFalse((client / "sdk").exists())
        self.assertFalse((client / "bin/qt_tools").exists())
        self.assertEqual(verify_component(client)["version"], "0.7.0")

    def test_game_export_consumes_client_not_editor(self):
        self.split()
        info, providers = selected_plugins(self.output / "GameClient", "jolt")
        self.assertEqual(info["version"], "0.7.0")
        self.assertEqual(len(providers), len(CAPABILITIES))
        with self.assertRaises(GameBuildError):
            selected_plugins(self.output / "Engine", "jolt")

    def test_corrupted_client_is_rejected(self):
        self.split()
        (self.output / "GameClient/bin/ecs.dll").write_bytes(b"corrupted")
        with self.assertRaises(SplitError):
            verify_component(self.output / "GameClient")

    def test_unapproved_qa_cannot_claim_publication(self):
        self.split()
        for role in ("Engine", "GameClient"):
            data = verify_component(self.output / role)
            self.assertFalse(data["release_approved"])

    def test_independent_archives(self):
        from zipfile import ZipFile
        with patch("tools.build_dist.verify_manifest",
                   side_effect=lambda path: json.loads(
                       (path / "manifest.json").read_text())):
            result = assemble(self.source, self.output, approved=False,
                              zip_packages=True)
        self.assertEqual(len(result["archives"]), 2)
        for archive in result["archives"]:
            with ZipFile(archive) as zipped:
                self.assertIsNone(zipped.testzip())
                names = zipped.namelist()
                role = Path(archive).name.split("-")[0]
                self.assertIn(role + "/manifest.json", names)
                self.assertTrue(all(p.startswith(role + "/") for p in names))

    def test_registry_inconsistency_rejected(self):
        self.split()
        client = self.output / "GameClient"
        data = json.loads((client / "manifest.json").read_text())
        data["plugins"] = []
        (client / "manifest.json").write_text(json.dumps(data))
        with self.assertRaises(SplitError):
            verify_component(client)

    def test_no_overwrite_existing_package(self):
        self.split()
        with self.assertRaises(SplitError):
            self.split()


if __name__ == "__main__":
    unittest.main()
