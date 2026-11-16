"""Regenerate IDE typings from the runtime input classes.

Source of truth:
    engine/northstarcpp/resources/sharedAssets/api/input/Keys.js
    engine/northstarcpp/resources/sharedAssets/api/input/GamepadButtons.js

Do not edit the input enum declarations in northstar-engine.d.ts directly.
Usage: python tools/dev/generate_input_typings.py [--check]
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
INPUT_DIR = ROOT / "engine" / "northstarcpp" / "resources" / "sharedAssets" / "api" / "input"
DECLARATIONS = ROOT / "engine" / "northstarcpp" / "resources" / "sharedAssets" / "api" / "types" / "northstar-engine.d.ts"

BEGIN = "    // BEGIN GENERATED INPUT ENUMS: tools/dev/generate_input_typings.py"
END = "    // END GENERATED INPUT ENUMS"


def extract_values(filename: str, class_name: str, maximum: int) -> list[tuple[str, int]]:
    content = (INPUT_DIR / filename).read_text(encoding="utf-8-sig")
    match = re.search(
        rf"^\s*class {re.escape(class_name)}\s*\{{(?P<body>.*?)^\s*\}}",
        content,
        re.MULTILINE | re.DOTALL,
    )
    if not match:
        raise ValueError(f"{filename}: class {class_name} not found")
    body = match.group("body")
    entries = re.findall(
        r"^\s*static\s+([A-Za-z_$][A-Za-z0-9_$]*)\s*=\s*"
        r"(0[xX][0-9a-fA-F]+|[0-9]+)\s*;\s*$",
        body,
        re.MULTILINE,
    )
    residual = re.sub(
        r"^\s*static\s+[A-Za-z_$][A-Za-z0-9_$]*\s*=\s*"
        r"(?:0[xX][0-9a-fA-F]+|[0-9]+)\s*;\s*$",
        "",
        body,
        flags=re.MULTILINE,
    )
    if residual.strip() or not entries:
        raise ValueError(f"{filename}: unexpected class members; update the typings generator")
    result = [(name, int(value, 0)) for name, value in entries]
    names = [name for name, _ in result]
    if len(set(names)) != len(names):
        raise ValueError(f"{filename}: duplicate input symbol name")
    if any(value <= 0 or value > maximum for _, value in result):
        raise ValueError(f"{filename}: input code outside expected range")
    return result


def generated_section() -> str:
    keyboard = extract_values("Keys.js", "Keys", 0xFF)
    gamepad = extract_values("GamepadButtons.js", "GamepadButtons", 0xFFFF)
    lines = [
        BEGIN,
        "    /** Keyboard Virtual-Key codes, generated from input/Keys.js. */",
        "    export class Keys {",
    ]
    lines.extend(f"        static readonly {name}: 0x{value:02X};" for name, value in keyboard)
    lines.extend([
        "    }",
        "",
        "    /** Gamepad bitmasks, generated from input/GamepadButtons.js. */",
        "    export class GamepadButtons {",
    ])
    lines.extend(
        f"        static readonly {name}: 0x{value:04X};"
        for name, value in gamepad
    )
    lines.extend(["    }", END, ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail if generated IDE declarations are stale")
    args = parser.parse_args()

    content = DECLARATIONS.read_text(encoding="utf-8-sig")
    section = generated_section()
    if BEGIN in content or END in content:
        if content.count(BEGIN) != 1 or content.count(END) != 1:
            raise ValueError("Generated input declarations have damaged markers")
        start = content.index(BEGIN)
        end = content.index(END, start) + len(END)
        updated = content[:start] + section.rstrip("\n") + content[end:]
    else:
        start = content.index("    /** Virtual-key codes")
        end = content.index("    export interface GamepadState", start)
        updated = content[:start] + section + "\n" + content[end:]
    if updated == content:
        print("Input IDE declarations are synchronized with their JavaScript classes")
        return 0
    if args.check:
        print(
            "Input declarations are stale. Run: "
            "python tools/dev/generate_input_typings.py",
            file=sys.stderr,
        )
        return 1
    DECLARATIONS.write_text(updated, encoding="utf-8")
    print(f"Generated {DECLARATIONS.relative_to(ROOT)} from input/Keys.js and input/GamepadButtons.js")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
