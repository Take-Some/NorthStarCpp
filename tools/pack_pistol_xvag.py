#!/usr/bin/env python3
"""Convert pistol XVAG clips into the current NorthStar NEF8/NAC audio archive.

Uses vgmstream for PlayStation ADPCM -> PCM16 decoding. This does not modify
or remove the XVAG sources or pistol.yscd. Run from any working directory:
    python tools/pack_pistol_xvag.py [--decoder PATH] [--verify-only]
Requires the Python blake3 package and the vendor vgmstream-cli executable.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
import zlib

from blake3 import blake3

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "sharedAssets" / "audio" / "weapon" / "pistol"
TARGET = SOURCE / "pistol.nac"
HEADER = struct.Struct("<4sHHHHHHQQQQQQ")
MAX_BODY = 64 * 1024 * 1024


def stable_id(name: str) -> int:
    result = 2166136261
    for byte in name.encode("utf-8"):
        result = ((result ^ byte) * 16777619) & 0xFFFFFFFF
    return result


def decoder_path(specified: str | None) -> str:
    if specified:
        return specified
    on_path = shutil.which("vgmstream-cli")
    if on_path:
        return on_path
    bundled = (ROOT.parent / "Tools" / "toolbelt" / "third_party"
               / "vgmstream" / "bin" / "vgmstream-cli.exe")
    if bundled.is_file():
        return str(bundled)
    raise RuntimeError("vgmstream-cli is not available; specify --decoder")


def decode_xvag(decoder: str, source: Path, output: Path) -> bytes:
    proc = subprocess.run(
        [decoder, "-i", "-W", "1", "-o", str(output), str(source)],
        text=True, capture_output=True, check=False,
        timeout=90,
    )
    if proc.returncode != 0 or not output.is_file():
        raise RuntimeError(
            f"XVAG decoding failed for {source.name}: "
            + (proc.stderr or proc.stdout).strip()
        )
    with wave.open(str(output), "rb") as wav:
        channels = wav.getnchannels()
        rate = wav.getframerate()
        frames = wav.getnframes()
        if (wav.getcomptype() != "NONE" or wav.getsampwidth() != 2
                or channels not in (1, 2) or not 8000 <= rate <= 192000
                or not frames):
            raise RuntimeError(f"Unsupported PCM WAV output: {source.name}")
        pcm = wav.readframes(frames)
    if len(pcm) != frames * channels * 2:
        raise RuntimeError(f"Truncated PCM samples: {source.name}")
    return pcm, rate, channels, frames


def pack(decoder: str, files: list[Path]) -> tuple[bytes, dict[str, bytes]]:
    body = bytearray()
    records = []
    originals = {}
    ids = set()
    with tempfile.TemporaryDirectory(prefix="northstar-pistol-wav-") as temp:
        for path in files:
            name = path.stem
            if not 0 < len(name.encode("utf-8")) <= 47:
                raise RuntimeError(f"Invalid NAC track name: {name}")
            tid = stable_id(name)
            if tid == 0 or tid in ids:
                raise RuntimeError(f"Duplicate NAC track id: {name}")
            ids.add(tid)
            pcm, rate, channels, frames = decode_xvag(
                decoder, path, Path(temp) / (name + ".wav"))
            start = (len(body) + 15) & ~15
            if start + len(pcm) > MAX_BODY:
                raise RuntimeError("Expanded PCM body exceeds 64 MiB")
            body.extend(b"\0" * (start - len(body)))
            body.extend(pcm)
            originals[name] = pcm
            records.append({
                "id": tid, "name": name, "codec": 1,
                "sample_rate": rate, "channels": channels,
                "frames": frames, "offset": start, "bytes": len(pcm),
                "crc32": zlib.crc32(pcm), "looping": 0,
                "loop_start": 0, "loop_end": 0,
            })
            print(f"DECODED {name}: {rate} Hz, {channels} ch, {frames} frames")
    metadata = json.dumps(
        {"schema": "northstar.nac.nef8.v1", "tracks": records},
        ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(metadata) > 8 * 1024 * 1024:
        raise RuntimeError("NAC metadata exceeds 8 MiB")
    compressor = zlib.compressobj(level=9, wbits=-15)
    compressed = compressor.compress(body) + compressor.flush()
    data_offset = 128 + len(metadata)
    header = bytearray(128)
    HEADER.pack_into(header, 0, b"NEF8", 1, 128, 12, 1, 1, 0,
                     data_offset, len(compressed), len(body), len(records),
                     128, len(metadata))
    header[64:96] = blake3(body).digest()
    struct.pack_into("<Q", header, 112, 1)
    return bytes(header) + metadata + compressed, originals


def verify(blob: bytes, expected: dict[str, bytes] | None = None) -> dict:
    if len(blob) < 128:
        raise RuntimeError("Truncated NEF8 header")
    (magic, major, hsize, kind, compression, schema, flags,
     offset, csize, usize, count, moffset, msize) = HEADER.unpack_from(blob)
    if (magic, major, hsize, kind, compression, schema, flags) != (
            b"NEF8", 1, 128, 12, 1, 1, 0):
        raise RuntimeError("Invalid NEF8/NAC header")
    if (moffset != 128 or offset != moffset + msize or not msize
            or offset + csize != len(blob) or not 1 <= count <= 1024
            or not 0 < usize <= MAX_BODY
            or struct.unpack_from("<Q", blob, 112)[0] != 1):
        raise RuntimeError("Invalid NEF8/NAC offsets or limits")
    metadata = json.loads(blob[moffset:offset].decode("utf-8"))
    if (metadata.get("schema") != "northstar.nac.nef8.v1"
            or len(metadata["tracks"]) != count):
        raise RuntimeError("Invalid NAC audio metadata")
    inflated = zlib.decompressobj(wbits=-15)
    body = inflated.decompress(blob[offset:], MAX_BODY + 1) + inflated.flush()
    if (not inflated.eof or inflated.unused_data
            or len(body) != usize or blake3(body).digest() != blob[64:96]):
        raise RuntimeError("Invalid NEF8 PCM, stream termination, or BLAKE3")
    cursor = 0
    names, ids = set(), set()
    for raw in metadata["tracks"]:
        # The native boost::property_tree writer quotes integer fields in JSON;
        # the engine accepts both quoted integers and JSON integer literals.
        t = {k: (v if k == "name" else int(v)) for k, v in raw.items()}
        name = t["name"]
        ident = t["id"]
        start, count_bytes = t["offset"], t["bytes"]
        if (name in names or ident in ids or ident != stable_id(name)
                or t["codec"] != 1 or t["looping"] != 0
                or t["loop_start"] != 0 or t["loop_end"] != 0
                or start != (cursor + 15) & ~15
                or not 0 < count_bytes == t["frames"] * t["channels"] * 2
                or t["channels"] not in (1, 2)
                or not 8000 <= t["sample_rate"] <= 192000
                or start + count_bytes > len(body)):
            raise RuntimeError(f"Invalid NAC track: {name}")
        pcm = body[start:start + count_bytes]
        if zlib.crc32(pcm) != t["crc32"]:
            raise RuntimeError(f"Invalid NAC track CRC32: {name}")
        if expected is not None and expected.get(name) != pcm:
            raise RuntimeError(f"PCM mismatch against XVAG source: {name}")
        cursor = start + count_bytes
        names.add(name)
        ids.add(ident)
    if cursor != len(body) or (expected is not None and names != set(expected)):
        raise RuntimeError("NAC contains unexpected trailing/missing tracks")
    return {"tracks": count, "bytes": len(blob), "pcm_bytes": len(body),
            "names": sorted(names)}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--decoder", help="Path to vgmstream-cli.exe")
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    files = sorted(SOURCE.glob("*.xvag"))
    if not files:
        raise RuntimeError(f"No XVAG sources found in {SOURCE}")
    # Always compare decoded source PCM, even on subsequent runs. Track-name
    # equality alone cannot detect a changed XVAG with the same filename.
    blob, originals = pack(decoder_path(args.decoder), files)
    if args.verify_only:
        info = verify(TARGET.read_bytes(), originals)
        print("VERIFIED", json.dumps(info))
        return
    if TARGET.exists():
        info = verify(TARGET.read_bytes(), originals)
        print("EXISTS VALID, PCM MATCHES SOURCES", json.dumps(info))
        return
    info = verify(blob, originals)
    staged = SOURCE / "pistol.nac.stage"
    if staged.exists():
        raise RuntimeError("Staged NAC already exists; refusing overwrite")
    try:
        with staged.open("xb") as stream:
            stream.write(blob)
            stream.flush()
            os.fsync(stream.fileno())
        verify(staged.read_bytes(), originals)
        if TARGET.exists():
            raise RuntimeError("Destination appeared during conversion")
        staged.rename(TARGET)
    finally:
        if staged.exists():
            staged.unlink()
    print("CREATED", TARGET)
    print("VERIFIED", json.dumps(info))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError,
            subprocess.SubprocessError) as exc:
        print(f"PISTOL_NAC_ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
