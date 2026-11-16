#!/usr/bin/env python3
"""Build isolated extra .nsp packages and validate discovered VFS precedence."""
from __future__ import annotations
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.pack_nsp import pack, verify

def main() -> int:
    if len(sys.argv) != 4:
        return 2
    binary = Path(sys.argv[1]).resolve()
    provider = Path(sys.argv[2]).resolve()
    base_pack = Path(sys.argv[3]).resolve()
    with tempfile.TemporaryDirectory(prefix="northstar-nsp-archive-") as tmp:
        root=Path(tmp)
        archives=root/"archive"
        archives.mkdir()
        shutil.copyfile(base_pack, archives/"northstar_shared.nsp")
        for name, content in (("alpha.nsp", b"first"), ("zeta.nsp", b"last")):
            source=root/("source-"+name)
            target=source/"data"/"pack_order.txt"
            target.parent.mkdir(parents=True)
            target.write_bytes(content)
            pack(source,archives/name)
            verify(archives/name)
        project=root/"project"
        (project/"data").mkdir(parents=True)
        (project/"data"/"pack_order.txt").write_bytes(b"project")
        env=os.environ.copy()
        env["NORTHSTARCPP_ASSETS_PLUGIN"]=str(provider)
        result=subprocess.run([str(binary),str(archives),str(project)],
                              env=env,capture_output=True,text=True,timeout=30)
        print(result.stdout)
        if result.stderr: print(result.stderr,file=sys.stderr)
        return result.returncode

if __name__=="__main__":
    sys.exit(main())
