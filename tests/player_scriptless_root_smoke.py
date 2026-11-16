"""Player auto-activates engine root scripts without a project main.js."""
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.player_e2e import run

SESSION = r'''import { Events, Player, Scene } from "@northstar/engine";
let frames = 0;
const save = (name, data) => engine.save.write(name, JSON.stringify(data));
Events.on("start", () => {
    if (!Player.enabled || Player.index < 0 ||
        !Scene.get(Player.index)?.player)
        throw new Error("Scriptless scene did not activate authored player");
    save("scripting_start", {phase:"start",
        objects: Scene.objectCount(),
        playerEnabled:Player.enabled, index:Player.index});
});
Events.on("update", dt => {
    if (++frames === 5) {
        save("scripting_update", {phase:"update",frames,dt});
        engine.session.quit();
    }
});
Events.on("stop", () => {
    save("scripting_stop", {phase:"stop",frames});
});
'''

def verify(player):
    with tempfile.TemporaryDirectory(prefix="northstar-scriptless-root-") as tmp:
        project = Path(tmp) / "project"
        shutil.copytree(ROOT / "Projects/NorthStarFirstPersonPlatform", project)
        (project / "scripts/main.js").unlink()
        # The shared default config must discover the authored player
        # rather than relying on a project-local Player.configure() script.
        (project / "scripts/playerConfig.js").unlink()
        root_script = project / "scripts/player/playerSession.js"
        root_script.parent.mkdir(parents=True, exist_ok=True)
        root_script.write_text(SESSION, encoding="utf-8")
        output = run(Path(player), None, project, seconds=35)
        marker = output["markers"]["scripting_start"]
        if not marker["playerEnabled"] or marker["index"] < 0:
            raise AssertionError(f"Scriptless player not initialized: {marker}")
        print("PASS: native Player loaded root lifecycle without scripts/main.js")

if __name__ == "__main__":
    verify(sys.argv[1])
