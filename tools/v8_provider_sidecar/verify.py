#!/usr/bin/env python3
"""Verify installed official d8 + NorthStarCpp C ABI development V8 provider.

This smoke/provenance verification is not a cryptographic security approval
or replacement for a signed clean-Windows release acceptance report.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def sha(path: Path) -> str:
    h=hashlib.sha256()
    with path.open("rb") as src:
        for block in iter(lambda:src.read(1024*1024),b""):
            h.update(block)
    return h.hexdigest()


def run() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--e2e",action="store_true",
                        help="Also run actual Player source and FAT/DB archive lifecycles")
    opts=parser.parse_args()
    root=Path(__file__).resolve().parents[2]
    folder=root/"out/v8-official-windows/15.8.62"
    manifest=json.loads((folder/"sidecar-provenance.json").read_text(encoding="utf-8"))
    pieces={
        "archive_sha256":folder/"v8-win64-rel-15.8.62.zip",
        "d8_sha256":folder/"bin/d8.exe",
        "worker_sha256":root/"tools/v8_provider_sidecar/v8_worker.js",
        "provider_sha256":folder/"sidecar-build/scripting_v8_sidecar.dll",
    }
    for name,path in pieces.items():
        if not path.is_file() or sha(path)!=manifest.get(name):
            raise RuntimeError("V8 official archive/provider integrity mismatch: "+str(path))
    d8=pieces["d8_sha256"]
    provider=pieces["provider_sha256"]
    worker=pieces["worker_sha256"]
    version=subprocess.run([str(d8),"-e","print(version())"],
        cwd=d8.parent,capture_output=True,text=True,timeout=15)
    if version.returncode or version.stdout.strip()!="15.8.62":
        raise RuntimeError("V8 d8 version probe mismatch: "+version.stdout)
    build=root/"out/distribution/gcc-release-nvkqwpwq"
    smoke=build/"northstarcpp_scripting_smoke.exe"
    fixture=build/"test_modules/script_service_fixture.dll"
    if not smoke.is_file() or not fixture.is_file():
        raise RuntimeError("The original NorthStarCpp C ABI smoke or service fixture is missing")
    env=os.environ.copy()
    env.update(NORTHSTARCPP_SCRIPTING_PLUGIN=str(provider.resolve()),
               NORTHSTARCPP_V8_D8_EXECUTABLE=str(d8.resolve()),
               NORTHSTARCPP_V8_SIDECAR_WORKER=str(worker.resolve()),
               NORTHSTARCPP_SCENE_SCRIPT_PLUGIN=str(fixture.resolve()),
               NORTHSTARCPP_FIXTURE_SCRIPT_PLUGIN=str(fixture.resolve()))
    results={}
    for kind,command in [
        ("engine_scripting_real_v8",[str(smoke),"v8",str(provider),
                                      str(root/"tests/fixtures")]),
        ("engine_scripting_services",[str(smoke),"service-v8"]),
    ]:
        proc=subprocess.run(command,cwd=build,env=env,capture_output=True,
                            text=True,errors="replace",timeout=90)
        results[kind]={"passed":proc.returncode==0,
                       "exit":proc.returncode,"stderr":proc.stderr[-2000:]}
        if proc.returncode:
            raise RuntimeError(kind+" failed: "+proc.stderr[-2500:])
    if opts.e2e:
        from tempfile import TemporaryDirectory
        from shutil import copytree
        sys.path.insert(0,str(root))
        from tools.v8_provider.archive_fixture import generate
        from tools.v8_provider.player_e2e import run as e2e_run
        player=root/("out/qa-fps-dist-20261114/"
                     "NorthStarCpp-0.4.0-windows-x64-Release-QA/"
                     "bin/northstarcpp_player.exe")
        if not player.is_file():
            raise RuntimeError("Installed QA Player missing")
        with TemporaryDirectory(prefix="northstar-official-v8-e2e-") as tmp:
            project=Path(tmp)/"project"
            copytree(root/"Projects/NorthStarFirstPersonPlatform",project)
            script=project/"assets/scripts/main.js"
            script.parent.mkdir(parents=True,exist_ok=True)
            script.write_text(
                '(() => { "use strict"; let frames=0;'
                'engine.on("start",()=>engine.save.write("scripting_start",'
                'JSON.stringify({phase:"start",objects:engine.scene.objectCount()})));'
                'engine.on("update",(dt)=>{frames++; if(frames===4){'
                'engine.save.write("scripting_update",'
                'JSON.stringify({phase:"update",frames,dt}));'
                'engine.session.quit();}});'
                'engine.on("stop",()=>engine.save.write("scripting_stop",'
                'JSON.stringify({phase:"stop",frames})));'
                '})();\n',encoding="utf-8")
            archive=Path(tmp)/"scripting.db"
            generate(archive,(project/"main.nscene").read_bytes(),script.read_bytes())
            # The script-service fixture belongs ONLY to C ABI unit tests.
            # Do not inject its overridden methods into a real game session.
            player_env=env.copy()
            player_env.pop("NORTHSTARCPP_SCENE_SCRIPT_PLUGIN",None)
            player_env.pop("NORTHSTARCPP_FIXTURE_SCRIPT_PLUGIN",None)
            old=os.environ.copy()
            os.environ.clear()
            os.environ.update(player_env)
            try:
                for kind,which in (("player_source",None),("player_archive",archive)):
                    results[kind]=e2e_run(player,provider,project,which,seconds=90)
            finally:
                os.environ.clear()
                os.environ.update(old)
    report={
        "classification":"OFFICIAL_V8_D8_DEVELOPER_SIDECAR_NOT_RELEASE_APPROVED",
        "v8_version":"15.8.62",
        "official_archive_sha256":manifest["archive_sha256"],
        "results":results,
        "release_approved":False,
        "security_approval":False,
    }
    report_path=folder/"verify-report.json"
    report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",
                           encoding="utf-8")
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 0


if __name__=="__main__":
    try:sys.exit(run())
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        print("V8 DEVELOPER VERIFICATION FAILED: "+str(exc),file=sys.stderr)
        sys.exit(2)
