#!/usr/bin/env python3
"""Fail CI when parallel asset-root/VFS loading paths reappear.

The invariant applies to asset discovery and *read-only* preview services.
Asset authoring may still use bounded physical file I/O to save editable
documents; the provider ABI remains the only runtime asset read API.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("repo", type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()
    modules = repo / "engine/northstarcpp/modules"
    errors: list[str] = []

    locations: dict[str, str] = {}
    for file in modules.rglob("*"):
        if file.suffix.lower() not in {".cpp", ".hpp", ".h", ".inc"}:
            continue
        path = file.relative_to(repo).as_posix()
        body = file.read_text(encoding="utf-8", errors="replace")
        locations[path] = body
        for legacy, reason in [
            (r'/\s*"sharedAssets"', "legacy parent/sharedAssets lookup"),
            (r"locateSharedContentRoot\s*\(", "Qt-specific root resolver"),
            (r"mount_preview_assets\s*\(", "preview-only mount service"),
        ]:
            for match in re.finditer(legacy, body):
                errors.append(f"{path}: {reason}, byte offset {match.start()}")
        if path != "engine/northstarcpp/modules/host/assets.cpp":
            for match in re.finditer(
                r'"engine/northstarcpp/resources/sharedAssets"', body
            ):
                errors.append(
                    f"{path}: duplicated hardcoded engine asset path at {match.start()}"
                )

    paths = {
        "host": "engine/northstarcpp/modules/host/assets.cpp",
        "viewport": "engine/northstarcpp/modules/viewport/viewport.cpp",
        "options": "engine/northstarcpp/modules/editor/window_runtime_options.cpp",
        "thumbnail": "engine/northstarcpp/modules/editor/qt_content_browser_thumbnails.cpp",
        "material": "engine/northstarcpp/modules/editor/editor_asset_material_source.cpp",
        "preview": "engine/northstarcpp/modules/editor/qt_asset_preview_dialog.cpp",
        "prefab": "engine/northstarcpp/modules/editor/editor_prefab_composition.cpp",
    }
    for label, path in paths.items():
        if path not in locations:
            errors.append(f"Missing source: {label} / {path}")

    host = locations.get(paths["host"], "")
    for needle in [
        "AssetStore::mount_base_assets(",
        "AssetStore::mount_project_assets(",
        "AssetStore::mount_content_assets(",
        "asset_paths::locate_shared_root(",
    ]:
        if host.count(needle) != 1 and needle != "asset_paths::locate_shared_root(":
            errors.append(f"Host: expected single VFS implementation of {needle}")
        if needle == "asset_paths::locate_shared_root(" and needle not in host:
            errors.append("Host: shared-root resolution is not centralized")

    for label in ["viewport", "options"]:
        if "asset_paths::locate_shared_root(" not in locations.get(paths[label], ""):
            errors.append(f"{label} does not use common engine asset paths")
    for label in ["thumbnail", "material", "preview"]:
        body = locations.get(paths[label], "")
        if label == "preview":
            required = "mount_content_assets("
        else:
            required = "mount_content_assets("
        if required not in body and label != "preview":
            errors.append(f"{label} does not use common editor VFS")
        if label == "preview" and "mount_content_assets(" not in body:
            errors.append("Prefab preview does not use common VFS")
    for label in ["thumbnail", "material"]:
        body = locations.get(paths[label], "")
        for bypass in ["std::ifstream", "std::filebuf", "load_materials_from_bytes(",
                       "load_textures_from_bytes(", "load_drawables_from_bytes("]:
            if bypass in body:
                errors.append(f"{label} bypasses engine.assets via {bypass}")

    if errors:
        print("Asset architecture single-source audit: FAIL", file=sys.stderr)
        for error in errors[:60]:
            print(" - " + error, file=sys.stderr)
        return 1
    print(
        "Asset architecture single-source audit: PASS "
        f"({len(locations)} engine modules inspected; one VFS root policy)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
