"""NorthStarCpp release readiness gate.

No network, no modifications to shipping artifacts. All checks are fail-closed.
The V8 stable-branch attestation must be supplied from a trusted, reviewed
upstream build record (never inferred from the SDK path alone).
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET


def ninja_tree_is_current(build: Path) -> tuple[bool,str]:
    """Reject stale build trees even when an old passing CTest log exists."""
    if not (build / "build.ninja").is_file():
        return False,"Missing Ninja dependency graph"
    try:
        result=subprocess.run(
            ["ninja","-C",str(build),"-n","all"],
            capture_output=True,text=True,errors="replace",timeout=45)
        if result.returncode:
            return False,"Ninja graph validation failed"
        pending=re.findall(r"^\[\d+/\d+\]",result.stdout,re.M)
        if pending:
            return False,f"{len(pending)} pending Ninja steps; old binaries/tests are stale"
        return True,"No pending Ninja targets"
    except (OSError,subprocess.TimeoutExpired):
        return False,"Cannot verify Ninja graph freshness"


def ctest_results(build: Path) -> dict:
    p = subprocess.run(
        ["ctest", "--test-dir", str(build), "-N"],
        capture_output=True, text=True, errors="replace", timeout=60
    )
    if p.returncode:
        raise RuntimeError("CTest discovery failed: " + p.stderr[-1200:])
    return {"discovery_ok": True, "output": p.stdout[-200:]}


def parse_v8_version(path: Path) -> str:
    values = {}
    source = path.read_text("utf-8")
    for name, val in re.findall(
        r"#define\s+V8_(MAJOR_VERSION|MINOR_VERSION|BUILD_NUMBER|PATCH_LEVEL)\s+(\d+)",
        source
    ):
        values[name] = val
    required = ("MAJOR_VERSION", "MINOR_VERSION", "BUILD_NUMBER", "PATCH_LEVEL")
    if not all(name in values for name in required):
        raise ValueError("V8 version header incomplete")
    return ".".join(values[name] for name in required)


def verified_v8_attestation(attestation: Path, version: str,
                            provider: Path) -> tuple[bool, str]:
    """Verify offline release approval with a repository-trusted Ed25519 key.

    Source JSON claims alone are never acceptable. Release engineering must
    provision and review the PUBLIC key; the private key stays offline.
    """
    pubkey = (Path(__file__).resolve().parent / "v8_provider" /
              "trust" / "release_approval_ed25519.pub.pem")
    signature = attestation.with_name(attestation.name + ".sig")
    if not pubkey.is_file() or not signature.is_file():
        return False, "Missing trusted release public key or detached signature"
    if pubkey.is_symlink() or attestation.is_symlink() or signature.is_symlink():
        return False, "Redirected security attestation input"
    try:
        key_type = subprocess.run(
            ["openssl", "pkey", "-pubin", "-in", str(pubkey),
             "-text", "-noout"],
            capture_output=True, text=True, errors="replace", timeout=12)
        if key_type.returncode or "ED25519" not in (key_type.stdout + key_type.stderr).upper():
            return False, "Trusted approval key must be Ed25519"
        sig = subprocess.run(
            ["openssl", "pkeyutl", "-verify", "-pubin", "-inkey", str(pubkey),
             "-rawin", "-in", str(attestation), "-sigfile", str(signature)],
            capture_output=True, text=True, errors="replace", timeout=12)
        if sig.returncode:
            return False, "V8 attestation signature verification failed"
        data = json.loads(attestation.read_text("utf-8"))
        expiry = dt.datetime.fromisoformat(
            str(data.get("valid_until", "")).replace("Z", "+00:00"))
        if expiry.tzinfo is None:
            return False, "Unsigned timezone in approval expiry"
        fingerprint = hashlib.sha256(provider.read_bytes()).hexdigest()
        valid = (
            data.get("approved") is True and
            data.get("v8_version") == version and
            data.get("provider_sha256") == fingerprint and
            data.get("verified_stable_branch") is True and
            re.fullmatch(r"[0-9a-f]{40}", str(data.get("source_revision", ""))) is not None and
            expiry > dt.datetime.now(dt.timezone.utc)
        )
        return (bool(valid),
                "Valid signed release approval" if valid else
                "Signed approval expired or mismatched the binary/source")
    except (OSError, ValueError, TypeError, subprocess.TimeoutExpired,
            json.JSONDecodeError):
        return False, "Invalid or unverifiable V8 security attestation"


def verified_clean_machine_report(report: Path, package: Path) -> tuple[bool, str]:
    """Verify independent clean-machine QA signed for the exact install manifest.

    A successful smoke on a development PC is not equivalent to a clean VM.
    A separately provisioned release key signs the raw guest output after a
    reviewer validates the VM's provenance.
    """
    key = (Path(__file__).resolve().parent / "v8_provider" / "trust" /
           "clean_machine_ed25519.pub.pem")
    signature = report.with_name(report.name + ".sig")
    manifest = package / "manifest.json"
    if not all(p.is_file() for p in (key,signature,report,manifest)):
        return False, "Signed clean-machine report or installed package manifest missing"
    if any(p.is_symlink() for p in (key,signature,report,manifest)):
        return False, "Redirected clean-machine evidence rejected"
    try:
        pub=subprocess.run(
            ["openssl","pkey","-pubin","-in",str(key),"-text","-noout"],
            capture_output=True,text=True,errors="replace",timeout=12)
        if pub.returncode or "ED25519" not in (pub.stdout+pub.stderr).upper():
            return False, "Untrusted or non-Ed25519 clean-machine public key"
        sig=subprocess.run(
            ["openssl","pkeyutl","-verify","-pubin","-inkey",str(key),
             "-rawin","-in",str(report),"-sigfile",str(signature)],
            capture_output=True,text=True,errors="replace",timeout=12)
        if sig.returncode:
            return False, "Clean-machine report signature verification failed"
        data=json.loads(report.read_text("utf-8-sig"))
        tested=dt.datetime.fromisoformat(
            str(data.get("tested_utc","")).replace("Z","+00:00"))
        now=dt.datetime.now(dt.timezone.utc)
        if tested.tzinfo is None or tested>now+dt.timedelta(minutes=5) or (
                now-tested>dt.timedelta(days=14)):
            return False, "Clean-machine evidence is stale or has invalid time"
        digest=hashlib.sha256(manifest.read_bytes()).hexdigest()
        expected=("independent_virtual_machine","developer_toolchains_absent",
                  "manifest_sha256","native_editor","qt_tools",
                  "source_project_gameplay","archive_project_gameplay")
        checked=data.get("checks",{})
        valid=(data.get("schema")==1 and
               data.get("classification")=="unsigned_clean_windows_qa_evidence" and
               data.get("release_approved") is False and
               data.get("qa_passed") is True and
               data.get("tested_with_developer_path") is False and
               data.get("requires_independent_release_signature") is True and
               data.get("package_manifest_sha256")==digest and
               isinstance(data.get("machine_name"),str) and
               bool(data["machine_name"].strip()) and
               isinstance(checked,dict) and
               all(checked.get(name) is True for name in expected))
        return valid, ("Signed clean Windows QA matches exact installed package"
                       if valid else "Signed clean-machine report does not meet acceptance")
    except (OSError,ValueError,TypeError,subprocess.TimeoutExpired):
        return False,"Invalid or unverifiable clean-machine report"


def checked_process_isolation(package: Path) -> tuple[bool, str]:
    """Check signed-stage Qt/V8 private DLL layout without merging namespaces."""
    try:
        qt=json.loads((package/"qt.runtime.json").read_text("utf-8"))
        v8=json.loads((package/"v8.runtime.json").read_text("utf-8"))
        if qt.get("layout") != "process-isolated":
            return False,"Qt DLLs are not isolated in bin/qt_tools"
        if not (package/"bin/scripting_v8.dll").is_file():
            return False,"Native scripting provider missing"
        qt_files=qt.get("dlls",{})
        v8_files=v8.get("dlls",{})
        if not isinstance(qt_files,dict) or not isinstance(v8_files,dict) or (
                not qt_files or not v8_files):
            return False,"Incomplete private runtime manifests"
        for prefix,entries in (("bin/qt_tools/",qt_files),("bin/",v8_files)):
            for name,detail in entries.items():
                if not isinstance(name,str) or not name.startswith(prefix) or (
                        "\\" in name or ".." in Path(name).parts):
                    return False,"Unsafe dependency manifest path"
                path=package/name
                if (not path.is_file() or path.is_symlink() or
                        path.stat().st_size != detail.get("size") or
                        hashlib.sha256(path.read_bytes()).hexdigest() != detail.get("sha256")):
                    return False,"Missing or invalid private runtime DLL"
        if any((package/"bin"/name).exists() for name in (
                "Qt6Core.dll","Qt6Gui.dll","Qt6Widgets.dll")):
            return False,"Qt DLLs remain in the native engine process"
        return True,"Qt and V8 DLLs are isolated and content-verified"
    except (OSError,ValueError,TypeError,AttributeError):
        return False,"Cannot verify process-isolated installed runtime"


def evaluate(build: Path, sdk: Path, provider: Path, attestation: Path | None,
             clean_machine_report: Path | None = None,
             installed_package: Path | None = None) -> dict:
    checks = {}
    def check(name: str, ok: bool, detail: str) -> None:
        checks[name] = {"pass": bool(ok), "detail": detail}

    cache = build / "CMakeCache.txt"
    clean_log = build / "full-clean-build.log"
    clean_output = clean_log.read_text("utf-8", errors="replace") if clean_log.is_file() else ""
    counts = re.findall(r"\[(\d+)/(\d+)\]", clean_output)
    complete_clean = (
        bool(counts) and counts[-1][0] == counts[-1][1] and
        int(counts[-1][1]) >= 50 and
        "northstarcpp_editor" in clean_output and
        "northstarcpp_player" in clean_output)
    build_current,build_reason=ninja_tree_is_current(build)
    check("ninja_build_current",build_current,build_reason)
    check("clean_release_configuration",
          cache.is_file() and "CMAKE_BUILD_TYPE:STRING=Release" in
          cache.read_text(errors="replace") and complete_clean and build_current,
          "Complete fresh Release Ninja graph and clean build log required")
    exe = build / "northstarcpp_player.exe"
    editor = build / "northstarcpp_editor.exe"
    check("player_and_editor", exe.is_file() and editor.is_file(),
          "Both release executables are present")
    check("v8_provider", provider.is_file(),
          "Native V8 provider exists")
    header = sdk / "include" / "v8-version.h"
    version = parse_v8_version(header) if header.is_file() else "unavailable"
    check("v8_version", header.is_file(), "SDK header V8 " + version)

    approval = False
    details = "Missing signed V8 stable-branch security attestation"
    if attestation and attestation.is_file() and provider.is_file():
        approval, details = verified_v8_attestation(attestation, version, provider)
    check("v8_security_approval", approval, details)

    deps = build / "v8-qt-runtime-collision-report.json"
    report = json.loads(deps.read_text("utf-8")) if deps.is_file() else {}
    conflicts = report.get("conflicts", [])
    isolated_package_ok=False
    isolated_package_reason="No installed package to validate"
    if installed_package:
        isolated_package_ok,isolated_package_reason = checked_process_isolation(installed_package)
    check("qt_v8_dll_compatibility",
          isolated_package_ok or (
              deps.is_file() and report.get("safe_to_merge") is True and not conflicts),
          isolated_package_reason if installed_package else
          str(len(conflicts)) + " mismatched dependencies; never merge with Qt")
    ctest_log = build / "release-full-ctest-latest.log"
    if not ctest_log.is_file():
        ctest_log = build / "release-full-ctest.log"
    text = ctest_log.read_text("utf-8", errors="replace") if ctest_log.is_file() else ""
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
    # A previously passing CTest run cannot authorize a modified source tree.
    full = bool(match) and int(match.group(2)) == 0 and (
        int(match.group(3)) > 0) and build_current
    if ctest_log.is_file() and (build / "build.ninja").is_file():
        full = full and ctest_log.stat().st_mtime >= (
            build / "build.ninja").stat().st_mtime
    check("full_ctest", full,
          (match.group(0) + (" (stale build not accepted)" if not full else ""))
          if match else "No complete CTest result")
    installed = build / "installed-stage-v8-e2e.json"
    try:
        marker = json.loads(installed.read_text("utf-8"))
        passed = {x.get("mode") for x in marker["results"] if x.get("pass")}
        good = passed >= {"source", "archive"}
    except (OSError, ValueError, KeyError, TypeError):
        good = False
    check("installed_staging_gameplay", good, "Source and archive E2E required")
    qa = build / "qa-process-isolated-v3"
    try:
        isolation = json.loads((qa / "process-isolation-report.json").read_text("utf-8"))
        samples = json.loads((qa / "qa-updated-binaries-checks.json").read_text("utf-8"))
        isolated = (isolation.get("classification") == "UNSAFE_V8_QA_ONLY" and
                    isolation.get("import_closure_pass") is True and
                    all(isinstance(item,dict) and item.get("pass") is True
                        for item in samples.values()))
    except (OSError, ValueError, TypeError):
        isolated = False
    # Successful process isolation is an engineering result, not proof of
    # current V8 security or a production installation.
    check("process_isolation_qa", isolated or isolated_package_ok,
          "Qt/V8 process isolation verified for staged QA or installed package"
          if isolated or isolated_package_ok else
          "No validated isolated Qt/V8 package")
    # A staging pass is not a clean-machine installation pass. Only
    # cryptographically reviewed evidence tied to a specific package can pass.
    clean_ok=False
    clean_reason="No independently signed clean-machine acceptance artifact"
    if clean_machine_report and installed_package:
        clean_ok,clean_reason=verified_clean_machine_report(
            clean_machine_report,installed_package)
    check("clean_machine_install",clean_ok,clean_reason)
    return {
        "schema": 1, "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "release_approved": all(x["pass"] for x in checks.values()),
        "build": str(build), "v8_version": version,
        "checks": checks
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", type=Path, required=True)
    ap.add_argument("--sdk", type=Path, required=True)
    ap.add_argument("--provider", type=Path, required=True)
    ap.add_argument("--v8-attestation", type=Path)
    ap.add_argument("--clean-machine-report", type=Path)
    ap.add_argument("--installed-package", type=Path)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    report = evaluate(args.build,args.sdk,args.provider,args.v8_attestation,
                      args.clean_machine_report,args.installed_package)
    output = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(output, encoding="utf-8")
    print(output)
    return 0 if report["release_approved"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
