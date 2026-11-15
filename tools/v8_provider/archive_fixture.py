"""Generate a deterministic FAT/DB test archive with a scene and project JS.

This reproduces the asset layout supported by NorthStarCpp's FAT/DB VFS.
Used only for scripting integration regression testing.
"""
from __future__ import annotations

from pathlib import Path
import sys

HEADER = 24
BODY = 512


def generate(path: Path, scene_override: bytes | None = None,
             script_override: bytes | None = None,
             camera_override: bytes | None = None,
             config_override: bytes | None = None,
             prefab_override: bytes | None = None) -> None:
    script = (
        b"engine.on('start',()=>engine.logger.info('archive loaded'));\n"
        b"3+4\n"
    )
    scene = b'NorthStarCpp.Scene 2\ncube "Box" 0 0 0 1 46b9d8 0 0\n'
    if scene_override is not None:
        scene = scene_override
    if script_override is not None:
        script = script_override
    scene_name = "main.nscn" if scene[:4] == b"NEF8" else "main.nscene"
    data = bytearray(HEADER + (640 if prefab_override is not None else BODY))
    data[:14] = b"little-endian"
    def put(offset: int, value: int, width: int) -> None:
        data[offset:offset + width] = value.to_bytes(width, "little")
    def field(offset: int, value: int, width: int) -> None:
        put(HEADER + offset, value, width)
    def node(offset: int, parent: int, previous: int,
             following: int, flags: int, name: str) -> None:
        field(offset + 8, following, 8)
        field(offset + 16, previous, 8)
        field(offset + 24, parent, 8)
        field(offset + 32, 0x2000 | flags, 2)
        encoded = name.encode("utf-8")
        data[HEADER + offset + 34:HEADER + offset + 34 + len(encoded)] = encoded

    has_camera = camera_override is not None
    has_config = config_override is not None
    put(16, 5 + int(has_camera) + int(has_config) +
        (2 if prefab_override is not None else 0), 4)
    put(20, 640 if prefab_override is not None else BODY, 4)
    # Shared VFS names for project root script overrides.
    field(0, 64, 8)
    node(8, 0, 0, 0, 0x8000, "")
    field(64 - 8, 128, 8)
    node(64, 8, 0, 256, 0x8000, "scripts")
    field(128 - 16, 2048, 8)
    field(128 - 8, len(script), 4)
    node(128, 64, 0, 384 if has_camera else (448 if has_config else 0), 0, "main.js")
    if has_camera:
        field(384 - 16, 2048 + len(script), 8)
        field(384 - 8, len(camera_override), 4)
        node(384, 64, 128, 448 if has_config else 0, 0, "playerCamera.js")
    if has_config:
        field(448 - 16, 2048 + len(script) +
              (len(camera_override) if has_camera else 0), 8)
        field(448 - 8, len(config_override), 4)
        node(448, 64, 384 if has_camera else 128, 0, 0, "playerConfig.js")
    field(256 - 8, 320, 8)
    node(256, 8, 64, 512 if prefab_override is not None else 0, 0x8000, "level")
    field(320 - 16, 2048 + len(script) +
          (len(camera_override) if has_camera else 0) +
          (len(config_override) if has_config else 0), 8)
    field(320 - 8, len(scene), 4)
    node(320, 256, 0, 0, 0, scene_name)
    if prefab_override is not None:
        field(512 - 8, 576, 8)
        node(512, 8, 256, 0, 0x8000, "prefabs")
        field(576 - 16, 2048 + len(script) +
              (len(camera_override) if has_camera else 0) +
              (len(config_override) if has_config else 0) + len(scene), 8)
        field(576 - 8, len(prefab_override), 4)
        node(576, 512, 0, 0, 0, "roadblock.nprefab")
    data.extend(b'\x00' * (2048 - len(data)))
    data.extend(script)
    if has_camera:
        data.extend(camera_override)
    if has_config:
        data.extend(config_override)
    data.extend(scene)
    if prefab_override is not None:
        data.extend(prefab_override)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


if __name__ == "__main__":
    if len(sys.argv) not in (2, 4):
        raise SystemExit("Usage: archive_fixture.py OUTPUT.db [SCENE.nscene MAIN.js]")
    generate(Path(sys.argv[1]),
             Path(sys.argv[2]).read_bytes() if len(sys.argv) == 4 else None,
             Path(sys.argv[3]).read_bytes() if len(sys.argv) == 4 else None)
