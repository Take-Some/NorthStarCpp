#!/usr/bin/env python3
"""Resolve the current Windows Stable V8 source and pin its remote branch tip.

This tool produces SOURCE PROVENANCE ONLY. It does not build, audit or approve
binary SDKs. The returned revision must be built, tested and security reviewed.
"""
from __future__ import annotations
import argparse
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import urllib.request

MILESTONES="https://chromiumdash.appspot.com/fetch_milestones?only_active=true"
RELEASES=("https://chromiumdash.appspot.com/fetch_releases?"
          "channel=Stable&platform=Windows&num=10")
UPSTREAM="https://chromium.googlesource.com/v8/v8.git"

def fetch(url: str) -> list:
    request=urllib.request.Request(
        url,headers={"User-Agent":"NorthStarCpp-SourceProvenance/1.0"})
    with urllib.request.urlopen(request,timeout=22) as response:
        content=response.read(1_000_001)
    if len(content)>1_000_000:
        raise ValueError("Chromiumdash response exceeded size limit")
    parsed=json.loads(content)
    if not isinstance(parsed,list):
        raise ValueError("Unexpected Chromiumdash response")
    return parsed

def resolve() -> dict:
    milestones=fetch(MILESTONES)
    releases=fetch(RELEASES)
    latest=max((r for r in releases
                if r.get("channel")=="Stable" and
                isinstance(r.get("milestone"),int) and
                isinstance(r.get("hashes"),dict) and
                re.fullmatch(r"[0-9a-f]{40}",r["hashes"].get("v8",""))),
               key=lambda r:(r["milestone"],r.get("time",0)))
    milestone=next((x for x in milestones
                    if x.get("milestone")==latest["milestone"]),None)
    if not milestone:
        raise ValueError("Stable milestone not present in current metadata")
    branch=milestone.get("v8_branch")
    if not isinstance(branch,str) or not re.fullmatch(r"\d{1,2}\.\d{1,3}",branch):
        raise ValueError("Invalid stable V8 branch")
    ref="refs/branch-heads/"+branch
    result=subprocess.run(["git","ls-remote",UPSTREAM,ref],
                          capture_output=True,text=True,
                          errors="replace",timeout=35)
    if result.returncode:
        raise RuntimeError("Cannot resolve official V8 branch: "+result.stderr[-250:])
    matched=re.fullmatch(r"([0-9a-f]{40})\s+"+re.escape(ref)+r"\s*",result.stdout)
    if not matched:
        raise RuntimeError("Official V8 branch did not resolve to an exact commit")
    return {
        "schema":1,"purpose":"source_pin_not_a_security_approval",
        "release_approved":False,
        "fetched_utc":dt.datetime.now(dt.timezone.utc).isoformat(),
        "chrome_channel":"Windows Stable (includes early stable)",
        "chrome_milestone":latest["milestone"],
        "chrome_version":latest.get("version"),
        "chrome_released_v8_commit":latest["hashes"]["v8"],
        "v8_stable_branch":ref,
        "v8_stable_tip_commit":matched.group(1),
        "source_git":UPSTREAM,
        "references":[MILESTONES,RELEASES,UPSTREAM],
        "requires":"Full source checkout/deps, reproducible Windows build, ABI tests, cryptographic artifact attestation, security review",
    }

def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    record=resolve()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(record,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(record,indent=2))
    return 0

if __name__=="__main__":
    raise SystemExit(main())
