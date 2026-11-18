#!/usr/bin/env python3
"""Build the protected V8 entrypoint from independently authored SDK parts.

The parts compile into one lexical scope; evaluating them separately would
break class references and the protected NorthStar bootstrap contract.
"""
from __future__ import annotations
import argparse
import json
import pathlib
import re

def compose(root: pathlib.Path, verify: bool = False) -> bool:
    manifest = root / "manifest.json"
    if not manifest.is_file() or manifest.is_symlink():
        return False
    descriptor = json.loads(manifest.read_text("utf-8"))
    names = descriptor["modules"]
    if not isinstance(names, list) or not names:
        raise ValueError("SDK manifest must declare a nonempty ordered module list")
    if descriptor.get("schemaVersion") != 1:
        raise ValueError("Unsupported SDK manifest schema version")
    chunks = []
    seen = set()
    for name in names:
        # Root-relative source, category/name.js, not a runtime VFS script.
        if (not isinstance(name, str) or
            not re.fullmatch(r"[a-z][a-z_]*[/][a-z][a-z_]*[.]js", name) or
            name in seen):
            raise ValueError(f"Invalid or duplicate SDK module: {name!r}")
        seen.add(name)
        path = root / pathlib.PurePosixPath(name)
        if not path.is_file() or path.is_symlink() or path.parent.is_symlink():
            raise FileNotFoundError(path)
        data = path.read_bytes()
        if not data or len(data) > 1024 * 1024:
            raise ValueError(f"Invalid SDK module length: {name}")
        data.decode("utf-8")
        chunks.append(data)
    generated=b"".join(chunks)
    if (not generated.startswith(b"// NorthStarCpp") or
        not generated.rstrip().endswith(b"})();")):
        raise ValueError("Invalid V8 bootstrap boundary")
    output=root / "northstar" / "bootstrap.js"
    if verify:
        if not output.is_file() or output.read_bytes()!=generated:
            raise ValueError("Bootstrap is stale: run SDK module composer")
    elif not output.is_file() or output.read_bytes()!=generated:
        output.write_bytes(generated)
    return True

def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--verify", action="store_true")
    args=parser.parse_args()
    if not compose(args.root, verify=args.verify):
        raise SystemExit("Missing NorthStar bootstrap manifest")
    print("NorthStar SDK bootstrap:", "verified" if args.verify else "composed")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
