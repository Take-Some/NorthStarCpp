#!/usr/bin/env python3
"""Build a disposable, process-isolated Qt/V8 QA stage.

This is NOT an approved release, and cannot write to dist. It deliberately
permits a *known outdated* V8 for regression testing, never for publishing.
The Qt GUI tools are separate Windows processes with private CRT/ICU DLLs;
Player/Editor run in bin with the V8 runtime. No DLL is silently overwritten.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import os
import re

QT_PROGRAMS = (
    "northstarcpp_project_manager.exe",
    "northstarcpp_asset_browser.exe",
    "northstarcpp_camera_timeline_editor.exe",
)
QT_OPTIONAL_PROGRAMS = ("northstarcpp_editor_qt.exe",)
V8_RUNTIME = (
    "scripting_v8.dll", "libv8.dll", "libv8_libbase.dll",
    "libv8_libplatform.dll", "libgcc_s_seh-1.dll",
    "libstdc++-6.dll", "libwinpthread-1.dll",
    "libicuin78.dll", "libicuuc78.dll", "libicudt78.dll", "zlib1.dll",
)
SYSTEM = {
    "advapi32.dll", "authz.dll", "bcrypt.dll", "comctl32.dll",
    "comdlg32.dll", "crypt32.dll", "dbghelp.dll", "dwmapi.dll",
    "gdi32.dll", "gdiplus.dll", "imm32.dll", "iphlpapi.dll",
    "kernel32.dll", "mpr.dll", "msvcrt.dll", "netapi32.dll",
    "ntdll.dll", "ole32.dll", "oleaut32.dll", "rpcrt4.dll", "secur32.dll",
    "setupapi.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "uxtheme.dll", "version.dll", "winmm.dll",
    "ws2_32.dll", "wsock32.dll", "wtsapi32.dll", "normaliz.dll",
    "d3d11.dll", "dxgi.dll", "d3dcompiler_47.dll", "d3d12.dll",
    "powrprof.dll", "winhttp.dll", "ws2help.dll", "vcruntime140.dll",
    "ucrtbase.dll", "dwrite.dll", "d2d1.dll", "dxva2.dll",
    "windows.storage.dll", "cfgmgr32.dll", "bcryptprimitives.dll",
    "usp10.dll", "d3d9.dll", "shcore.dll",
    "combase.dll", "wintrust.dll", "userenv.dll", "iphlpapi.dll",
}

def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda:f.read(1024*1024),b""):
            h.update(block)
    return h.hexdigest()

def pe_imports(path: Path, objdump: Path) -> set[str]:
    proc=subprocess.run([str(objdump),"-p",str(path)],
                        capture_output=True,text=True,errors="replace",timeout=20)
    if proc.returncode:
        raise RuntimeError("PE import scan failed: "+str(path))
    return {
        line.partition("DLL Name:")[2].strip().lower()
        for line in proc.stdout.splitlines() if "DLL Name:" in line
    }

def assert_imports(folder: Path, objdump: Path, *, exclude: str = "") -> dict[str,list[str]]:
    """Check each process's private import graph; no developer PATH fallback."""
    all_files={str(p.relative_to(folder)).replace("\\","/"):p
               for p in folder.rglob("*") if p.is_file() and
               p.suffix.lower() in (".dll",".exe") and
               (not exclude or exclude not in p.relative_to(folder).parts)}
    main={p.name.casefold():p for p in folder.iterdir()
          if p.is_file() and p.suffix.lower() in (".dll",".exe")}
    unresolved={}
    for rel,file in all_files.items():
        missing=sorted(x for x in pe_imports(file,objdump)
            if x not in main and x not in SYSTEM and
            not x.startswith(("api-ms-win-","ext-ms-win-")))
        if missing:
            unresolved[rel]=missing
    return unresolved

