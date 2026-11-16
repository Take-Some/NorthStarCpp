import subprocess, os, sys, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
exe=Path(sys.argv[1])
env=os.environ.copy()
env["NORTHSTARCPP_PHYSICS_PROVIDER"]="jolt"
env["NORTHSTARCPP_RENDER_PROVIDER"]="d3d12"
cmd=[str(exe),"--warp","--fps","--scene",str(root/"Projects/NorthStarFirstPersonPlatform/main.nscn")]
with tempfile.TemporaryDirectory(prefix="northstar-fp-debug-",ignore_cleanup_errors=True) as tmp:
    env["NORTHSTARCPP_LOG_DIRECTORY"]=str(Path(tmp)/"logs")
    (Path(tmp)/"logs").mkdir()
    a=Path(tmp)/"out.txt"; b=Path(tmp)/"err.txt"
    with a.open("wb") as stdout,b.open("wb") as stderr:
        p=subprocess.Popen(cmd,cwd=exe.parent,env=env,stdout=stdout,stderr=stderr)
        try:
            p.wait(timeout=8)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait(timeout=8)
    print("RETURN",p.returncode)
    print("OUT",a.read_text(encoding="utf-8",errors="replace")[-2000:])
    print("ERR",b.read_text(encoding="utf-8",errors="replace")[-8000:])
    for file in sorted((Path(tmp)/"logs").rglob("*")):
        if file.is_file() and file.suffix in (".log",".ulog",".json"):
            print("LOGFILE",file.name)
            lines=file.read_text(encoding="utf-8",errors="replace").splitlines()
            print("ERRORS",*[s for s in lines if "ERROR" in s or "FAIL" in s or "Exception" in s][-18:],sep="\n")
            print("TAIL",*lines[-12:],sep="\n")
