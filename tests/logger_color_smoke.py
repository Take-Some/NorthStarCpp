"""Verify ANSI console colors, monochrome redirects and file isolation."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(exe: Path, directory: Path, extra: dict[str,str]):
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    env=os.environ.copy()
    env.pop("NORTHSTARCPP_LOGGER_PLUGIN",None)
    env.pop("NO_COLOR",None)
    env.pop("FORCE_COLOR",None)
    env.pop("CLICOLOR",None)
    env.pop("CLICOLOR_FORCE",None)
    env.update(extra)
    p=subprocess.run([str(exe),"colors",str(directory)],cwd=exe.parent,
                     env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                     timeout=30)
    if p.returncode:
        raise RuntimeError(f"Logger color probe returned {p.returncode}: {p.stderr[-900:]!r}")
    logs=list(directory.glob("*.log"))
    ulogs=list(directory.glob("*.ulog"))
    if len(logs)!=1 or len(ulogs)!=1:
        raise RuntimeError("Expected one clean .log and one JSON .ulog")
    text=logs[0].read_bytes()
    structured=ulogs[0].read_bytes()
    if b"\x1b[" in text or b"\x1b[" in structured:
        raise RuntimeError("Terminal ANSI color sequence leaked into log files")
    rows=[json.loads(line) for line in structured.splitlines()]
    if len(rows)!=8:
        raise RuntimeError(f"Expected 8 structured records, got {len(rows)}")
    if rows[6]["message"]!="text with \x1b[31m escape":
        raise RuntimeError("Structured log unexpectedly lost source message")
    if b"\x1b[31m" in text:
        raise RuntimeError("Injected control sequence leaked to readable text log")
    if b"second line" not in text or b"first line" not in text:
        raise RuntimeError("Multiline messages missing")
    return p.stderr,text


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--executable",type=Path,required=True)
    parser.add_argument("--directory",type=Path,required=True)
    arg=parser.parse_args()
    exe=arg.executable.resolve(strict=True)
    folder=arg.directory.resolve()
    plain,txt=run(exe,folder/"plain",{})
    if b"\x1b[" in plain:
        raise RuntimeError("ANSI color emitted into redirected stderr by default")
    force,force_txt=run(exe,folder/"force",{"FORCE_COLOR":"1"})
    if b"\x1b[38;2;96;200;230m" not in force:
        raise RuntimeError("Time cyan ANSI color missing")
    if b"\x1b[38;2;122;136;155m" not in force:
        raise RuntimeError("Bracket gray ANSI color missing")
    if b"\x1b[1;38;2;104;218;155mINFO" not in force:
        raise RuntimeError("INFO status ANSI color missing")
    if b"\x1b[1;37;41mFATAL" not in force:
        raise RuntimeError("FATAL standout style missing")
    if b"\x1b[38;2;226;232;242m" not in force:
        raise RuntimeError("Message color missing")
    if b"\x1b[38;2;190;164;250m" not in force:
        raise RuntimeError("Channel color missing")
    if b"\x1b[31m" in force:
        raise RuntimeError("Injected ANSI sequence reached console")
    clean,clean_txt=run(exe,folder/"no_color",
                        {"NO_COLOR":"1","FORCE_COLOR":"1"})
    if b"\x1b[" in clean:
        raise RuntimeError("NO_COLOR ignored even with forced color")
    if txt!=force_txt or txt!=clean_txt:
        # Files differ in timestamps / PIDs; compare color independent payload.
        for item in (txt,force_txt,clean_txt):
            if b"fatal sample" not in item:
                raise RuntimeError("Console styling changed file payload")
    print("PASS: color palette, forced ANSI, NO_COLOR, redirected plain, sanitized input, clean log files")


if __name__=="__main__":
    try:main()
    except Exception as e:
        print("FAIL:",e,file=sys.stderr)
        raise SystemExit(1)
