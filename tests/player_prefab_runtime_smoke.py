"""Actual Player V8 + VFS Scene V3 prefab hierarchy integration."""
from __future__ import annotations

from pathlib import Path
import shutil
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import run
from tools.v8_provider.archive_fixture import generate

PREFAB = """NorthStarCpp.Scene 3
scene "Runtime roadblock"
streaming 150 220 1
stream_budget 64 64
layer "Default" 1
entity 1 0 "Roadblock root" "Default" "" "" "" 0 0 0 2 46b9d8 0 0 0 0 0 1 1 1 0 1000000 1 1
entity 2 1 "Roadblock child" "Default" "" "" "" 3 0 0 1 FF9933 0 0 0 0 0 1 1 1 0 1000000 1 1
"""

SCRIPT = r'''import { Prefab, Scene, Events, Physics } from "@northstar/engine";
let instance = null;
let frames = 0;
const save = (slot, payload) =>
    engine.storage.saves.write(slot, JSON.stringify(payload));
Events.on("start", () => {
    const before = Scene.objectCount();
    instance = Prefab.instantiate("prefabs/roadblock.nprefab", {
        position: {x:45, y:1, z:45}
    });
    if (!instance.alive || instance.entities.length !== 2 ||
        instance.root.index !== before)
        throw new Error("Prefab did not create two runtime ECS entities");
    const [root, child] = instance.entities;
    const props = child.snapshot;
    if (props.parent !== root.id || props.position.x !== 48 ||
        Scene.findByName("Roadblock child").id !== child.id)
        throw new Error("Prefab hierarchy or relative transform is incorrect");
    let hit = Physics.sweepSphere(
        {x:48,y:5,z:45},{x:48,y:2,z:45},0.2);
    if (!hit || hit.index !== child.index)
        throw new Error("Prefab child has no physical collider");
    save("scripting_start", {phase:"start",
        objects: Scene.objectCount(), root: root.index, child: child.index});
});
Events.on("update", () => {
    if (++frames === 4) {
        const [root, child] = instance.entities;
        const childIndex = child.index;
        const rootIndex = root.index;
        if (instance.destroy() !== 2 || child.alive || root.alive ||
            Scene.get(childIndex) || Scene.get(rootIndex) ||
            Physics.sweepSphere({x:48,y:5,z:45},{x:48,y:2,z:45},0.2))
            throw new Error("Prefab group removal left a collider or scene node");
        save("scripting_update", {phase:"update", frames, dt:1/60});
        engine.session.lifecycle.quit();
    }
});
Events.on("stop", () => save("scripting_stop", {phase:"stop",frames}));
'''


def verify(player: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="northstar-prefab-qa-") as td:
        project=Path(td)/"project"
        shutil.copytree(ROOT/"Projects/NorthStarFirstPersonPlatform",project)
        # Source of truth: scripts are project assets. Do not write parallel
        # legacy project/scripts files that the runtime correctly ignores.
        (project/"assets/scripts").mkdir(parents=True,exist_ok=True)
        (project/"assets/scripts/main.js").write_text(SCRIPT,encoding="utf-8")
        (project/"assets/scripts/playerConfig.js").write_text(
            "NorthStar.player.configure({enabled:false});\n",encoding="utf-8")
        (project/"prefabs").mkdir(exist_ok=True)
        (project/"prefabs/roadblock.nprefab").write_text(PREFAB,encoding="utf-8")
        scene=next(p for p in (project/"main.nscn",
            project/"main.nscene") if p.is_file())
        archive=Path(td)/"prefab-game.db"
        generate(archive,scene.read_bytes(),SCRIPT.encode("utf-8"),
                 prefab_override=PREFAB.encode("utf-8"))
        for source in (None,archive):
            result=run(player,None,project,source,seconds=45)
            markers=result["markers"]
            assert markers["scripting_start"]["root"] < markers["scripting_start"]["child"]
            print(f"Player V8 Scene V3 prefab {result['mode']} VFS hierarchy + physics: PASS",
                  flush=True)


if __name__=="__main__":
    verify(Path(sys.argv[1]))
