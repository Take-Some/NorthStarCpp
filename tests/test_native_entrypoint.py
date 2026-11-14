# NorthStarCpp
# Author: Kayla Verner
# Updated: 2026
"""Regression tests for the native C++ GUI entry point.

Build management can use Python; it must never open the retired Python GUI.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
NATIVE_EXE = "northstarcpp_project_manager.exe"


class NativeEntryPointTests(unittest.TestCase):
    def test_retired_python_gui_sources_are_absent(self):
        self.assertFalse((ROOT / "tools/dev/project_browser.py").exists())
        self.assertFalse((ROOT / "tools/dev/gui.py").exists())

    def test_default_cmd_selects_native_start_script(self):
        text = (ROOT / "northstarcpp.cmd").read_text(encoding="utf-8")
        self.assertIn("tools\\dev\\start.cmd", text)
        self.assertNotIn("tools.dev.launcher", text)

    def test_native_batch_never_launches_python_gui(self):
        text = (ROOT / "tools/dev/start.cmd").read_text(encoding="utf-8")
        self.assertIn(NATIVE_EXE, text)
        self.assertNotIn("pythonw", text.casefold())
        self.assertNotIn("tools.dev.launcher", text)
        self.assertNotIn("tools.dev.project_browser", text)

    def test_no_python_gui_in_cli_dispatch(self):
        for file in ("tools/dev/cli.py", "tools/dev/launcher.py"):
            with self.subTest(file=file):
                source = (ROOT / file).read_text(encoding="utf-8")
                self.assertNotIn("from .project_browser import", source)
                self.assertNotIn("from .gui import", source)
                self.assertIn("open_native_project_manager", source)

    @unittest.skipUnless(os.name == "nt", "Native Windows manager required")
    def test_native_launch_readiness_does_not_start_gui(self):
        entry = ROOT / "tools/dev/start.cmd"
        proc = subprocess.run(
            ["cmd", "/d", "/c", str(entry), "--check"],
            cwd=ROOT, capture_output=True, text=True, errors="replace", timeout=15
        )
        self.assertIn(proc.returncode, (0, 1), proc.stderr)
        self.assertIn("NorthStarCpp", proc.stdout)
        self.assertNotIn("Tkinter", proc.stdout)


if __name__ == "__main__":
    unittest.main()
