"""End-to-end current Player/Jolt/V8 check for the first-person project."""
from __future__ import annotations
from pathlib import Path
import os
import shutil
import sys
import tempfile
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.player_e2e import read_marker

QA = r'''
let qaFrames = 0;
let qaOwnedId = null;
Events.on("start", () => {
    const owned = Scene.findPawn();
    const crate = Scene.findByName("Course Pushable Crate");
    if (!owned || !crate || Player.index < 0 || !Player.entityId)
        throw new Error("Pawn, native controller or game course missing");
    qaOwnedId = Player.entityId;
    if (Scene.objectCount() < 48)
        throw new Error("Course markers were not created");
    engine.save.write("scripting_start", JSON.stringify({
        phase: "start", objects: Scene.objectCount(),
        pawn: qaOwnedId, index: Player.index, crateIndex: crate.index, spawn: owned.position
    }));
});
Events.on("update", dt => {
    qaFrames++;
    if (qaFrames === 3) {
        const stable = Player.entityId;
        Player.unpossess();
        if (Player.index !== -1 || Player.entityId !== null)
            throw new Error("Unpossess retained controller ownership");
        Player.possess(stable);
        if (Player.entityId !== stable || Player.index < 0)
            throw new Error("Possess did not restore stable Entity ownership");
        Player.sprint(true);
    }
    if (qaFrames === 4) {
        const crate = Scene.findByName("Course Pushable Crate");
        if (!crate) throw new Error("Course crate disappeared during physics");
        engine.save.write("scripting_update", JSON.stringify({
            phase: "update", frames: qaFrames, dt, pawn: Player.entityId,
            cratePosition: crate.position
        }));
        engine.session.quit();
    }
});
Events.on("stop", () => {
    engine.save.write("scripting_stop", JSON.stringify({
        phase: "stop", frames: qaFrames
    }));
});
'''

def run_direct(player: Path, project: Path, seconds: int = 25):
    # Win32 V8 workers can inherit stdout/stderr pipes. Use files instead of
    # subprocess.run(capture_output=True), which may otherwise wait for them.
    with tempfile.TemporaryDirectory(prefix="northstar-fp-run-",
                                     ignore_cleanup_errors=True) as tmp:
        root = Path(tmp)
        appdata = root / "AppData"
        appdata.mkdir()
        logs = root / "logs"
        logs.mkdir()
        env = os.environ.copy()
        env["LOCALAPPDATA"] = str(appdata)
        env["NORTHSTARCPP_LOG_DIRECTORY"] = str(logs)
        cmd = [str(player.resolve()), "--warp", "--fps", "--scene",
               str((project / "main.nscn").resolve()), "--shared-assets",
               str((ROOT / "sharedAssets").resolve())]
        with (root / "out.txt").open("wb") as out, (root / "err.txt").open("wb") as err:
            process = subprocess.Popen(cmd, cwd=player.parent, env=env,
                                       stdout=out, stderr=err)
            expired = False
            try:
                process.wait(timeout=seconds)
            except subprocess.TimeoutExpired:
                expired = True
                process.kill()
                process.wait(timeout=10)
        stderr = (root / "err.txt").read_text(encoding="utf-8",errors="replace")
        logs_tail = []
        for file in logs.glob("*.log"):
            text = file.read_text(encoding="utf-8",errors="replace")
            logs_tail.extend(text.splitlines()[-25:])
        try:
            markers = {key: read_marker(appdata, key)
                       for key in ("scripting_start","scripting_update","scripting_stop")}
        except Exception as exc:
            raise RuntimeError(
                f"FP runtime markers missing (exit={process.returncode}, "
                f"timeout={expired}): {exc}\n"
                f"STDERR:\n{stderr[-3500:]}\nLOGS:\n"
                + "\n".join(logs_tail[-35:])) from exc
        if expired or process.returncode != 0:
            raise RuntimeError(
                f"FP runtime exit={process.returncode} timeout={expired} "
                f"stderr={stderr[-2500:]}")
        return {"markers": markers}


def verify(player: Path) -> None:
    old_physics = os.environ.get("NORTHSTARCPP_PHYSICS_PROVIDER")
    old_render = os.environ.get("NORTHSTARCPP_RENDER_PROVIDER")
    os.environ["NORTHSTARCPP_PHYSICS_PROVIDER"] = "jolt"
    os.environ["NORTHSTARCPP_RENDER_PROVIDER"] = "d3d12"
    try:
        with tempfile.TemporaryDirectory(prefix="northstar-fp-protocol-") as tmp:
            project = Path(tmp) / "game"
            shutil.copytree(ROOT / "Projects/NorthStarFirstPersonPlatform", project)
            source = project / "scripts/main.js"
            source.write_text(source.read_text(encoding="utf-8") + "\n" + QA,
                              encoding="utf-8")
            result = run_direct(player, project, seconds=25)
            start, update = result["markers"]["scripting_start"], result["markers"]["scripting_update"]
            assert start["objects"] >= 48, start
            assert start["pawn"] == update["pawn"], (start, update)
            assert update["frames"] == 4 and update["cratePosition"]["y"] < 10, update
            print("NorthStar First Person Protocol real Jolt/V8: PASS")
            print(f"  Pawn={start['pawn']} spawn={start['spawn']} entities={start['objects']} frames={update['frames']}")
    finally:
        if old_physics is None:
            os.environ.pop("NORTHSTARCPP_PHYSICS_PROVIDER",None)
        else:
            os.environ["NORTHSTARCPP_PHYSICS_PROVIDER"] = old_physics
        if old_render is None:
            os.environ.pop("NORTHSTARCPP_RENDER_PROVIDER",None)
        else:
            os.environ["NORTHSTARCPP_RENDER_PROVIDER"] = old_render

if __name__ == "__main__":
    verify(Path(sys.argv[1]))
