"""Qt runtime packaging contract tests, independent of an installed Qt SDK."""
import hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
import json
import unittest

from tools.deploy_qt import QtDeploymentError, verified_extras, isolate_qt_runtime


class QtRuntimeValidationTests(unittest.TestCase):
    def setUp(self):
        self.stage = TemporaryDirectory(prefix="northstar-qt-validator-")
        self.addCleanup(self.stage.cleanup)
        self.root = Path(self.stage.name)
        files = {}
        for name in ("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll"):
            files["bin/" + name] = b"mock-" + name.encode("ascii")
        files["bin/platforms/qwindows.dll"] = b"mock-platform"
        for rel, data in files.items():
            target = self.root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        self.files = {
            rel: {"size": len(value), "sha256": hashlib.sha256(value).hexdigest()}
            for rel, value in files.items()}
        for notice in ("licenses/FontAwesome/LICENSE.txt",
                       "licenses/Qt6Base/LGPL-3.0-only.txt"):
            file = self.root / notice
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("License retained", encoding="utf-8")
        self.write_registry()

    def write_registry(self):
        (self.root / "qt.runtime.json").write_text(json.dumps({
            "schema": 1, "runtime": "Qt6Widgets", "dlls": self.files
        }), encoding="utf-8")

    def test_qt_isolation_preserves_private_runtime_and_manifest(self):
        tools = ("northstarcpp_project_manager.exe",
                 "northstarcpp_asset_browser.exe",
                 "northstarcpp_editor_qt.exe",
                 "northstarcpp_camera_timeline_editor.exe")
        launcher_dir = self.root / "tools" / "qt_launchers"
        launcher_dir.mkdir(parents=True)
        for name in tools:
            (self.root / "bin" / name).write_bytes(b"qt-tool-" + name.encode())
            launcher = launcher_dir / (Path(name).stem + "_launcher.exe")
            launcher.write_bytes(b"native-launcher-" + name.encode())
        concurrent = b"qt-concurrent"
        (self.root / "bin/Qt6Concurrent.dll").write_bytes(concurrent)
        self.files["bin/Qt6Concurrent.dll"] = {
            "size": len(concurrent),
            "sha256": hashlib.sha256(concurrent).hexdigest()}
        # A native engine DLL must remain completely untouched.
        native = self.root / "bin/physics.dll"
        native.write_bytes(b"native-engine-physics")
        self.write_registry()
        record = isolate_qt_runtime(self.root)
        self.assertEqual(record["layout"], "process-isolated")
        self.assertEqual(verified_extras(self.root), set())
        self.assertEqual(native.read_bytes(), b"native-engine-physics")
        self.assertFalse((self.root / "bin/Qt6Core.dll").exists())
        for name in tools:
            self.assertTrue((self.root / "bin/qt_tools" / name).exists())
            self.assertTrue((self.root / "bin" / name).exists())
            self.assertEqual((self.root / "bin" / name).read_bytes(),
                             b"native-launcher-" + name.encode())
        self.assertEqual(len(record["launchers"]), 4)
        self.assertTrue((self.root / "bin/qt_tools/platforms/qwindows.dll").is_file())

    def test_qt_isolation_refuses_missing_launcher_without_mutation(self):
        name = "northstarcpp_project_manager.exe"
        (self.root / "bin" / name).write_bytes(b"real-qt")
        with self.assertRaisesRegex(QtDeploymentError, "launcher"):
            isolate_qt_runtime(self.root)
        self.assertFalse((self.root / "bin/qt_tools").exists())
        self.assertTrue((self.root / "bin" / name).is_file())

    def test_isolated_launcher_tampering_is_detected(self):
        name = "northstarcpp_project_manager.exe"
        (self.root / "bin" / name).write_bytes(b"real-qt")
        launcher_dir = self.root / "tools" / "qt_launchers"
        launcher_dir.mkdir(parents=True)
        (launcher_dir / "northstarcpp_project_manager_launcher.exe").write_bytes(
            b"windows-launcher")
        isolate_qt_runtime(self.root)
        (self.root / "bin" / name).write_bytes(b"modified")
        with self.assertRaises(QtDeploymentError):
            verified_extras(self.root)

    def test_qt_isolation_refuses_tampered_input_without_mutation(self):
        (self.root / "bin/northstarcpp_project_manager.exe").write_bytes(b"fixture")
        (self.root / "bin/Qt6Core.dll").write_bytes(b"unexpected")
        with self.assertRaises(QtDeploymentError):
            isolate_qt_runtime(self.root)
        self.assertTrue((self.root / "bin/northstarcpp_project_manager.exe").exists())
        self.assertFalse((self.root / "bin/qt_tools").exists())

    def test_runtime_manifest(self):
        self.assertEqual(verified_extras(self.root),
                         {"qt6core.dll", "qt6gui.dll", "qt6widgets.dll"})

    def test_rejects_changed_qt_binary(self):
        (self.root / "bin/Qt6Core.dll").write_bytes(b"tampered")
        with self.assertRaises(QtDeploymentError):
            verified_extras(self.root)

    def test_rejects_missing_platform_plugin(self):
        (self.root / "bin/platforms/qwindows.dll").unlink()
        with self.assertRaises(QtDeploymentError):
            verified_extras(self.root)

    def test_rejects_missing_license(self):
        (self.root / "licenses/FontAwesome/LICENSE.txt").unlink()
        with self.assertRaises(QtDeploymentError):
            verified_extras(self.root)

    def test_rejects_unsafe_path(self):
        self.files["bin/../escape.dll"] = {"size": 0, "sha256": ""}
        self.write_registry()
        with self.assertRaises(QtDeploymentError):
            verified_extras(self.root)


if __name__ == "__main__":
    unittest.main()
