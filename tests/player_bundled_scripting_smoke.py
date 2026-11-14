"""Exercise bundled V8 discovery and the real Player lifecycle."""
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.v8_provider.archive_fixture import generate
from tools.v8_provider.player_e2e import run

SCRIPT = """(() => {
    let frames = 0;
    const save = (slot, data) => engine.save.write(slot, JSON.stringify(data));
    engine.on("start", () => save("scripting_start",
        {phase: "start", objects: engine.scene.objectCount()}));
    engine.on("update", (dt) => {
        if (++frames === 4) {
            save("scripting_update", {phase: "update", frames, dt});
            engine.session.quit();
        }
    });
    engine.on("stop", () => save("scripting_stop", {phase: "stop", frames}));
})();"""


def verify(player: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="northstar-bundled-v8-") as temp:
        root = Path(temp)
        project = root / "project"
        shutil.copytree(ROOT / "Projects/NorthStarFirstPersonPlatform", project)
        script = project / "scripts/main.js"
        script.write_text(SCRIPT, encoding="utf-8")
        archive = root / "project.db"
        generate(archive, (project / "main.nscene").read_bytes(), script.read_bytes())
        for source in (None, archive):
            result = run(player, None, project, source, seconds=40)
            print(f"Bundled V8 {result['mode']} start/update/stop: PASS", flush=True)


if __name__ == "__main__":
    verify(Path(sys.argv[1]))
