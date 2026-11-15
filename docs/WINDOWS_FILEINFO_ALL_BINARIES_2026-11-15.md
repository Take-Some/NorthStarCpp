# Windows FileInfo / VERSIONINFO for NorthStar Binaries

Date: 2026-11-15

## Scope

All first-party Windows executable and shared/module-library CMake targets in
the NorthStarCpp root and engine build directories receive native PE
`VS_VERSION_INFO` metadata automatically. This includes the editor, player,
project manager, asset browser, GPU diagnostics, providers, and in-tree QA tools.

The two existing standalone application resources are preserved:
`bugtrap_version.rc.in` and `shader_generator.rc.in`. Each binary gets one
VERSIONINFO block. All vendor DLLs (Qt, Vulkan loader, V8, GCC runtime,
Bullet/Jolt and other redistributed third-party files) retain their original
metadata and ownership.

Separately configured V8 provider builds under `tools/v8_provider` and
`tools/v8_provider/official_sdk` now also call the same FileInfo helper.
Their independent CMake project versions are used.

## Fields

| Resource property | Value |
| --- | --- |
| CompanyName | Kayla's Systems |
| ProductName | NorthStarCpp Engine |
| LegalCopyright | Copyright (C) 2026 Kayla's Systems - Kayla Verner |
| FileVersion / ProductVersion | Derived from CMake project VERSION plus fourth component .0 |
| InternalName | Actual CMake target output base name |
| OriginalFilename | Computed from final CMake output name, prefix and suffix |
| FileDescription | Tailored to each major application or provider capability |
| Comments | Component-specific purpose |
| File type | `VFT_APP` for EXE and `VFT_DLL` for DLL |
| Resource translation | en-US, Unicode (0409/04B0) |

For `AssetManager.dll`, `OriginalFilename=AssetManager.dll` and the
description identifies the `engine.assets` directory provider. Neither the
host ABI nor the provider's dispatch identity is changed.

## Implementation

- `tools/cmake/NorthStarFileInfo.cmake`: finds first-party CMake targets and
  adds a generated resource for each executable/DLL.
- `tools/cmake/NorthStarFileInfo.rc.in`: common version-resource template.
- `CMakeLists.txt`: wires this into the Windows build after target registration.
- `tools/check_windows_fileinfo.py`: validates Version API properties,
  numeric and string versions, resource type and original filename.
- `northstarcpp_windows_fileinfo`: repeatable Windows CTest.
- Shader Generator's `ProductName` was aligned to `NorthStarCpp Engine`.

## Verification

A fresh MinGW-w64/Ninja configuration was created in
`out/fileinfo-qa-20261115`.

- Windows CMake configure: PASS (128 auto-generated FileInfo targets).
- 128/128 generated RC resources compiled with `windres`: PASS.
- `northstarcpp_portability_smoke.exe`: built and Version API checked: PASS.
- `AssetManager.dll`: built and Version API checked: PASS.
- `input_win32.dll`: built and Version API checked: PASS.
- `shader_generator.exe`: built and Version API checked: PASS.
- `bugtrap.exe`: built and Version API checked: PASS.
- Shader Generator icon/resource CTest: PASS.
- Windows FileInfo CTest: PASS.

Check built binaries with:

```powershell
python tools/check_windows_fileinfo.py --version 0.4.0.0 `
  out/fileinfo-qa-20261115/AssetManager.dll `
  out/fileinfo-qa-20261115/input_win32.dll
ctest --test-dir out/fileinfo-qa-20261115 -R windows_fileinfo --output-on-failure
```

## Current limitations

The 128 resource scripts are configured and validated, but this QA run did
not rebuild all 128 corresponding binaries. Existing older `out` or `dist`
files are not retroactively rewritten: rebuild/install them from this source
tree to embed FileInfo. The separately sourced V8 provider DLLs were wired
up but could not be built here without their respective external V8 SDKs.
No commit or push was performed.
