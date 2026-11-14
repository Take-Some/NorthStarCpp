#!/usr/bin/env python3
"""Preflight official pinned Windows V8 build; never approves a release.

No install/download and no mutation of the shipping tree. Validates that the
local SDK build can start and writes an explicit list of blockers. Upstream
source pin must come from resolve_stable_source.py, not a version string.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

MINIMUM_FREE_GIB = 30  # NorthStar release-engineering precaution, not a V8 spec.

def check_pin(path: Path) -> dict:
    value=json.loads(path.read_text("utf-8"))
    revision=value.get("v8_stable_tip_commit")
    branch=value.get("v8_stable_branch")
    if (value.get("purpose")!="source_pin_not_a_security_approval" or
            value.get("source_git")!="https://chromium.googlesource.com/v8/v8.git" or
            not isinstance(branch,str) or
            not re.fullmatch(r"refs/branch-heads/15[.]8",branch) or
            not isinstance(revision,str) or
            not re.fullmatch(r"[a-f0-9]{40}",revision)):
        raise ValueError("Unsigned, unsupported or malformed V8 15.8 source pin")
    return value

def inspect(pin_path: Path, build_root: Path, *, check_remote=False) -> dict:
    pin=check_pin(pin_path)
    free=shutil.disk_usage(build_root.anchor or "C:/").free
    # GN/clang are frequently fetched via depot_tools; report unavailable
    # locally even if later setup can provision them.
    bins={name:shutil.which(name) for name in
          ("git","fetch","gclient","gn","ninja","clang-cl","cl","cmake")}
    vc=Path("C:/Program Files (x86)/Microsoft Visual Studio")
    vc_detected=vc.exists() and any(vc.rglob("cl.exe"))
    check={
        "disk_space_30_gib":free >= MINIMUM_FREE_GIB*1024**3,
        "git":bool(bins["git"]),
        "depot_tools_fetch":bool(bins["fetch"]),
        "depot_tools_gclient":bool(bins["gclient"]),
        "gn_generator":bool(bins["gn"]),
        "ninja":bool(bins["ninja"]),
        "windows_cpp_toolchain":bool(bins["clang-cl"] or bins["cl"] or vc_detected),
    }
    remote=None
    if check_remote:
        if not bins["git"]:
            check["verified_upstream_sha"]=False
        else:
            result=subprocess.run(
                [bins["git"],"ls-remote",pin["source_git"],pin["v8_stable_branch"]],
                capture_output=True,text=True,errors="replace",timeout=35)
            remote=result.stdout.split()[0] if result.returncode==0 and result.stdout else None
            check["verified_upstream_sha"]=(remote==pin["v8_stable_tip_commit"])
    # Production engine is compiled with MinGW64 while its existing V8
    # provider is UCRT64 GNU C++ ABI. An official GN/clang-cl V8 .lib cannot
    # simply replace that import library. This must be checked AFTER the
    # SDK build and before release, not as a prerequisite for fetching source.
    return {
        "schema":1,"purpose":"V8_15_8_BUILD_PREREQUISITES_NOT_RELEASE_APPROVAL",
        "generated_utc":dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_branch":pin["v8_stable_branch"],
        "source_commit":pin["v8_stable_tip_commit"],
        "remote_commit":remote,
        "minimum_free_gib_policy":MINIMUM_FREE_GIB,
        "available_free_gib":round(free/(1024**3),2),
        "tool_paths":bins,
        "checks":check,
        "ready_for_full_build":all(check.values()),
        "post_build_gates":{
            "v8_sdk_provider_abi_validated":False,
            "native_host_and_archive_tests":False,
            "security_attestation_signed":False,
            "independent_clean_vm":False
        },
        "build_plan":[
            "Install supported depot_tools and Windows C++ toolchain on a separate sufficiently sized builder",
            "fetch v8 from Chromium upstream using depot_tools (not git clone alone)",
            "checkout reviewed stable commit and run gclient sync",
            "build d8 and V8 libraries using official GN/Ninja workflow",
            "test d8 and V8; package exact headers/libraries + source commit + dependency hashes",
            "compile ABI-compatible NorthStar scripting_v8 provider against new SDK",
            "run real host ABI and project archive/native tests",
            "submit independent signed security approval before packaging",
        ],
        "release_approved":False,
    }

def main(argv=None)->int:
    p=argparse.ArgumentParser()
    p.add_argument("--pin",type=Path,required=True)
    p.add_argument("--build-root",type=Path,default=Path("C:/"))
    p.add_argument("--verify-upstream",action="store_true")
    p.add_argument("--output",type=Path)
    args=p.parse_args(argv)
    result=inspect(args.pin,args.build_root,check_remote=args.verify_upstream)
    contents=json.dumps(result,indent=2,ensure_ascii=False)+"\n"
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(contents,encoding="utf-8")
    print(contents)
    return 0 if result["ready_for_full_build"] else 2

if __name__=="__main__":
    raise SystemExit(main())
