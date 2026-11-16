#!/usr/bin/env python3
"""Compile the authoritative engine API scripts into immutable C++ resources.

Only current *.js and *.d.ts are embedded. No backups, source maps, or user
project scripts are published. Input bytes and ordering are deterministic.
"""
from __future__ import annotations

import argparse
import pathlib
import hashlib

EXTENSIONS = (".js", ".d.ts")
LIMIT_BYTES = 1024 * 1024

def resources(root: pathlib.Path):
    result = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        if not path.name.endswith(EXTENSIONS):
            continue
        rel = path.relative_to(root).as_posix()
        if any(part.startswith(".") or part == ".." for part in pathlib.PurePosixPath(rel).parts):
            raise ValueError(f"Invalid script path: {rel}")
        data = path.read_bytes()
        if not data or len(data) > LIMIT_BYTES:
            raise ValueError(f"Script size outside 1..{LIMIT_BYTES}: {rel}")
        data.decode("utf-8")  # reject invalid sources during build
        result.append(("scripts/" + rel, data))
    if not result:
        raise ValueError("No engine API scripts to embed")
    return result

def generate(values: list[tuple[str, bytes]]) -> str:
    out = [
        "// Generated; DO NOT EDIT. Source: engine/northstarcpp/resources/api/.",
        "#include <northstarcpp/embedded_script_api.hpp>",
        "#include <algorithm>",
        "#include <iterator>",
        "namespace northstarcpp::embedded_api {",
        "namespace {"
    ]
    for i, (_, data) in enumerate(values):
        out.append(f"static constexpr unsigned char script_{i}[] = {{")
        for start in range(0, len(data), 24):
            out.append("    " + ",".join(f"0x{byte:02x}" for byte in data[start:start+24]) + ",")
        out.append("};")
    out.append("static constexpr Script script_entries[] = {")
    for i, (name, data) in enumerate(values):
        out.append(f'    {{ "{name}", std::string_view{{reinterpret_cast<char const*>(script_{i}), {len(data)}}} }},')
    out.extend([
        "};",
        "} // namespace",
        "Script const* entries() noexcept { return script_entries; }",
        "std::size_t count() noexcept { return sizeof(script_entries)/sizeof(script_entries[0]); }",
        "Script const* find(std::string_view key) noexcept {",
        "    auto const first=std::begin(script_entries);",
        "    auto const last=std::end(script_entries);",
        "    auto const it=std::lower_bound(first,last,key,",
        "        [](Script const& entry,std::string_view name){return entry.virtual_path<name;});",
        "    return it!=last && it->virtual_path==key ? it : nullptr;",
        "}",
        "} // namespace northstarcpp::embedded_api",
    ])
    # Notes: constexpr variable initializers use reinterpret_cast, which is not
    # allowed for constant evaluation in C++17. Use static const instead.
    result = "\n".join(out) + "\n"
    return result.replace("static constexpr Script script_entries[]", "static const Script script_entries[]")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--verify", action="store_true")
    opts = parser.parse_args()
    values = resources(opts.root)
    data = generate(values).encode("utf-8")
    if opts.verify:
        if not opts.out.is_file() or opts.out.read_bytes() != data:
            raise SystemExit("Embedded API generation is out of date")
    else:
        opts.out.parent.mkdir(parents=True, exist_ok=True)
        if not opts.out.is_file() or opts.out.read_bytes() != data:
            opts.out.write_bytes(data)
    print(f"Engine API generated: {len(values)} files; sha256={hashlib.sha256(data).hexdigest()}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
