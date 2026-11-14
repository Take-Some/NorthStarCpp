# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026

"""Regression tests for the public NorthStarCpp name."""
import re
import subprocess
import sys
import unittest
from tools.dev.cli import ROOT

PUBLIC = (
    "README.md",
    "docs/START_HERE.md",
    "docs/GCC_DEVELOPMENT_SHELL.md",
    "CMakeLists.txt",
    "tools/dev/cli.py",
    "tools/dev/launcher.py",
    "config/northstarcpp.json",
    "tests/northstarcpp_portability_smoke.cpp",
    "engine/northstarcpp/modules/core/northstarcpp/macro_extensions.h",
    "engine/northstarcpp/modules/core/northstarcpp/editor/world/sources/about_dialog.cpp",
    "engine/northstarcpp/modules/core/northstarcpp/editor/world/sources/about_dialog.h",
)
FORBIDDEN = re.compile(r"x[-_]?ray(?:[\\s_-]?2)?", re.IGNORECASE)


class BrandingTests(unittest.TestCase):
    def test_public_branding(self):
        for name in PUBLIC:
            with self.subTest(file=name):
                text = (ROOT / name).read_text(encoding="utf-8", errors="replace")
                self.assertIsNone(FORBIDDEN.search(text))

    def test_full_source_tree_has_no_old_identifiers(self):
        checker = ROOT / "tools/verify_northstarcpp.py"
        result = subprocess.run([sys.executable, str(checker), "--json"],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_cmake_project_name(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("project(NorthStarCpp", cmake)
        self.assertIn("northstarcpp_math", cmake)

    def test_northstarcpp_owned_headers(self):
        from datetime import date
        current_year = str(date.today().year)
        for name in (
            "engine/northstarcpp/modules/host/audio.cpp",
            "engine/northstarcpp/modules/host/ecs_world.cpp",
            "engine/northstarcpp/modules/physics/bullet/bullet.cpp",
            "engine/northstarcpp/modules/api/include/northstarcpp/ecs_api.h",
            "tools/dev/launcher.py",
        ):
            with self.subTest(file=name):
                source = (ROOT / name).read_text(encoding="utf-8")
                self.assertIn("Author: Kayla Verner", source[:250])
                self.assertIn("Updated: " + current_year, source[:250])

    def test_current_company_metadata(self):
        expected = r"2026 Kayla's Systems \u2014 Kayla Verner"
        sources = (
            "engine/northstarcpp/modules/core/northstarcpp/macro_extensions.h",
            "engine/northstarcpp/modules/core/northstarcpp/editor/controls/sources/AssemblyInfo.cpp",
            "engine/northstarcpp/modules/core/northstarcpp/maya/sources/maya_entry_point.cpp",
        )
        for name in sources:
            with self.subTest(file=name):
                source = (ROOT / name).read_text(encoding="utf-8", errors="replace")
                self.assertIn(expected, source)

    def test_original_copyright_is_preserved(self):
        about = (ROOT / "engine/northstarcpp/modules/core/northstarcpp/editor/world/sources/about_dialog.cpp").read_text(
            encoding="utf-8", errors="replace")
        self.assertIn("Kayla's Systems", about)
        self.assertIn("NorthStarCpp Editor", about)


if __name__ == "__main__":
    unittest.main()
