#!/usr/bin/env python3
"""Player/Editor provider resolution parity without opening a game window."""
import os
import subprocess
import sys
from pathlib import Path

def run(player: Path, override: Path | None) -> dict[str, list[dict]]:
    env=os.environ.copy()
    env.pop("NORTHSTARCPP_GEOMETRY_PLUGIN", None)
    if override:
        env["NORTHSTARCPP_GEOMETRY_PLUGIN"]=str(override)
    process=subprocess.run(
        [str(player), "--inspect-providers"], cwd=player.parent,
        env=env, capture_output=True, text=True, timeout=30)
    if process.returncode:
        raise AssertionError(f"Player inspection failed {process.returncode}: {process.stderr}")
    lines=process.stdout.strip().splitlines()
    assert lines and lines[0].split("\t")==[
        "Capability","Requested","Provider","Version","ABI","Source","Status"
    ], "Unexpected provider report columns"
    results: dict[str, list[dict]]={}
    for line in lines[1:]:
        values=line.split("\t")
        assert len(values)==7, f"Malformed provider row: {line}"
        capability,requested,provider,version,abi,source,status=values
        results.setdefault(capability, []).append({
            "requested":requested,"provider":provider,"version":version,
            "abi":int(abi),"source":source,"status":status})
    return results

def main() -> int:
    player=Path(sys.argv[1]).resolve()
    good=Path(sys.argv[2]).resolve()
    bad=Path(sys.argv[3]).resolve()
    builtin=run(player,None)
    for capability in ("engine.geometry","engine.collision","engine.time","engine.perlin"):
        row=builtin[capability][0]
        assert row["source"]=="builtin" and row["status"]=="OK", (capability,row)
    assert {x["requested"] for x in builtin["engine.render"]}=={"d3d11","d3d12"}
    external=run(player,good)["engine.geometry"][0]
    assert external["provider"]=="Fixture Geometry" and external["status"]=="OK"
    assert str(good).lower()==external["source"].lower(), external
    invalid=run(player,bad)["engine.geometry"][0]
    assert invalid["status"]!="OK" and invalid["source"]=="" and not invalid["provider"], invalid
    assert "Incompatible module" in invalid["status"], invalid
    print("Player provider report: builtin, author override and rejected ABI PASS")
    return 0

if __name__=="__main__":
    try:sys.exit(main())
    except Exception as error:
        print(f"Player provider report regression: {error}",file=sys.stderr)
        sys.exit(1)
