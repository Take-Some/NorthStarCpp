"""E2E QA: the original project-local pistol OBJ / Scene V3 prefab.

Spawns a nonfunctional game prop through real V8, verifies a live rigid body,
checks gravity and removes the entity before stopping the Player.
"""
from __future__ import annotations

from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.player_e2e import run

SCRIPT = r'''import { Scene, Prefab, Events, Physics } from "@northstar/engine";
let item = null;
let frames = 0;
const save = (slot, data) => engine.storage.saves.write(slot, JSON.stringify(data));
Events.on("start", () => {
    const before = Scene.objectCount();
    item = Prefab.instantiate("prefabs/weapon/pistol.nprefab", {
        position: {x: 45, y: 1, z: 45}
    });
    const entity = item.root;
    if (!item.alive || item.entities.length !== 1 ||
        entity.index !== before || entity.snapshot?.name !== "Pistol" ||
        entity.snapshot?.mass < 0.5)
        throw new Error("Pistol is not a valid runtime physics prefab");
    const hit = Physics.sweepSphere(
        {x:45,y:3,z:45}, {x:45,y:1,z:45}, 0.1);
    if (!hit || hit.index !== entity.index)
        throw new Error("Pistol runtime collider is missing");
    Scene.playAnimation(entity.index,
        "animations/weapon/pistol/military-pistol.ycd@p-fire");
    save("scripting_start", {phase:"start", objects: Scene.objectCount(),
        id:entity.id, index:entity.index, hit:hit.index,
        animation:"p-fire"});
});
Events.on("update", () => {
    ++frames;
    if (frames === 5) {
        Scene.playAnimation(item.root.index,
            "animations/weapon/pistol/military-pistol.ycd@p-reload");
    }
    if (frames === 12) {
        const now = item.root.snapshot;
        if (!now || !(now.position.y < 0.99))
            throw new Error("Pistol rigid body did not fall with gravity");
        save("scripting_update", {phase:"update", frames, dt:1/60,
            y:now.position.y});
        engine.session.lifecycle.quit();
    }
});
Events.on("stop", () => {
    const index = item.root.index;
    Scene.stopAnimation(index);
    if (item.destroy() !== 1 || Scene.get(index) !== null)
        throw new Error("Pistol destructor left an ECS/physics entity");
    save("scripting_stop", {phase:"stop", frames});
});
'''


def verify(player: Path) -> None:
    project_source = ROOT / "Projects/NorthStarFirstPersonPlatform"
    assert (ROOT/"engine/northstarcpp/resources/sharedAssets/prefabs/weapon/pistol.nprefab").is_file()
    assert (ROOT/"engine/northstarcpp/resources/sharedAssets/models/weapon/pistol/pistol.ydd").is_file()
    with tempfile.TemporaryDirectory(prefix="northstar-pistol-runtime-") as work:
        project = Path(work)/"project"
        shutil.copytree(project_source, project)
        (project/"assets/scripts").mkdir(parents=True, exist_ok=True)
        (project/"assets/scripts/main.js").write_text(SCRIPT, encoding="utf-8")
        (project/"assets/scripts/playerConfig.js").write_text(
            "NorthStar.player.configure({enabled:false});\n", encoding="utf-8")
        output = run(player, None, project, None, seconds=45)
        assert output["markers"]["scripting_start"]["hit"] == \
            output["markers"]["scripting_start"]["index"]
        assert output["markers"]["scripting_start"]["animation"] == "p-fire"
        assert output["markers"]["scripting_update"]["y"] < 0.99
        print("NorthStar Pistol native YDD + Scene V3 prefab + physics + V8: PASS",
              flush=True)


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
