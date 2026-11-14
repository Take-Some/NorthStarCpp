# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026

"""Headless tests of the NorthStarCpp Launcher backend."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from tools.dev.launcher import (
    LauncherError, ProjectManager, runtime_state, relative_path, status
)


class LauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manager = ProjectManager(self.root)
        self.cfg = {"default_build_type": "Debug", "build_root": "out",
                    "runtime": {"executable": "", "required_paths": ["resources", "mounts"]}}

    def test_project_creation(self):
        item = self.manager.create("My First Game")
        self.assertTrue((item.directory / "northstarcpp.project.json").exists())
        self.assertTrue((item.directory / "assets").is_dir())
        self.assertTrue((item.directory / "scripts").is_dir())
        self.assertTrue((item.directory / "main.nscn").read_bytes().startswith(b"NEF8"))
        self.assertEqual("NorthStarCpp", json.loads(
            (item.directory / "northstarcpp.project.json").read_text(encoding="utf-8")
        )["engine"])
        self.assertEqual(["My First Game"], [project.name for project in self.manager.list()])

    def test_projects_never_overwritten(self):
        self.manager.create("Sample")
        with self.assertRaises(LauncherError):
            self.manager.create("Sample")

    def test_unsafe_name_rejected(self):
        for name in ("..", "../outside", "A/B", "CON", "LPT1", "bad?", ""):
            with self.subTest(name=name), self.assertRaises(LauncherError):
                self.manager.create(name)

    def test_runtime_missing_not_reported_ready(self):
        state = runtime_state(self.root, self.cfg)
        self.assertFalse(state.ready)
        self.assertIn("not compiled", state.reason)

    def test_fake_development_exe_not_runtime(self):
        exe = self.root / "out/gcc-debug/northstarcpp_portability_smoke.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"placeholder")
        cfg = {"runtime": {"executable": "out/gcc-debug/northstarcpp_portability_smoke.exe"}}
        self.assertFalse(runtime_state(self.root, cfg).ready)

    def test_assets_missing_prohibits_launch(self):
        exe = self.root / "out/gcc-debug/NorthStarCpp.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"placeholder")
        cfg = {"runtime": {"executable": "out/gcc-debug/NorthStarCpp.exe",
                           "required_paths": ["resources", "mounts"]}}
        state = runtime_state(self.root, cfg)
        self.assertFalse(state.ready)
        self.assertIn("Required runtime files missing", state.reason)

    def test_repository_traversal_rejected(self):
        with self.assertRaises(LauncherError):
            relative_path(self.root, "../outside.exe", "runtime")

    def test_status_is_machine_readable(self):
        data = status(self.root, self.cfg)
        self.assertEqual("NorthStarCpp", data["engine"])
        self.assertFalse(data["runtime_ready"])
        self.assertEqual(0, data["projects"])


if __name__ == "__main__":
    unittest.main()
