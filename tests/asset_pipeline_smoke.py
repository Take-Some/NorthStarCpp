#!/usr/bin/env python3
"""Deterministic source registry, named materials, graph and NSP acceptance."""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from asset_pipeline import (PipelineError, canonical, prepare, publish_cook,
                            validate_inventory, verify_registry)
from pack_nsp import inventory, pack, verify
from pack_native_ymat import build_ymat


def xml_material(contents: str) -> bytes:
    xml = ('<YmatMaterialLibrary version="2" schema="northstarcpp.ymat.material_library.v2">'
           + contents + '</YmatMaterialLibrary>')
    return build_ymat(xml.encode("utf-8"))


class PipelineSmoke(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="northstar-pipeline-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "assets"
        (self.root / "materials").mkdir(parents=True)
        (self.root / "models").mkdir()
        (self.root / "configs").mkdir()
        (self.root / "models" / "vehicle.ydr").write_bytes(b"source-mesh")
        (self.root / "materials" / "vehicle.ymat").write_bytes(
            xml_material('<Material name="body"/><Material name="windows_glass"/>'))
        (self.root / "configs" / "car.json").write_text(
            '{"material":"materials/vehicle.ymat@windows_glass",'
            '"mesh":"models/vehicle.ydr"}', encoding="utf-8")

    def build(self, previous=None, **kwargs):
        return prepare(self.root, "KaylaNightCity", previous, **kwargs)

    def test_deterministic_and_dependencies(self):
        first = self.build()
        second = self.build(first)
        self.assertEqual(canonical(first), canonical(second))
        dep = first["assets"]["configs/car.json"]["dependencies"]
        self.assertEqual(dep, ["materials/vehicle.ymat@windows_glass", "models/vehicle.ydr"])
        self.assertEqual(len(first["assets"]["materials/vehicle.ymat"]["members"]), 2)
        self.assertNotEqual(first["assets"]["materials/vehicle.ymat"]["members"]["body"],
                            first["assets"]["materials/vehicle.ymat"]["members"]["windows_glass"])

        cache = self.base / "ddc"
        manifest = self.base / "cook.json"
        self.assertEqual(publish_cook(first, cache, manifest), (0, 3))
        self.assertEqual(publish_cook(first, cache, manifest), (3, 0))
        self.assertEqual(json.loads(manifest.read_text("utf-8")), first)
        source = inventory(self.root)
        validate_inventory(first, source)
        verify_registry(self.root, first)
        state = self.base / "asset_registry.json"
        state.write_bytes(canonical(first))
        dest1 = self.base / "first.nsp"
        dest2 = self.base / "second.nsp"
        pack(self.root, dest1, asset_registry=state)
        pack(self.root, dest2, asset_registry=state)
        self.assertEqual(dest1.read_bytes(), dest2.read_bytes())
        self.assertEqual(verify(dest1)[0], 3)

    def test_dependency_invalidation_and_stale_pack(self):
        first = self.build()
        old_key = first["assets"]["configs/car.json"]["cook_key"]
        (self.root / "models" / "vehicle.ydr").write_bytes(b"modified-mesh")
        changed = self.build(first)
        self.assertEqual(first["assets"]["models/vehicle.ydr"]["id"],
                         changed["assets"]["models/vehicle.ydr"]["id"])
        self.assertNotEqual(old_key, changed["assets"]["configs/car.json"]["cook_key"])
        with self.assertRaises(PipelineError):
            validate_inventory(first, inventory(self.root))
        with self.assertRaises(PipelineError):
            verify_registry(self.root, first)
        self.assertEqual(publish_cook(changed, self.base / "ddc", self.base / "manifest.json"),
                         (0, 3))

    def test_explicit_moves_preserve_identity(self):
        first = self.build()
        src = self.root / "models" / "vehicle.ydr"
        dst = self.root / "models" / "car.ydr"
        src.rename(dst)
        (self.root / "configs" / "car.json").write_text(
            '{"material":"materials/vehicle.ymat@windows_glass",'
            '"mesh":"models/car.ydr"}', encoding="utf-8")
        with self.assertRaisesRegex(PipelineError, "require --move"):
            self.build(first)
        moved = self.build(first, moves=["models/vehicle.ydr=models/car.ydr"])
        self.assertEqual(first["assets"]["models/vehicle.ydr"]["id"],
                         moved["assets"]["models/car.ydr"]["id"])

    def test_fail_closed_material_identity(self):
        before = self.build()
        material = self.root / "materials" / "vehicle.ymat"
        material.write_bytes(xml_material('<Material name="mat_0"/>'))
        with self.assertRaisesRegex(PipelineError, "anonymous"):
            self.build()
        material.write_bytes(xml_material('<Material name="windows_glass"/>'))
        with self.assertRaisesRegex(PipelineError, "identity changed"):
            self.build(before)

    def test_material_slot_order_and_shader_includes(self):
        before = self.build()
        (self.root / "materials" / "vehicle.ymat").write_bytes(
            xml_material('<Material name="windows_glass"/><Material name="body"/>'))
        with self.assertRaisesRegex(PipelineError, "slot order"):
            self.build(before)
        (self.root / "materials" / "vehicle.ymat").write_bytes(
            xml_material('<Material name="body"/><Material name="windows_glass"/>'))
        shaders = self.root / "shaders"
        shaders.mkdir()
        (shaders / "common.inc").write_text("#define SCALE 1", encoding="utf-8")
        (shaders / "main.hlsl").write_text('#include "common.inc"', encoding="utf-8")
        graph = self.build(before)
        self.assertEqual(graph["assets"]["shaders/main.hlsl"]["dependencies"],
                         ["shaders/common.inc"])
        old_key = graph["assets"]["shaders/main.hlsl"]["cook_key"]
        (shaders / "common.inc").write_text("#define SCALE 2", encoding="utf-8")
        self.assertNotEqual(old_key, self.build(graph)["assets"]["shaders/main.hlsl"]["cook_key"])

    def test_reject_kind30_json_material(self):
        source = self.root / "materials" / "vehicle.ymat"
        data = bytearray(source.read_bytes())
        data[8:10] = (30).to_bytes(2, "little")
        source.write_bytes(data)
        with self.assertRaisesRegex(PipelineError, "kind 30"):
            self.build()

    def test_missing_named_member_and_cycle(self):
        (self.root / "configs" / "car.json").write_text(
            '{"mat":"materials/vehicle.ymat@nonexistent"}', encoding="utf-8")
        with self.assertRaisesRegex(PipelineError, "Missing named YMAT"):
            self.build()
        (self.root / "configs" / "car.json").write_text(
            '{"config":"configs/other.json"}', encoding="utf-8")
        (self.root / "configs" / "other.json").write_text(
            '{"config":"configs/car.json"}', encoding="utf-8")
        with self.assertRaisesRegex(PipelineError, "cycle"):
            self.build()


if __name__ == "__main__":
    unittest.main(verbosity=2)
