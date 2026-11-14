"""Tests for optional security-gated V8 engine distribution registration."""
from __future__ import annotations

import json
from pathlib import Path
import tempfile
import unittest

from tools.v8_provider.registry import (
    registered_v8_runtime, stage_v8_provider, v8_version,
)


class V8DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="northstar-v8-registry-")
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.engine = root / "engine"
        self.bin = self.engine / "bin"
        self.bin.mkdir(parents=True)
        self.provider = root / "staged-v8" / "bin"
        self.provider.mkdir(parents=True)
        self.sdk = root / "sdk"
        (self.sdk / "lib/pkgconfig").mkdir(parents=True)
        (self.sdk / "share/licenses/v8").mkdir(parents=True)
        (self.sdk / "share/licenses/v8/LICENSE").write_text(
            "Synthetic license fixture - not for redistribution\n")
        for relative in ("share/licenses/gcc-libs/COPYING.RUNTIME",
                         "share/licenses/libwinpthread/COPYING",
                         "share/licenses/zlib/LICENSE",
                         "share/icu/78.1/LICENSE"):
            file = self.sdk / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text("Synthetic license fixture\n", encoding="utf-8")
        (self.sdk / "lib/pkgconfig/v8.pc").write_text(
            "Name: V8\nVersion: 15.3.48\n", encoding="utf-8")
        (self.sdk / "bin").mkdir(parents=True)
        for filename in ("scripting_v8.dll", "libv8.dll", "libv8_libbase.dll",
                         "libv8_libplatform.dll", "libicuuc78.dll"):
            (self.provider / filename).write_bytes(filename.encode("ascii"))
            if filename != "scripting_v8.dll":
                (self.sdk / "bin" / filename).write_bytes(filename.encode("ascii"))
        (self.engine / "northstarcpp.plugins.json").write_text(json.dumps({
            "engine": "NorthStarCpp", "host_abi": 1,
            "engine_version": "1.0.0", "plugins": [],
        }), encoding="utf-8")

    def test_register_and_verify_v8_dependencies_and_licenses(self):
        record = stage_v8_provider(self.engine, self.provider, self.sdk, version_probe=lambda _: (15, 3, 48))
        self.assertEqual(record["v8_version"], "15.3.48")
        self.assertEqual(registered_v8_runtime(self.engine),
                         {"bin/libv8.dll", "bin/libv8_libbase.dll",
                          "bin/libv8_libplatform.dll", "bin/libicuuc78.dll"})
        registry = json.loads((self.engine / "northstarcpp.plugins.json").read_text())
        plugin = registry["plugins"][0]
        self.assertEqual(plugin["capability"], "engine.scripting")
        self.assertTrue(plugin["default"])
        self.assertEqual(plugin["path"], "bin/scripting_v8.dll")
        (self.engine / "bin/libv8.dll").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "tampered"):
            registered_v8_runtime(self.engine)

    def test_refuses_outdated_v8(self):
        (self.sdk / "lib/pkgconfig/v8.pc").write_text("Version: 14.6.202\n")
        with self.assertRaisesRegex(ValueError, "below reviewed minimum"):
            stage_v8_provider(self.engine, self.provider, self.sdk, version_probe=lambda _: (14, 6, 202))
        self.assertFalse((self.engine / "bin/scripting_v8.dll").exists())

    def test_refuses_crt_collision(self):
        (self.engine / "bin/libv8.dll").write_bytes(b"incompatible")
        with self.assertRaisesRegex(ValueError, "native DLL collision"):
            stage_v8_provider(self.engine, self.provider, self.sdk, version_probe=lambda _: (15, 3, 48))
        self.assertFalse((self.engine / "bin/scripting_v8.dll").exists())
        self.assertFalse((self.engine / "bin/libv8_libbase.dll").exists())

    def test_sdk_version_provenance_rejected(self):
        with self.assertRaisesRegex(ValueError, "metadata.*disagrees"):
            stage_v8_provider(
                self.engine, self.provider, self.sdk,
                version_probe=lambda _: (14, 0, 210))

    def test_sdk_dll_binary_mismatch_rejected(self):
        (self.sdk / "bin" / "libv8.dll").write_bytes(b"wrong version")
        with self.assertRaisesRegex(ValueError, "binary mismatch"):
            stage_v8_provider(
                self.engine, self.provider, self.sdk,
                version_probe=lambda _: (15, 3, 48))
        self.assertFalse((self.engine / "bin/scripting_v8.dll").exists())

    def test_missing_license_rejected(self):
        (self.sdk / "share/licenses/v8/LICENSE").unlink()
        with self.assertRaisesRegex(ValueError, "license"):
            stage_v8_provider(self.engine, self.provider, self.sdk, version_probe=lambda _: (15, 3, 48))


class V8RuntimeCollisionAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="northstar-v8-qt-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.qt = self.root / "qt"
        self.v8 = self.root / "v8"
        self.qt.mkdir()
        self.v8.mkdir()
        (self.qt / "Qt6Core.dll").write_bytes(b"Qt")
        (self.v8 / "libv8.dll").write_bytes(b"V8")
        (self.v8 / "scripting_v8.dll").write_bytes(b"provider")

    def test_conflicting_mingw_and_ucrt_runtime_dlls(self):
        from tools.v8_provider.runtime_audit import audit_runtime
        (self.qt / "libstdc++-6.dll").write_bytes(b"mingw-msvcrt")
        (self.v8 / "libstdc++-6.dll").write_bytes(b"ucrt")
        result = audit_runtime(self.qt, self.v8)
        self.assertFalse(result["safe_to_merge"])
        self.assertEqual([item["dll"] for item in result["conflicts"]],
                         ["libstdc++-6.dll"])

    def test_matching_runtime_dlls(self):
        from tools.v8_provider.runtime_audit import audit_runtime
        (self.qt / "libstdc++-6.dll").write_bytes(b"same binary")
        (self.v8 / "libstdc++-6.dll").write_bytes(b"same binary")
        result = audit_runtime(self.qt, self.v8)
        self.assertTrue(result["safe_to_merge"])
        self.assertEqual(result["conflicts"], [])


if __name__ == "__main__":
    unittest.main()
