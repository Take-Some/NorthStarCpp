#!/usr/bin/env python3
"""A/B engine Player FPS benchmark with V8, using a temporary scene project.

Reports: clamped engine.on(update) delta, plus wall time per actual frame.
Does NOT instrument Vulkan GPU timestamps or true unclamped CPU frame time.
"""
from __future__ import annotations
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/"out/v8-official-windows/15.8.62"
PLAYER=ROOT/("out/qa-fps-dist-20261114/"
             "NorthStarCpp-0.4.0-windows-x64-Release-QA/bin/northstarcpp_player.exe")
SOURCE=ROOT/"Projects/NorthStarFirstPersonPlatform"
sys.path.insert(0,str(ROOT))
from tools.v8_provider.player_e2e import read_marker


def percentile(values: list[float], fract: float) -> float:
    ordered=sorted(values)
    import math
    return ordered[math.ceil(len(ordered)*fract)-1]


def run_mode(mode: str, frames: int, native_calls: int) -> dict:
    with tempfile.TemporaryDirectory(prefix=f"v8-player-{mode}-") as tmp:
        temp=Path(tmp)
        project=temp/"project"
        shutil.copytree(SOURCE,project)
        script=project/"assets/scripts/main.js"
        script.parent.mkdir(parents=True,exist_ok=True)
        # Every mode has the same per-frame V8 update registration and
        # sample persistence. Only the optional engine.* call workload differs.
        code=f"""(() => {{
    "use strict";
    const frameDeltas=[];
    let received=0;
    let checksum=0;
    engine.on("update", dt => {{
        received++;
        if(received>30)frameDeltas.push(dt*1000);
        for(let i=0;i<{native_calls};i++) checksum+=engine.scene.objectCount();
        if(received>={frames}+30)engine.session.quit();
    }});
    engine.on("stop",()=>engine.save.write("frame_benchmark",
        JSON.stringify({{mode:{json.dumps(mode)},samples:frameDeltas,
                        frames:received,checksum}})));
}})();"""
        script.write_text(code+"\n",encoding="utf-8")
        appdata=temp/"AppData"
        appdata.mkdir()
        env=os.environ.copy()
        env.update(
            LOCALAPPDATA=str(appdata),
            NORTHSTARCPP_SCRIPTING_PLUGIN=str((WORK/"sidecar-build/scripting_v8_sidecar.dll").resolve()),
            NORTHSTARCPP_V8_D8_EXECUTABLE=str((WORK/"bin/d8.exe").resolve()),
            NORTHSTARCPP_V8_SIDECAR_WORKER=str((ROOT/"tools/v8_provider_sidecar/v8_worker.js").resolve()))
        start=time.perf_counter()
        completed=subprocess.run([str(PLAYER.resolve()),"--warp","--fps",
                                  "--scene",str((project/"main.nscene").resolve())],
                                 cwd=PLAYER.parent,env=env,capture_output=True,
                                 text=True,errors="replace",timeout=135)
        wall_s=time.perf_counter()-start
        if completed.returncode:
            raise RuntimeError(f"{mode} Player failed code {completed.returncode}:"
                               f" {completed.stderr[-2100:]}")
        if any(x in completed.stderr for x in ["Project script update failed",
                    "Unhandled project script error"]):
            raise RuntimeError(f"{mode} Player script failed: {completed.stderr[-900:]}")
        result=read_marker(appdata,"frame_benchmark")
        samples=result["samples"]
        if len(samples)!=frames or result["frames"]!=frames+30:
            raise RuntimeError(f"{mode}: incomplete frame sampling: {len(samples)}")
        expected=frames+30
        if result["checksum"] != native_calls*40*expected:
            raise RuntimeError(f"{mode}: native API call checksum differs")
        return {
            "mode":mode,"script_native_calls_per_frame":native_calls,
            "frames":frames,"warmup_frames":30,
            "observed_delta_p50_ms":round(percentile(samples,.5),4),
            "observed_delta_p95_ms":round(percentile(samples,.95),4),
            "observed_delta_p99_ms":round(percentile(samples,.99),4),
            "observed_delta_mean_ms":round(statistics.mean(samples),4),
            "clamped_50ms_samples":sum(x>=49.99 for x in samples),
            "wall_seconds":round(wall_s,4),
            "wall_avg_ms_per_frame_including_startup":round(
                 wall_s*1000/(frames+30),4),
            "checksum":result["checksum"],
        }


def main() -> int:
    if not PLAYER.is_file():
        raise RuntimeError("Installed QA Player is missing")
    frames=1000
    modes=[("baseline",0),("host_bridge_32_calls",32)]
    result=[]
    for mode,calls in modes:
        print("Running",mode,"for",frames,"sampled Player frames",flush=True)
        value=run_mode(mode,frames,calls)
        print(mode,json.dumps(value,ensure_ascii=False),flush=True)
        result.append(value)
    report={
        "classification":"WARP_PLAYER_V8_AB_COMPARISON_CLAMPED_SIMULATION_DT",
        "source_scene":"NorthStarFirstPersonPlatform/main.nscene",
        "player":str(PLAYER.resolve()),
        "v8":"15.8.62",
        "render_backend":"D3D11 WARP",
        "frame_dt_limit_ms":50,
        "notes":"engine.on(update) dt is clamped to 50 ms in FPS mode; this is not raw frame time or Vulkan GPU timestamp. Two runs are sequential and uncontrolled for system load.",
        "results":result,
        "release_approved":False,
    }
    file=WORK/"player-frame-benchmark.json"
    file.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print("PASS: real Player frame delta comparison saved",file,flush=True)
    return 0


if __name__=="__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print("BENCHMARK FAILED:",repr(e),file=sys.stderr)
        sys.exit(1)
