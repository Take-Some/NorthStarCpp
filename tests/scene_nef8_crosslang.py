"""Byte-for-byte NEF8 scene C++/Python codec regression."""
from __future__ import annotations
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.dev.scene_binary import (
    encode_legacy_template, validate_nef8_scene, read_nef8_scene_records,
    _pack_listfile_v1
)


def main() -> int:
    cli = argparse.ArgumentParser()
    cli.add_argument("--converter", type=Path, required=True)
    cli.add_argument("--source", type=Path, required=True)
    args = cli.parse_args()
    python_data = encode_legacy_template(args.source.read_text(encoding="utf-8"))
    assert python_data.startswith(b"NEF8")
    count = validate_nef8_scene(python_data)
    with tempfile.TemporaryDirectory(prefix="ns-nef8-listfile-") as temporary:
        src = Path(temporary) / "python.nscn"
        dst = Path(temporary) / "native.nscn"
        src.write_bytes(python_data)
        command = [str(args.converter.resolve()), str(src), str(dst)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(f"Native Scene ListFile conversion failed: {result.stderr}")
        native_data = dst.read_bytes()
        if validate_nef8_scene(native_data) != count or native_data != python_data:
            raise AssertionError("C++/Python NEF8 encoding is not byte-for-byte stable")
        # Old NEF8 kind-6 schema v1 must remain readable and migrate into
        # the new chunked schema v2 on the next save.
        old_source = Path(temporary) / "old-nef8-v1.nscn"
        migrated = Path(temporary) / "migrated-nef8-v2.nscn"
        old_source.write_bytes(_pack_listfile_v1(read_nef8_scene_records(python_data)))
        result = subprocess.run(
            [str(args.converter.resolve()), str(old_source), str(migrated)],
            capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(f"Native NEF8 v1 migration failed: {result.stderr}")
        if (validate_nef8_scene(migrated.read_bytes()) != count or
            migrated.read_bytes() != python_data):
            raise AssertionError("NEF8 v1 to v2 migration changed scene records")
    print(f"NEF8 Scene ListFile Python/C++ roundtrip: PASS ({count} records)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
