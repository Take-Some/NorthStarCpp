#!/usr/bin/env python3
"""Pack every Mini-14 XVAG source into one verified NorthStar NEF8/NAC archive.

Reuses the existing XVAG -> PCM16 -> NEF8/NAC codec implementation from
pack_pistol_xvag.py; this tool only defines Mini-14 track identities and
the source/output paths.

Usage:
    python tools/pack_mini14_xvag.py
    python tools/pack_mini14_xvag.py --verify-only
    python tools/pack_mini14_xvag.py --decoder PATH
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

from pack_pistol_xvag import decoder_path, pack, verify

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "engine/northstarcpp/resources/sharedAssets/audio/weapon/mini14"
TARGET = SOURCE / "mini14.nac"

# Explicit, stable, short clip names for engine.audio.playNac. A source added
# without a corresponding identity fails the import instead of being omitted.
TRACKS = {
    "fire/WPN_Rifle_Lever_Semi_Fire_Player_01.xvag": "fire_01",
    "fire/WPN_VATS_RifleLever_Fire_Player_01.xvag": "fire_vats_01",
    "fire/WPN_VATS_RifleLever_Fire_Player_02.xvag": "fire_vats_02",
    "fire_suppressed/WPN_Rifle_Lever_Semi_Fire_Silenced_Player_01.xvag": "fire_suppressed_01",
    "reload/WPN_RifleLever_Reload_BoltClose_01.xvag": "reload_bolt_close_01",
    "reload/WPN_RifleLever_Reload_BoltOpen_01.xvag": "reload_bolt_open_01",
    "reload/WPN_RifleLever_Reload_Finish_01.xvag": "reload_finish_01",
    "reload/WPN_RifleLever_Reload_MagIn_01.xvag": "reload_mag_in_01",
    "reload/WPN_RifleLever_Reload_MagIn_02.xvag": "reload_mag_in_02",
    "reload/WPN_RifleLever_Reload_MagIn_03.xvag": "reload_mag_in_03",
    "reload/WPN_RifleLever_Reload_MagIn_04.xvag": "reload_mag_in_04",
    "reload/WPN_RifleLever_Reload_MagIn_05.xvag": "reload_mag_in_05",
    "reload/WPN_RifleLever_Reload_Start_01.xvag": "reload_start_01",
    "reload_composite.xvag": "reload_composite",
    "tails_indoor/WPN_TailInt_RifleLever_Semi_Player_ReverbA_01.xvag": "tail_indoor_a_01",
    "tails_indoor/WPN_TailInt_RifleLever_Semi_Player_ReverbB_01.xvag": "tail_indoor_b_01",
    "tails_indoor/WPN_TailInt_RifleLever_Semi_Player_ReverbC_01.xvag": "tail_indoor_c_01",
    "tails_indoor/WPN_TailInt_RifleLever_Semi_Player_ReverbD_01.xvag": "tail_indoor_d_01",
    "tails_indoor/WPN_TailInt_RifleLever_Semi_Player_ReverbE_01.xvag": "tail_indoor_e_01",
    "tails_outdoor/WPN_TailExt_RifleLever_Player_Default_01.xvag": "tail_outdoor_01",
    "tails_outdoor/WPN_TailExt_RifleLever_Player_Default_02.xvag": "tail_outdoor_02",
    "tails_outdoor/WPN_TailExt_RifleLever_Player_Default_03.xvag": "tail_outdoor_03",
}


class NamedSource:
    """Path adapter that assigns a stable short NAC track name to a source."""

    def __init__(self, path: Path, track_name: str) -> None:
        self.path = path
        self.stem = track_name

    @property
    def name(self) -> str:
        return self.path.name

    def __str__(self) -> str:
        return str(self.path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", help="Path to vgmstream-cli.exe")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()

    if not SOURCE.is_dir():
        raise RuntimeError(f"Missing source directory: {SOURCE}")
    present = {p.relative_to(SOURCE).as_posix() for p in SOURCE.rglob("*.xvag")}
    configured = set(TRACKS)
    if present != configured:
        raise RuntimeError(
            "Mini-14 XVAG source inventory mismatch: "
            f"new={sorted(present-configured)}, missing={sorted(configured-present)}"
        )
    if len(TRACKS) != len(set(TRACKS.values())):
        raise RuntimeError("Duplicate NAC track names")

    sources = [NamedSource(SOURCE / name, TRACKS[name]) for name in sorted(TRACKS)]
    blob, originals = pack(decoder_path(args.decoder), sources)
    expected = verify(blob, originals)

    if TARGET.exists():
        actual = verify(TARGET.read_bytes(), originals)
        print("EXISTS VALID, PCM MATCHES SOURCES", json.dumps(actual))
        return
    if args.verify_only:
        raise RuntimeError(f"Archive not found: {TARGET}")

    staged = SOURCE / "mini14.nac.stage"
    if staged.exists():
        raise RuntimeError(f"Staging file already exists: {staged}")
    try:
        with staged.open("xb") as stream:
            stream.write(blob)
            stream.flush()
            os.fsync(stream.fileno())
        actual = verify(staged.read_bytes(), originals)
        if actual != expected:
            raise RuntimeError("Staged NAC failed verification")
        if TARGET.exists():
            raise RuntimeError("Destination appeared during import; refusing overwrite")
        staged.rename(TARGET)
    finally:
        if staged.exists():
            staged.unlink()

    print("CREATED", TARGET)
    print("VERIFIED", json.dumps(expected))
    print("Sources preserved: original XVAG and mini14.yscd")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError,
            subprocess.SubprocessError) as exc:
        print(f"MINI14_NAC_ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
