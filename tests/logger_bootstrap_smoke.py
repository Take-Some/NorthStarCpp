"""Regression for engine.logger settings parsed from bootstrap.json by native C++.
Ensures the default console is hidden, and explicitly configured console opens
only when requested by the GUI host (the test itself never allocates a console).
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def check(condition: bool, why: str):
    if not condition:
        raise RuntimeError(why)


def main() -> None:
    parser=argparse.ArgumentParser()
    parser.add_argument("--executable",type=Path,required=True)
    parser.add_argument("--directory",type=Path,required=True)
    a=parser.parse_args()
    stage=a.directory.resolve()
    stage.mkdir(parents=True,exist_ok=True)
    target=stage/"logger_bootstrap_smoke.exe"
    shutil.copy2(a.executable.resolve(strict=True),target)
    config=stage/"bootstrap.json"

    def run(mode: str, data=None, env=None, expected: int=0, expect=""):
        if data is None:
            config.unlink(missing_ok=True)
        else:
            config.write_text(json.dumps(data),encoding="utf-8")
        options=os.environ.copy()
        options.pop("NORTHSTARCPP_LOG_DIRECTORY",None)
        options.pop("NORTHSTARCPP_LOGGER_PLUGIN",None)
        if env:options.update(env)
        cmd=[str(target),mode,str(stage/"logs/out")]
        result=subprocess.run(cmd,cwd=stage.parent,env=options,
                              capture_output=True,text=True,encoding="utf-8",
                              errors="replace",timeout=18)
        check(result.returncode==expected,
              f"{mode} failed: status={result.returncode}, stderr={result.stderr}")
        if expect:check(expect in result.stdout+result.stderr,
                        f"{mode} expected {expect!r} in result")
        print("PASS",mode)

    run("default")
    run("custom",{
        "plugins":{"path":"./"},
        "logging":{
            "enabled":True,"level":"warning","directory":"logs/out",
            "sinks":{"text":True,"structured":True,"debugger":False},
            "console":{"visible":True},
            "rotation":{"maxBytes":1048576,"backups":2}
        }
    })
    run("explicit_off",{"logging":{
        "console":{"visible":True},
        "sinks":{"console":False}
    }})
    run("disabled",{"logging":{"enabled":False,"sinks":{
        "console":False,"text":False,"structured":False,"debugger":False
    }}})
    run("override",{"logging":{"directory":"different"}},
        env={"NORTHSTARCPP_LOG_DIRECTORY":str(stage/"logs/out")})
    for bad in [
        {"logging":{"level":"supercritical"}},
        {"logging":{"rotation":{"maxBytes":1099511627776}}},
        {"logging":{"rotation":{"backups":17}}},
        {"logging":{"enabled":True,"sinks":{
            "text":False,"structured":False,"debugger":False,"console":False
        }}},
        {"logging":{"console":{"visible":"maybe"}}},
        {"logging":{"rotation":{"maxBytes":-1}}}
    ]:
        run("bad",bad)
    print("bootstrap.json logger configuration: all cases passed")


if __name__=="__main__":
    try:main()
    except Exception as exc:
        print("bootstrap logger test failed:",exc,file=sys.stderr)
        raise SystemExit(1)
