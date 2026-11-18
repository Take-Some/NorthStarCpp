"""Real Player/V8 runtime entity creation, physics and disposal integration."""
from __future__ import annotations

from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.archive_fixture import generate
from tools.v8_provider.player_e2e import run

SCRIPT = r'''import { Scene, Entity, Events, Physics } from "@northstar/engine";

let staticEntity = null;
let dynamicEntity = null;
let ticks = 0;

const save = (slot, value) =>
    engine.storage.saves.write(slot, JSON.stringify(value));

Events.on("start", () => {
    if (typeof Entity !== "function" ||
        !NorthStar.modules.engine.Entity ||
        typeof Scene.spawn !== "function")
        throw new Error("Entity API was not linked");
    const before = Scene.objectCount();
    staticEntity = Scene.spawn({
        name: "Runtime Static Block",
        position: {x: 45, y: 1, z: 45},
        size: 2,
        mass: 0,
        color: 0xCC8844
    });
    if (!(staticEntity instanceof Entity) ||
        !staticEntity.alive ||
        staticEntity.index !== before ||
        Scene.findByName("Runtime Static Block").index !== staticEntity.index)
        throw new Error("Runtime static entity was not registered in ECS/scene");
    let hit = Physics.sweepSphere(
        {x:45,y:5,z:45},{x:45,y:2,z:45},0.2);
    if (!hit || hit.index !== staticEntity.index)
        throw new Error("Runtime static collider missing");
    save("scripting_start", {
        phase: "start", objects: Scene.objectCount(),
        entity: staticEntity.id, index: staticEntity.index,
        collide: hit.index
    });
});

Events.on("update", () => {
    if (++ticks === 2) {
        staticEntity.setPosition({x:48,y:1,z:45}).setColor(0x3399FF);
        let oldHit = Physics.sweepSphere(
            {x:45,y:5,z:45},{x:45,y:2,z:45},0.2);
        let newHit = Physics.sweepSphere(
            {x:48,y:5,z:45},{x:48,y:2,z:45},0.2);
        if (oldHit || !newHit || newHit.index !== staticEntity.index)
            throw new Error("Native collider relocation failed");
        const previous = staticEntity.index;
        staticEntity.destroy();
        if (Scene.get(previous) || staticEntity.alive ||
            Physics.sweepSphere(
                {x:48,y:5,z:45},{x:48,y:2,z:45},0.2))
            throw new Error("Runtime destroy left an entity/collider");
        dynamicEntity = Scene.spawn({
            name: "Runtime Dynamic Block",
            position: {x:49,y:5,z:49},
            size: 1, mass: 3
        });
        if (dynamicEntity.index <= previous)
            throw new Error("Deleted scene indices were reused");
    }
    if (ticks === 14) {
        const y = dynamicEntity.snapshot.position.y;
        if (y >= 5)
            throw new Error("Runtime dynamic body did not participate in gravity");
        save("scripting_update", {phase:"update",frames:ticks,
            dt:1/60, dynamicY:y, index: dynamicEntity.index});
        engine.session.lifecycle.quit();
    }
});
Events.on("stop", () => {
    save("scripting_stop", {phase:"stop",frames:ticks});
});
'''


def verify(player: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="northstar-scene-runtime-") as work:
        root = Path(work)
        project = root / "project"
        shutil.copytree(ROOT / "Projects/NorthStarFirstPersonPlatform", project)
        script = project / "assets" / "scripts" / "main.js"
        script.write_text(SCRIPT, encoding="utf-8")
        (project / "assets" / "scripts" / "playerConfig.js").write_text(
            "NorthStar.player.configure({enabled:false});\n", encoding="utf-8")
        # Project overrides are not included in archive; the camera-only
        # fixture must use the engine's shared scripts and core assets.
        scene = next(p for p in (project / "main.nscn",
                  project / "main.nscene") if p.is_file())
        archive = root / "runtime-scene.db"
        generate(archive, scene.read_bytes(), script.read_bytes())
        for source in (None, archive):
            output = run(player, None, project, source, seconds=45)
            first = output["markers"]["scripting_start"]
            update = output["markers"]["scripting_update"]
            assert first["collide"] == first["index"], output
            assert update["dynamicY"] < 5, output
            assert update["index"] > first["index"], output
            print(f"Scene Runtime 1.0 V8 {output['mode']}: PASS", flush=True)


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
