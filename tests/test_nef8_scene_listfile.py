"""NEF8 scene envelope, typed component table and packaging safety regressions."""
import struct
from pathlib import Path
import unittest
import zlib
from blake3 import blake3
from tools.dev.scene_binary import (
    encode_legacy_template, pack_listfile, validate_nef8_scene,
    read_nef8_scene_records, scene_entity_references,
)


class SceneListFileTests(unittest.TestCase):
    @staticmethod
    def template():
        return ('NorthStarCpp.Scene 2\n'
                'cube "Player" 0 0 0 1.5 46b9d8 1 2\n'
                'cube "Block" 2 1 0 1 f2a65a 0 0\n')

    def test_nef8_kind_and_determinism(self):
        first = encode_legacy_template(self.template())
        self.assertTrue(first.startswith(b"NEF8"))
        self.assertEqual(first, encode_legacy_template(self.template()))
        self.assertEqual(validate_nef8_scene(first), 3)
        kinds = [entry[0] for entry in read_nef8_scene_records(first)]
        self.assertEqual(kinds, [1, 2, 2])
        self.assertEqual(scene_entity_references(first), [("", ""), ("", "")])
        self.assertLess(len(first), 1024)

    def test_outer_corruption_rejected(self):
        data = encode_legacy_template(self.template())
        for method in (
            lambda b: b[:-1],
            lambda b: b + b"x",
            lambda b: b[:64] + bytes([b[64] ^ 1]) + b[65:],
            lambda b: b[:8] + b"\x02" + b[9:],
            lambda b: b[:32] + b"\xff"*8 + b[40:],
        ):
            with self.subTest(method=method), self.assertRaises(ValueError):
                validate_nef8_scene(method(data))

    def test_wrong_entry_crc_and_unknown_component_rejected(self):
        original = encode_legacy_template(self.template())
        payload_offset = struct.unpack_from("<Q", original, 16)[0]
        table = bytearray(original[128:payload_offset])
        # The first chunk index entry has the compressed-stream CRC at 16+56.
        struct.pack_into("<I", table, 16 + 56, 0)
        header = bytearray(original[:128])
        header[96:112] = blake3(table).digest()[:16]
        with self.assertRaisesRegex(ValueError, "CRC"):
            validate_nef8_scene(bytes(header) + bytes(table) + original[payload_offset:])
        # Future component kinds are intentionally preserved as opaque data.
        future=pack_listfile([(1,0,b"x"),(65535,0,b"y")])
        self.assertEqual(validate_nef8_scene(future),2)
        self.assertEqual(read_nef8_scene_records(future)[1],(65535,0,b"y"))

    def test_independent_cell_chunks(self):
        from tools.dev.scene_binary_v2 import cell_records, _chunks
        doc = (1, 0, b"document")
        cell_a = (2, 100, b"entity-A")
        cell_b = (2, 200, b"entity-B")
        data = pack_listfile([doc, cell_a, cell_b],
                             entity_cells={100:"A",200:"B"},
                             cell_order=["A","B"])
        self.assertEqual(validate_nef8_scene(data),3)
        chunks, count = _chunks(data)
        self.assertEqual([c[0] for c in chunks],["","A","B"])
        self.assertIn(b"entity-A",cell_records(data,"A"))
        self.assertIn(b"entity-B",cell_records(data,"B"))
        self.assertIsNone(cell_records(data,"unallocated"))
        damaged = bytearray(data)
        damaged[chunks[2][2]] ^= 0x7f
        # A is genuinely independent of B's compressed bytes.
        self.assertIn(b"entity-A",cell_records(bytes(damaged),"A"))
        with self.assertRaises(ValueError):
            cell_records(bytes(damaged),"B")
        with self.assertRaises(ValueError):
            validate_nef8_scene(bytes(damaged))

    def test_legacy_nef8_v1_still_readable(self):
        from tools.dev.scene_binary import _pack_listfile_v1
        source = _pack_listfile_v1([(1,0,b"doc"),(2,1,b"entity")])
        self.assertEqual(validate_nef8_scene(source),2)
        self.assertEqual(read_nef8_scene_records(source)[1],(2,1,b"entity"))

    def test_scene_record_asset_and_script_refs(self):
        data = encode_legacy_template(self.template())
        records = read_nef8_scene_records(data)
        kind, stable_id, original = records[1]
        self.assertEqual(kind, 2)
        # Rebuild the fixture's entity string fields as if the editor assigned
        # a mesh asset and JavaScript component.
        name_end = 2 + struct.unpack_from("<H", original)[0]
        prefix = original[:name_end + 41]
        suffix = original[name_end + 41:]
        cursor = 0
        for _ in range(3):
            size = struct.unpack_from("<H", suffix, cursor)[0]
            cursor += 2 + size
        # This is the old empty asset+script prefix, each encoded as u16 zero.
        self.assertEqual(suffix[cursor:cursor+4], b"\x00"*4)
        asset=b"Meshes/prop.obj"
        script=b"scripts/main.js"
        patched = prefix + suffix[:cursor] + (
            struct.pack("<H", len(asset)) + asset +
            struct.pack("<H", len(script)) + script + suffix[cursor+4:])
        records[1] = (kind, stable_id, patched)
        encoded = pack_listfile(records)
        self.assertEqual(validate_nef8_scene(encoded), 3)
        self.assertEqual(scene_entity_references(encoded)[0],
                         ("Meshes/prop.obj", "scripts/main.js"))
        from tempfile import TemporaryDirectory
        from tools.build_game import scene_obj_refs, scene_script_refs
        with TemporaryDirectory(prefix="nefstream-refs-") as tmp:
            source = Path(tmp) / "scene.nscn"
            source.write_bytes(encoded)
            self.assertEqual(scene_obj_refs(source), {"assets/Meshes/prop.obj"})
            self.assertEqual(scene_script_refs(source), {"scripts/main.js"})


if __name__ == "__main__":
    unittest.main()
