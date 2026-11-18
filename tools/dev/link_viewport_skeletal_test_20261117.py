"""Link the already compiled skeletal smoke objects while unrelated runtime targets are dirty.

Uses Ninja's emitted linker command; does not rebuild runtime/vehicle sources.
QA-only workaround for shared out/build dependency churn; no shipping output.
"""
import subprocess
from pathlib import Path

root=Path(__file__).resolve().parents[2]
build=root/"out"/"build"
target="northstarcpp_viewport_skeletal_pipeline_smoke"
query=subprocess.run(["ninja","-C",str(build),"-t","commands",target],
    check=True,capture_output=True,text=True,errors="replace",timeout=25)
lines=[line for line in query.stdout.splitlines()
    if "northstarcpp_viewport_skeletal_pipeline_smoke.exe" in line
    and " -o " in line]
if not lines:
    raise RuntimeError("No linker command found for the existing target")
print("Linking already-built object and libraries; not rebuilding dirty runtime...")
result=subprocess.run(lines[-1],shell=True,cwd=build,
    capture_output=True,text=True,errors="replace",timeout=75)
print(result.stdout[-2500:],result.stderr[-2500:])
if result.returncode:
    raise SystemExit(result.returncode)
scene=build/"northstarcpp_viewport_skeletal_pipeline_smoke.exe"
assert scene.is_file()
run=subprocess.run([str(scene),
    str(root/"engine"/"northstarcpp"/"resources"/"sharedAssets")],
    cwd=root,capture_output=True,text=True,errors="replace",timeout=50)
for line in (run.stderr+run.stdout).replace("\\n","\n").splitlines():
    if ("[TPS SOCKET BASIS]" in line or "[ACTUAL TPS PISTOL]" in line
        or "Skeletal viewport pipeline:" in line):
        print(line[:500])
print("smoke exit",run.returncode)
if run.returncode:
    raise SystemExit(run.returncode)
