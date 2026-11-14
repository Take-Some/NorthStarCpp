"""Offline smoke for the source-built Windows V8 SDK pipeline.

Does not download V8, require a compiler, or pretend that d8 is an SDK.
"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
BUILDER = REPO / "tools/v8_provider/build_official_sdk.py"
VERIFIER = REPO / "tools/v8_provider/verify_official_sdk.py"
PIN = {
    "purpose": "source_pin_not_a_security_approval",
    "source_git": "https://chromium.googlesource.com/v8/v8.git",
    "v8_stable_branch": "refs/branch-heads/15.8",
    "v8_stable_tip_commit": "19c06df29e0050413f5f041888f4c990de970314",
}


def require(condition, reason):
    if not condition:
        raise AssertionError(reason)


with tempfile.TemporaryDirectory(prefix="v8-official-sdk-pipeline-") as d:
    root = Path(d)
    pin = root / "pin.json"
    pin.write_text(json.dumps(PIN), encoding="utf-8")
    planned = root / "build-plan.json"
    sdk = root / "v8-sdk"
    work = root / "build"
    command = [
        sys.executable, str(BUILDER), "--pin", str(pin),
        "--work-dir", str(work), "--sdk-dir", str(sdk),
        "--mode", "plan", "--plan-output", str(planned),
    ]
    result = subprocess.run(command, capture_output=True, text=True, timeout=18)
    require(result.returncode == 0, "Pinned SDK build plan failed: " + result.stderr)
    plan = json.loads(planned.read_text("utf-8"))
    require(plan["source_commit"] == PIN["v8_stable_tip_commit"],
            "Source SHA not captured")
    require(len(plan["commands"]) == 6, "Missing official source-build stages")
    require(plan["commands"][-1]["argv"][-2:] == ["v8_monolith", "d8"],
            "Builder does not create embeddable V8 monolith")
    require(plan["gn_args"]["v8_monolithic"] is True and
            plan["gn_args"]["is_component_build"] is False and
            plan["gn_args"]["v8_use_external_startup_data"] is False and
            plan["gn_args"]["use_custom_libcxx"] is False and
            plan["gn_args"]["v8_use_snapshot"] is True,
            "Monolithic static linking GN flags missing")
    require(plan["release_approved"] is False, "Unattested SDK treated as released")
    require(not sdk.exists() and not work.exists(),
            "Plan mode must never write an SDK or download sources")
    # The old official d8 archive is NOT an embeddable C++ SDK.
    sdk.mkdir()
    (sdk / "d8.exe").write_bytes(b"MZ")
    bad = subprocess.run([sys.executable, str(VERIFIER), "--sdk", str(sdk)],
                         capture_output=True, text=True, timeout=12)
    require(bad.returncode != 0, "Unvalidated d8-only runtime accepted as SDK")
    # Build tool must refuse to write into the production directory.
    unsafe = command.copy()
    unsafe[unsafe.index("--sdk-dir") + 1] = str(REPO / "dist" / "V8")
    rejected = subprocess.run(unsafe, capture_output=True, text=True, timeout=12)
    require(rejected.returncode != 0, "SDK builder accepted production dist output")
print("V8 15.8 official-source SDK build pipeline: PASS")
