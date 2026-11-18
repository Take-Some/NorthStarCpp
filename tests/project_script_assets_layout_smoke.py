"""Project scripts are physical assets and retain stable VFS references."""
from __future__ import annotations
from pathlib import Path
import importlib.util
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("northstar_game_export",ROOT/"tools/build_game.py")
assert spec and spec.loader
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

PROJECTS=["AbbyPistolDemo","KaylaNightCity","NorthStarFirstPersonPlatform"]
for name in PROJECTS:
    project=ROOT/"Projects"/name
    sources=project/"assets"/"scripts"
    assert sources.is_dir() and (sources/"main.js").is_file(),name
    assert not (project/"scripts").exists(),name
    # Source backups may exist; the game exporter must exclude them.
    assert all(file.is_file() for file in sources.rglob("*.js")),name
    print("project script assets:",name,len(list(sources.rglob("*.js"))))

with tempfile.TemporaryDirectory(prefix="northstar-script-assets-") as tmp:
    temporary=Path(tmp)
    project=temporary/"Project"
    scriptdir=project/"assets"/"scripts"
    (scriptdir/"player").mkdir(parents=True)
    (scriptdir/"main.js").write_text("export {};\n",encoding="utf-8")
    (scriptdir/"player"/"camera.mjs").write_text("export {};\n",encoding="utf-8")
    (scriptdir/"main.js.bak-20261118").write_text("archived version",encoding="utf-8")
    (project/"main.nscn").write_text(
        "script 1 scripts/player/camera.mjs\n",encoding="utf-8")
    stage=temporary/"Game"
    asset_count=module.copy_game_assets(project,stage,project/"main.nscn")
    script_count=module.copy_game_scripts(project,stage,project/"main.nscn")
    assert asset_count==2 and script_count==2,(asset_count,script_count)
    assert (stage/"assets"/"scripts"/"main.js").is_file()
    assert (stage/"assets"/"scripts"/"player"/"camera.mjs").is_file()
    assert not (stage/"assets"/"scripts"/"main.js.bak-20261118").exists()
    assert not (stage/"scripts").exists()
    # Older source projects remain loadable and export into the canonical VFS.
    legacy=temporary/"Legacy"
    (legacy/"scripts").mkdir(parents=True)
    (legacy/"scripts"/"main.js").write_text("export {};\n",encoding="utf-8")
    (legacy/"main.nscn").write_text(
        "script 1 scripts/main.js\n",encoding="utf-8")
    oldgame=temporary/"LegacyGame"
    count=module.copy_game_scripts(legacy,oldgame,legacy/"main.nscn")
    assert count==1
    assert (oldgame/"assets"/"scripts"/"main.js").is_file()
    assert not (oldgame/"scripts").exists()

print("PASS project script assets / packaging / backward compatibility")
