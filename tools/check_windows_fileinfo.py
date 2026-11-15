#!/usr/bin/env python3
"""Validate Windows VERSIONINFO in owned NorthStar PE binaries (EXE and DLL)."""
from __future__ import annotations

import argparse
import ctypes
import sys
from ctypes import wintypes
from pathlib import Path

FIELDS = (
    "CompanyName",
    "FileDescription",
    "FileVersion",
    "InternalName",
    "LegalCopyright",
    "OriginalFilename",
    "ProductName",
    "ProductVersion",
    "Comments",
)


class VSFixedFileInfo(ctypes.Structure):
    _fields_ = [
        ("dwSignature", wintypes.DWORD),
        ("dwStrucVersion", wintypes.DWORD),
        ("dwFileVersionMS", wintypes.DWORD),
        ("dwFileVersionLS", wintypes.DWORD),
        ("dwProductVersionMS", wintypes.DWORD),
        ("dwProductVersionLS", wintypes.DWORD),
        ("dwFileFlagsMask", wintypes.DWORD),
        ("dwFileFlags", wintypes.DWORD),
        ("dwFileOS", wintypes.DWORD),
        ("dwFileType", wintypes.DWORD),
        ("dwFileSubtype", wintypes.DWORD),
        ("dwFileDateMS", wintypes.DWORD),
        ("dwFileDateLS", wintypes.DWORD),
    ]


def read_version_info(file: Path) -> tuple[dict[str, str], VSFixedFileInfo]:
    api = ctypes.WinDLL("version", use_last_error=True)
    api.GetFileVersionInfoSizeW.argtypes = (wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD))
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD,
                                         wintypes.DWORD, ctypes.c_void_p)
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = (ctypes.c_void_p, wintypes.LPCWSTR,
                                    ctypes.POINTER(ctypes.c_void_p),
                                    ctypes.POINTER(wintypes.UINT))
    api.VerQueryValueW.restype = wintypes.BOOL
    size = api.GetFileVersionInfoSizeW(str(file), None)
    if not size:
        raise ValueError("missing VS_VERSION_INFO resource")
    buffer = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(file), 0, size, buffer):
        raise ValueError("GetFileVersionInfoW failed")

    def query(key: str) -> tuple[int, int]:
        value = ctypes.c_void_p()
        length = wintypes.UINT()
        if not api.VerQueryValueW(buffer, key, ctypes.byref(value), ctypes.byref(length)):
            raise ValueError("missing VERSIONINFO key: " + key)
        return value.value, length.value

    root, length = query("\\")
    if length < ctypes.sizeof(VSFixedFileInfo):
        raise ValueError("invalid fixed version information")
    fixed = VSFixedFileInfo.from_buffer_copy(
        ctypes.string_at(root, ctypes.sizeof(VSFixedFileInfo)))
    values = {}
    for name in FIELDS:
        data, size = query("\\StringFileInfo\\040904B0\\" + name)
        if not data or not size:
            raise ValueError("empty VERSIONINFO field: " + name)
        values[name] = ctypes.wstring_at(data)
        if not values[name].strip():
            raise ValueError("blank VERSIONINFO field: " + name)
    return values, fixed


def check(file: Path, version: str, company: str) -> None:
    if not file.is_file():
        raise ValueError(f"binary not found: {file}")
    metadata, fixed = read_version_info(file)
    if fixed.dwSignature != 0xFEEF04BD:
        raise ValueError("invalid VS_FIXEDFILEINFO signature")
    expected_type = {".exe": 1, ".dll": 2}[file.suffix.lower()]
    if fixed.dwFileType != expected_type:
        raise ValueError(f"incorrect file type: {fixed.dwFileType} != {expected_type}")
    if metadata["OriginalFilename"].lower() != file.name.lower():
        raise ValueError(f"OriginalFilename mismatch: {metadata['OriginalFilename']} != {file.name}")
    if metadata["CompanyName"] != company:
        raise ValueError("incorrect CompanyName: " + metadata["CompanyName"])
    if metadata["FileVersion"] != version or metadata["ProductVersion"] != version:
        raise ValueError("incorrect string version: " + metadata["FileVersion"])
    parts = tuple(map(int, version.split(".")))
    if len(parts) != 4:
        raise ValueError("expected a four-component version")
    file_parts = (fixed.dwFileVersionMS >> 16, fixed.dwFileVersionMS & 0xFFFF,
                  fixed.dwFileVersionLS >> 16, fixed.dwFileVersionLS & 0xFFFF)
    product_parts = (fixed.dwProductVersionMS >> 16, fixed.dwProductVersionMS & 0xFFFF,
                     fixed.dwProductVersionLS >> 16, fixed.dwProductVersionLS & 0xFFFF)
    if file_parts != parts or product_parts != parts:
        raise ValueError(f"incorrect numeric FileVersion/ProductVersion: {file_parts}/{product_parts}")
    print(f"PASS: {file.name} | {metadata['FileDescription']} | {version} | {company}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="four-part Windows file version")
    parser.add_argument("--company", default="Kayla's Systems")
    parser.add_argument("binaries", nargs="+", type=Path)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("the Windows Version API is required")
    for file in args.binaries:
        try:
            check(file.resolve(), args.version, args.company)
        except (ValueError, OSError) as error:
            print(f"FAIL: {file}: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
