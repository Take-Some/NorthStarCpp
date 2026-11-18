#!/usr/bin/env python3
"""Conservative source-level decomposition of stateful Win32 and Viewport code.

The extracted .inc files remain in the original C++ translation units. This
preserves private App/Viewport::State access, symbol visibility, function
ordering and ABI. Further .cpp/pimpl extraction requires separate interface
work and native QA. Refuses to overwrite existing modules or partial splits.

Usage:
    python tools/dev/decompose_runtime_sources.py --apply
    python tools/dev/decompose_runtime_sources.py --verify
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib

REPO = pathlib.Path(__file__).resolve().parents[2]
MODULES = REPO / "engine" / "northstarcpp" / "modules"
SPLITS = {
    "editor/window.cpp": [
        ("    void update_fps_title()", "    // Stateful App methods remain in the same class until Pimpl extraction.\n#include \"window_runtime_win32_messages.inc\"", "window_runtime_fps_bridge.inc"),
        ("    HWND child(", "    void play_fps_clip(", "window_editor_workspace.inc"),
        ("    void configure_script_bindings(", "    void load_script_files(", "window_runtime_script_bindings.inc"),
        ("    void load_script_files(", "    // Stateful App methods remain in the same class until Pimpl extraction.\n#include \"window_runtime_frame_loop.inc\"", "window_runtime_script_lifecycle.inc"),
    ],
    "viewport/viewport.cpp": [
        ("struct Viewport::State {", "Viewport::Viewport(HWND window", "viewport_state.inc"),
        ("void Viewport::draw(", "bool Viewport::supports_postfx()", "viewport_draw.inc"),
    ],
}

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def split_file(relative: str, specs: list[tuple[str, str, str]], apply: bool) -> None:
    path = MODULES / relative
    original = path.read_bytes()
    sep = bytes((13, 10)) if bytes((13, 10)) in original else bytes((10,))
    # Match anchors across CRLF or LF without converting file contents.
    normalize = lambda text: text.replace(chr(10), chr(13) + chr(10)).encode("utf-8") if sep == bytes((13, 10)) else text.encode("utf-8")
    sections = []
    for start, end, target in specs:
        key = normalize(start)
        tail = normalize(end)
        if original.count(key) != 1 or original.count(tail) != 1:
            raise ValueError(f"{relative}: non-unique or missing boundary: {start!r} -> {end!r}")
        begin = original.index(key)
        finish = original.index(tail, begin + len(key))
        if finish <= begin:
            raise ValueError(f"{relative}: invalid boundary ordering: {target}")
        out = path.with_name(target)
        if out.exists():
            raise FileExistsError(f"Refusing to overwrite {out}")
        sections.append((begin, finish, out, original[begin:finish]))
    ordered = sorted(sections)
    for (_, finish, _, _), (next_start, _, _, _) in zip(ordered, ordered[1:]):
        if finish > next_start:
            raise ValueError(f"{relative}: overlapping extraction boundaries")
    updated = original
    for begin, finish, out, payload in reversed(ordered):
        marker = b'#include "' + out.name.encode("ascii") + b'"' + sep + sep
        updated = updated[:begin] + marker + updated[finish:]
    recovered = updated
    for _, _, out, payload in ordered:
        marker = b'#include "' + out.name.encode("ascii") + b'"' + sep + sep
        if recovered.count(marker) != 1:
            raise ValueError(f"{relative}: include marker not unique: {out.name}")
        recovered = recovered.replace(marker, payload, 1)
    if recovered != original:
        raise AssertionError(f"{relative}: exact source reconstruction failed")
    print(f"{relative}: {len(original)} -> {len(updated)} bytes, "
          f"{len(ordered)} extracted sections, original sha256={sha(original)}")
    for _, _, out, payload in ordered:
        print(f"  {out.name}: {len(payload)} bytes, {payload.count(sep)} lines")
    if not apply:
        return
    # Backup the live source before mutation, retaining any preexisting edits.
    backup_dir = REPO / "out" / "refactor_backups"
    backup_dir.mkdir(parents=True, exist_ok=True)
    backup = backup_dir / (path.stem + "_" + path.parent.name + "_" + sha(original)[:12] + ".original")
    if not backup.exists():
        backup.write_bytes(original)
    for _, _, out, payload in ordered:
        out.write_bytes(payload)
    if path.read_bytes() != original:
        raise RuntimeError(f"{path} changed concurrently: original intact, extracted files retained")
    path.write_bytes(updated)
    print(f"  saved original at {backup.relative_to(REPO)}")

def verify_file(relative: str, specs: list[tuple[str, str, str]]) -> None:
    path = MODULES / relative
    source = path.read_bytes()
    sep = bytes((13, 10)) if bytes((13, 10)) in source else bytes((10,))
    for start, _, target in specs:
        out = path.with_name(target)
        marker = b'#include "' + target.encode("ascii") + b'"'
        if source.count(marker) != 1 or not out.is_file():
            raise ValueError(f"{relative}: missing or repeated extracted module {target}")
        data = out.read_bytes()
        if not data.startswith(start.encode("utf-8")):
            raise ValueError(f"{target}: unexpected module boundary")
        if len(data) < 40:
            raise ValueError(f"{target}: suspiciously empty module")
        print(f"{relative}: {target} OK ({len(data)} bytes, sha256={sha(data)[:16]})")
    print(f"{relative}: {len(source)} bytes, includes verified")

def main() -> None:
    parser = argparse.ArgumentParser()
    mutually = parser.add_mutually_exclusive_group(required=True)
    mutually.add_argument("--apply", action="store_true")
    mutually.add_argument("--plan", action="store_true")
    mutually.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.plan:
        for relative, specs in SPLITS.items():
            split_file(relative, specs, False)
    elif args.apply:
        # Never start a partial second run on already extracted sources.
        for relative, specs in SPLITS.items():
            for _, _, name in specs:
                if (MODULES / relative).with_name(name).exists():
                    raise FileExistsError(f"Already extracted: {relative} => {name}")
        for relative, specs in SPLITS.items():
            split_file(relative, specs, True)
    else:
        for relative, specs in SPLITS.items():
            verify_file(relative, specs)

if __name__ == "__main__":
    main()
