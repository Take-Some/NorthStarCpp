"""Exercise bundled V8 discovery and the real Player lifecycle."""
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.archive_fixture import generate
from tools.v8_provider.player_e2e import run

SCRIPT = r"""import { Camera, Input, Events, Player } from "@northstar/engine";
(() => {
    // The engine loads its compiled default playerCamera.js automatically,
    // BEFORE the project game script. A project may replace the same VFS path.
    if (typeof NorthStar !== "object" || NorthStar.apiVersion !== 1 ||
        typeof NorthStar.camera.createViewController !== "function" ||
        typeof NorthStar.character.createController !== "function")
        throw new Error("Shared NorthStar API was not bootstrapped");
    if (typeof engine.camera.available !== "function" ||
        !engine.camera.available())
        throw new Error("Native camera API is unavailable");
    if (typeof engine.input.wheelDelta !== "function" ||
        typeof engine.camera.followDistance !== "function")
        throw new Error("Camera/input VFS contract is unavailable");
    if (typeof Camera !== "function" || typeof Input !== "function" ||
        typeof Events !== "function" || typeof Player !== "function")
        throw new Error("Named @northstar/engine class imports did not link");
    let frames = 0;
    const save = (slot, data) => engine.save.write(slot, JSON.stringify(data));
    Events.on("start", () => {
        if (!["first_person", "third_person"].includes(Camera.mode()))
            throw new Error("Root player camera did not initialize");
        save("scripting_start", {
            phase:"start", objects:engine.scene.objectCount(),
            camera:Camera.mode(),
            playerEnabled:Player.enabled,
            playerIndex:Player.index
        });
    });
    Events.on("update", dt => {
        if (!Number.isFinite(Input.wheelDelta()))
            throw new Error("Mouse wheel API delivered invalid data");
        if (++frames === 4) {
            save("scripting_update", {
                phase:"update", frames, dt, camera:Camera.mode(),
                distance:Camera.followDistance()
            });
            engine.session.quit();
        }
    });
    Events.on("stop", () => save("scripting_stop", {phase:"stop", frames}));
})();"""

# Deliberately different from the engine default. If the VFS resolves the
# wrong file, the start marker will incorrectly contain third_person.
PROJECT_CAMERA = r"""import { Camera, Events } from "@northstar/engine";
Events.on("start", () => Camera.setMode("first_person", 0));
Events.on("update", () => {});"""


def verify(player: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="northstar-bundled-v8-") as temp:
        root = Path(temp)
        project = root / "project"
        shutil.copytree(ROOT / "Projects/NorthStarFirstPersonPlatform", project)
        script = project / "scripts/main.js"
        script.write_text(SCRIPT, encoding="utf-8")
        # Camera-only E2E must not accidentally instantiate the example Abby
        # when its author-defined playerConfig.js is copied with the project.
        # A disabled config still exercises the shared root-script loader.
        (project / "scripts/playerConfig.js").write_text(
            "NorthStar.player.configure({enabled:false});\n", encoding="utf-8")
        scene = next((candidate for candidate in
                      (project / "main.nscn", project / "main.nscene")
                      if candidate.is_file()), None)
        if scene is None:
            raise RuntimeError("Bundled V8 fixture has no startup scene")
        archive = root / "project.db"
        # No camera override: the default shared root script must initialize
        # third person without any project-specific camera implementation.
        generate(archive, scene.read_bytes(), script.read_bytes())
        for source in (None, archive):
            result = run(player, None, project, source, seconds=40)
            assert result["markers"]["scripting_start"]["camera"] == "third_person", (
                "Engine default shared camera script did not load")
            print(f"Bundled V8 default camera {result['mode']}: PASS", flush=True)

        override = project / "scripts/playerCamera.js"
        override.write_text(PROJECT_CAMERA, encoding="utf-8")
        archive_override = root / "project-camera-override.db"
        generate(archive_override, scene.read_bytes(),
                 script.read_bytes(), override.read_bytes())
        for source in (None, archive_override):
            result = run(player, None, project, source, seconds=40)
            assert result["markers"]["scripting_start"]["camera"] == "first_person", (
                "Project camera script did not override sharedAssets through VFS")
            print(f"Bundled V8 project camera override {result['mode']}: PASS", flush=True)

        # A project can declare a scene player with NO character resources.
        # This verifies that the new root player lifecycle works without
        # requiring a game-specific model, independent of source/archive VFS.
        override.unlink()
        # New grouped override path must replace the shared camera class.
        grouped_camera = project / "scripts/camera/playerCamera.js"
        grouped_camera.parent.mkdir(parents=True, exist_ok=True)
        grouped_camera.write_text(PROJECT_CAMERA, encoding="utf-8")
        grouped_result = run(player, None, project, None, seconds=40)
        assert grouped_result["markers"]["scripting_start"]["camera"] == "first_person", (
            "Grouped project camera override was not selected")
        print("Bundled V8 grouped camera override source: PASS", flush=True)
        grouped_camera.unlink()

        definition = (
            "NorthStar.player.configure({enabled:true,eyeHeight:1.45,"
            "character:null,interaction:{enabled:false}});\n"
        )
        config = project / "scripts/playerConfig.js"
        config.write_text(definition, encoding="utf-8")
        config_archive = root / "project-player-config.db"
        generate(config_archive, scene.read_bytes(), script.read_bytes(),
                 config_override=config.read_bytes())
        for source in (None, config_archive):
            result = run(player, None, project, source, seconds=40)
            marker = result["markers"]["scripting_start"]
            assert marker["playerEnabled"] is True and marker["playerIndex"] >= 0, (
                "Declarative root player was not found in the scene")
            print(f"Bundled V8 declarative player {result['mode']}: PASS", flush=True)

        # When a source project provides both paths, grouped wins.
        old_config = project / "scripts/playerConfig.js"
        old_config.write_text("NorthStar.player.configure({enabled:false});\n",
                              encoding="utf-8")
        grouped_config = project / "scripts/player/playerConfig.js"
        grouped_config.parent.mkdir(parents=True, exist_ok=True)
        grouped_config.write_text(definition, encoding="utf-8")
        grouped_result = run(player, None, project, None, seconds=40)
        assert grouped_result["markers"]["scripting_start"]["playerEnabled"] is True, (
            "Grouped project config did not override the legacy path")
        print("Bundled V8 grouped player config precedence: PASS", flush=True)


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
