"""One CMake binary tree, source isolation and consistent packaging paths."""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess
import unittest

from tools.build_layout import (
    BUILD_DIR, OUTPUT_ROOT, SOURCE_ROOT, LOG_DIR, QA_DIR,
    QA_PACKAGE_DIR, RELEASE_PACKAGE_DIR, GAME_PACKAGE_DIR,
    canonical_build_path, require_canonical_build, require_inside_output,
)
from tools.dev.cli import build_path

ROOT = Path(__file__).resolve().parents[1]


class SingleBuildLayoutTests(unittest.TestCase):
    def test_one_binary_directory_across_configs(self):
        config = json.loads((ROOT / "config/northstarcpp.json").read_text("utf-8"))
        self.assertEqual(config["build_root"], "out")
        for kind in ("Debug", "Release", "RelWithDebInfo"):
            self.assertEqual(canonical_build_path(kind), BUILD_DIR)
            self.assertEqual(build_path(config, kind), BUILD_DIR)
        self.assertEqual(SOURCE_ROOT, ROOT / "engine/northstarcpp/modules")
        self.assertNotEqual(SOURCE_ROOT, BUILD_DIR)
        self.assertNotIn(SOURCE_ROOT, BUILD_DIR.parents)

    def test_no_other_cmake_trees(self):
        require_canonical_build(BUILD_DIR)
        for candidate in (ROOT / "out/gcc-debug", ROOT / "out/project-manager-qt",
                          ROOT / "out/distribution/gcc-release", SOURCE_ROOT):
            with self.assertRaises(ValueError):
                require_canonical_build(candidate)

    def test_package_logs_and_qa_inside_output(self):
        for target in (BUILD_DIR, LOG_DIR, QA_DIR, QA_PACKAGE_DIR,
                       RELEASE_PACKAGE_DIR, GAME_PACKAGE_DIR):
            self.assertIn(OUTPUT_ROOT, target.parents)
            self.assertEqual(require_inside_output(target), target.resolve())
        with self.assertRaises(ValueError):
            require_inside_output(ROOT / "dist")

    def test_presets_share_one_binary_dir(self):
        data = json.loads((ROOT / "CMakePresets.json").read_text("utf-8"))
        presets = data["configurePresets"]
        base = next(item for item in presets if item["name"] == "base")
        self.assertEqual(base["binaryDir"], "${sourceDir}/out/build")
        self.assertTrue(all(item.get("inherits") == "base" for item in presets if not item.get("hidden")))

    @unittest.skipUnless(shutil.which("cmake"), "CMake unavailable")
    def test_cmake_rejects_legacy_binary_directory(self):
        cmake = shutil.which("cmake")
        base = [cmake, f"-DROOT={ROOT}", "-P", str(ROOT / "tests/build_layout_smoke.cmake")]
        good = subprocess.run(base, text=True, capture_output=True, timeout=20)
        self.assertEqual(good.returncode, 0, good.stderr)
        bad = subprocess.run(
            [cmake, f"-DROOT={ROOT}", "-DTEST_INVALID=ON",
             "-P", str(ROOT / "tests/build_layout_smoke.cmake")],
            text=True, capture_output=True, timeout=20)
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("exactly one build directory", bad.stderr)


if __name__ == "__main__":
    unittest.main()