def create_stage(source: Path, runtime: Path, target: Path, objdump: Path) -> dict:
    source=source.resolve(strict=True)
    runtime=runtime.resolve(strict=True)
    target=target.resolve(strict=False)
    if target.exists():
        raise RuntimeError("Refusing to overwrite an existing QA directory: "+str(target))
    if "dist" in {p.lower() for p in target.parts}:
        raise RuntimeError("Never stage unsafe QA runtimes into dist")
    if target == source or source in target.parents or target in source.parents:
        raise RuntimeError("QA output must be separate from input package")
    manifest=json.loads((source/"qt.runtime.json").read_text("utf-8"))
    qt_entries=manifest.get("dlls",{})
    if not qt_entries or manifest.get("runtime")!="Qt6Widgets":
        raise RuntimeError("Missing audited Qt manifest")
    qt_names={str(Path(k).name).casefold() for k in qt_entries}
    # Verify every recorded Qt binary before making any copy.
    for key,record in qt_entries.items():
        p=source/key
        if not p.is_file() or p.is_symlink() or sha256(p)!=record.get("sha256"):
            raise RuntimeError("Invalid Qt manifest entry: "+key)
    for name in V8_RUNTIME:
        p=runtime/name
        if not p.is_file() or p.is_symlink():
            raise RuntimeError("Missing V8 runtime DLL: "+name)
    for name in QT_PROGRAMS:
        if not (source/"bin"/name).is_file():
            raise RuntimeError("Qt executable missing: "+name)
    programs = QT_PROGRAMS + tuple(name for name in QT_OPTIONAL_PROGRAMS
                                   if (source/"bin"/name).is_file())
    # Only a disposable QA path; no in-place deploy, replacement, or production DLL.
    try:
        shutil.copytree(source,target,symlinks=False)
        bin_dir=target/"bin"
        qt=bin_dir/"qt_tools"
        qt.mkdir()
        for name in programs:
            (bin_dir/name).replace(qt/name)
        for relative in qt_entries:
            old=target/relative
            dst=qt/Path(relative).relative_to("bin")
            dst.parent.mkdir(parents=True,exist_ok=True)
            old.replace(dst)
        # V8's own CRT/ICU copies enter the engine process folder.
        # A collision in the root after moving Qt indicates unsafe mixed runtime.
        v8_hashes={}
        for name in V8_RUNTIME:
            src=runtime/name
            dst=bin_dir/name
            if dst.exists():
                if sha256(dst)!=sha256(src):
                    raise RuntimeError("Native engine runtime DLL collision: "+name)
            else:
                shutil.copy2(src,dst)
            v8_hashes[name]=sha256(dst)
        # No Qt runtime should be left in bin/ nor V8 core in qt_tools/.
        # The seven overlapping *names* intentionally exist in both
        # process-private folders. Confirm the ROOT holds V8 hashes, while
        # Qt-owned non-overlapping DLLs never enter the engine process.
        leftover_qt=sorted(p.name for p in bin_dir.iterdir()
                          if p.is_file() and p.name.casefold() in qt_names and
                          p.name not in V8_RUNTIME)
        if leftover_qt:
            raise RuntimeError("Qt-only DLLs still in engine directory: "+
                               ", ".join(leftover_qt))
        bad_qt=assert_imports(qt,objdump)
        bad_engine=assert_imports(bin_dir,objdump,exclude="qt_tools")
        report={
            "schema":1,"classification":"UNSAFE_V8_QA_ONLY",
            "approved_for_release":False,
            "layout":"Windows process-isolated native engine and Qt tools",
            "bin":str(bin_dir),"qt_tools":str(qt),
            "qt_tool_executables":list(programs),
            "qt_dlls":{k:v["sha256"] for k,v in qt_entries.items()},
            "v8_dlls":v8_hashes,
            "unresolved_qt_imports":bad_qt,"unresolved_engine_imports":bad_engine,
            "import_closure_pass":not (bad_qt or bad_engine),
            "security_approval":False,
            "notes":"V8 security gate must pass after SDK update; this stage cannot be shipped."
        }
        (target/"ISOLATION_QA_NOT_FOR_RELEASE.txt").write_text(
            "UNTRUSTED V8 QA ONLY. No redistribution; security approval missing.\n",
            encoding="utf-8")
        (target/"process-isolation-report.json").write_text(
            json.dumps(report,indent=2)+"\n",encoding="utf-8")
        if not report["import_closure_pass"]:
            raise RuntimeError("Unresolved runtime imports. See "+str(target/
                               "process-isolation-report.json"))
        return report
    except Exception:
        # Leave stage and report for forensics, never touch input or production DLLs.
        raise

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--source-stage",required=True,type=Path)
    p.add_argument("--v8-provider-bin",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    p.add_argument("--objdump",required=True,type=Path)
    args=p.parse_args()
    try:
        report=create_stage(args.source_stage,args.v8_provider_bin,args.output,args.objdump)
        print(json.dumps({"import_closure_pass":report["import_closure_pass"],
                          "qa_only":True,"report":str(args.output/
                              "process-isolation-report.json")},indent=2))
        return 0
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as exc:
        print("STAGING REJECTED:",exc)
        return 2

if __name__=="__main__":
    raise SystemExit(main())
