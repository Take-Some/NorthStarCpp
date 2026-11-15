"""End-to-end WARP smoke: corrupted optional assets warn once, scene survives."""
from __future__ import annotations
import subprocess
import sys
from pathlib import Path

exe=Path(sys.argv[1]).resolve()
shared=Path(sys.argv[2]).resolve()
result=subprocess.run([str(exe),str(shared)],cwd=exe.parent,
                      capture_output=True,text=True,errors="replace",timeout=45)
if result.returncode:
    raise RuntimeError(
        f"Optional asset recovery failed ({result.returncode}):\n"
        f"{result.stdout}\n{result.stderr}")
for name in ("materials/corrupt.ymat","models/corrupt.ydd@broken",
             "world_surfaces_runtime.ytd"):
    lines=[line for line in result.stderr.splitlines()
           if "[WARN] [engine.assets]" in line and name in line]
    if len(lines)!=1:
        raise RuntimeError(
            f"Expected exactly one warning for {name}; got {len(lines)}:\n"
            f"{result.stderr}")
if "survived 3 WARP frames" not in result.stdout:
    raise RuntimeError("Valid sibling objects did not render in three frames")
print("Malformed optional material, mesh, terrain dictionary: WARN once each, 3 frames PASS")
