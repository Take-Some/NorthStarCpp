"""End-to-end Windows player/V8 QA against an isolated copy of a real project.

Run in a Windows desktop session. Does not alter the original game project or
shipping DLLs. Success requires native player, V8 startup/update/shutdown,
persisted engine.save markers, and an ordinary 0 process exit.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any


def read_marker(root: Path, slot: str) -> dict[str, Any]:
    matches = list(root.rglob(slot + ".nsave"))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one {slot} save marker, found {len(matches)}")
    payload = matches[0].read_bytes()
    rows = payload.split(b"\n", 3)
    if len(rows) != 4 or rows[0] != b"NorthStarCpp.Save 1":
        raise RuntimeError("Unexpected save file header")
    length = int(rows[1])
    if len(rows[3]) != length:
        raise RuntimeError("Incomplete save record")
    return json.loads(rows[3])


def run(player: Path, provider: Path | None, project: Path,
        archive: Path | None = None, seconds: int = 35) -> dict[str, Any]:
    player = player.resolve(strict=True)
    project = project.resolve(strict=True)
    scene = next((candidate for candidate in
                  (project / "main.nscn", project / "main.nscene")
                  if candidate.is_file()), None)
    if scene is None:
        raise RuntimeError("QA project startup scene missing")
    exe_dir = player.parent
    env = os.environ.copy()
    if provider is None:
        for key in ("NORTHSTARCPP_SCRIPTING_PLUGIN", "NORTHSTARCPP_V8_D8_EXECUTABLE",
                    "NORTHSTARCPP_V8_SIDECAR_WORKER"):
            env.pop(key, None)
    else:
        provider = provider.resolve(strict=True)
        env["NORTHSTARCPP_SCRIPTING_PLUGIN"] = str(provider)
        env["PATH"] = str(provider.parent) + os.pathsep + str(exe_dir) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="northstar-player-v8-") as tmp:
        working = Path(tmp)
        appdata = working / "LocalAppData"
        appdata.mkdir()
        env["LOCALAPPDATA"] = str(appdata)
        if archive:
            cmd = [str(player), "--warp", "--fps", "--archive", str(archive.resolve(strict=True)),
                   "--scene", "level/" + scene.name]
            mode = "archive"
        else:
            cmd = [str(player), "--warp", "--fps", "--scene", str(scene)]
            mode = "source"
        try:
            result = subprocess.run(cmd, cwd=exe_dir, env=env, capture_output=True,
                                    text=True, errors="replace", timeout=seconds)
        except subprocess.TimeoutExpired as e:
            raise RuntimeError(f"{mode} player did not quit within {seconds}s") from e
        if result.returncode != 0:
            raise RuntimeError(f"{mode} player exit code {result.returncode}: {result.stderr[-1500:]}")
        markers = {}
        for name in ("scripting_start", "scripting_update", "scripting_stop"):
            markers[name] = read_marker(appdata, name)
        # Soft asset-load failures are WARNINGs, not fatal. The asset
        # manager deliberately skips optional malformed or missing assets.
        output_lines = (result.stdout + result.stderr).splitlines()
        fatal_lines = [
            line for line in output_lines
            if ("[ERROR]" in line or "[FATAL]" in line
                or "Plugin ABI mismatch" in line
                or "Unhandled project script error" in line)
            and "[WARN]" not in line
        ]
        if fatal_lines:
            raise RuntimeError(f"{mode} player reported fatal runtime errors: "
                               + " | ".join(fatal_lines[-5:])[-1000:])
        if (markers["scripting_start"].get("phase") != "start" or
                markers["scripting_start"].get("objects", 0) < 5 or
                markers["scripting_update"].get("phase") != "update" or
                markers["scripting_update"].get("frames", 0) < 4 or
                markers["scripting_stop"].get("phase") != "stop"):
            raise RuntimeError(f"Missing/incorrect {mode} gameplay lifecycle: {markers}")
        return {"mode": mode, "pass": True, "markers": markers,
                "stdout_tail": result.stdout[-200:], "stderr_tail": result.stderr[-200:]}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--provider", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--timeout", type=int, default=35)
    parser.add_argument("--report", type=Path)
    opts = parser.parse_args()
    outputs = [run(opts.player, opts.provider, opts.project,
                   seconds=opts.timeout)]
    if opts.archive:
        outputs.append(run(opts.player, opts.provider, opts.project,
                           opts.archive, seconds=opts.timeout))
    result = {"player": str(opts.player), "provider": str(opts.provider),
              "results": outputs}
    data = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if opts.report:
        opts.report.parent.mkdir(parents=True, exist_ok=True)
        opts.report.write_text(data, encoding="utf-8")
    print(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
