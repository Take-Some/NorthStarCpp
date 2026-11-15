"""Player startup VFS regression for Qt Editor F6 Play.
This checks the full App startup path, NOT --check-scene (which skips mounts).
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def launch(cmd: list[str], *, cwd: Path, env: dict[str, str]) -> None:
    completed = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True,
                               text=True, errors="replace", timeout=40)
    if completed.returncode:
        raise RuntimeError(
            f"command failed: {cmd!r} (rc={completed.returncode})\\n"
            f"stdout={completed.stdout[-1600:]}\\n"
            f"stderr={completed.stderr[-1600:]}")
    print("PASS", " ".join(cmd[-7:]), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--shared", type=Path, required=True)
    args = parser.parse_args()

    editor = args.editor.resolve(strict=True)
    player = args.player.resolve(strict=True)
    shared = args.shared.resolve(strict=True)
    env = os.environ.copy()
    # Never load overridden plugin paths from a developer shell.
    for key in list(env):
        if key in {"NORTHSTARCPP_MODULES", "NORTHSTARCPP_ASSETS_PLUGIN",
                   "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"}:
            env.pop(key, None)
    with tempfile.TemporaryDirectory(prefix="northstarcpp-mount-regression-") as temp:
        root = Path(temp)
        scene = root / "main.nscn"
        assets = root / "assets"
        assets.mkdir()
        zones = assets / "streaming" / "zones.json"
        zones.parent.mkdir()
        zones.write_text(json.dumps({"version": 1, "zones": []}),encoding="utf-8")
        env["LOCALAPPDATA"] = str(root / "local")
        env["APPDATA"] = str(root / "appdata")
        (root / "local").mkdir()
        (root / "appdata").mkdir()

        launch([str(editor), "--smoke", "--smoke-output", str(scene)],
               cwd=editor.parent,env=env)
        if not scene.is_file():
            raise RuntimeError("Qt Editor did not produce NSCN fixture")

        base = [str(player), "--runtime-init-smoke", "--scene", str(scene)]
        # This EXACT misuse of --shared-assets used to raise the message box
        # from AssetManager.dll and was invisible to --check-scene.
        launch(base + ["--shared-assets", str(assets)],
               cwd=player.parent,env=env)
        # Canonical-equivalent paths must not mount twice, either.
        launch(base + ["--shared-assets", str(assets) + os.sep + "."],
               cwd=player.parent,env=env)
        # Zone loader also used a duplicate root when the same argument
        # appeared; exercise both independent AssetManager instances.
        launch(base + ["--shared-assets", str(assets),
                       "--asset-zones", str(zones)],
               cwd=player.parent,env=env)
        # A genuinely separate shared content layer is still mounted and
        # remains lower-priority than project/assets.
        launch(base + ["--shared-assets", str(shared)],
               cwd=player.parent,env=env)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as err:
        print("FAIL",err,file=sys.stderr)
        raise SystemExit(1)
