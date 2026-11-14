# NorthStarCpp 0.4.0 - Qt Editor-only clean Release QA

Date: 2026-11-14
Source root: C:/Users/AidenFox/Documents/NorthStarCpp
Status: local Windows QA accepted; PUBLIC RELEASE BLOCKED

## Editor consolidation

- Only editor executable built and packaged: northstarcpp_editor_qt.exe
- Removed legacy northstarcpp_editor CMake target, install rule, and Project
  Manager legacy fallback.
- Historical window.cpp remains compiled ONLY by northstarcpp_player.exe,
  which needs the shared runtime source. It no longer builds an Editor.
- Qt Editor contains fly camera, shaded/wireframe proxy views, adjustable
  grid and speed, and Play/Stop Game commands.

## Full clean Release build

Build directory: out/qt-editor-clean-verify-20261114

Windows x64 MinGW-w64 GCC 15.2.0, Ninja Release, BUILD_TESTING=ON.
- Clean CMake configuration: PASS.
- Ninja all: 711/711 operations PASS, exit code 0.
- Ninja dry run after reconfigure: no work to do.
- Target discovery: northstarcpp_editor_qt.exe present,
  northstarcpp_editor.exe absent.
- Full CTest: 89/89 PASS, 0 failures, 13.95 seconds.

Logs in out/qt-editor-clean-verify-20261114:
- clean-build.log
- full-ctest.log
- qa-package.log

## Isolated installed QA candidate

Output:
out/qt-editor-qa-candidates-20261114/
NorthStarCpp-0.4.0-windows-x64-Release-QA

- CMake install Runtime, SDK, Tools, Documentation, Licenses: PASS.
- Qt DLL/platform plugin deployment (qwindows.dll): PASS.
- Installed Qt Editor->Player NSCN save/validate: PASS.
- Local installed package application smoke: PASS.
- SDK smoke and GNU objdump DLL import audit: PASS.
- Manifest re-verification via build_dist.py --verify: PASS.
- Manifest: 201 files and 9 plugin records, valid.
- One Qt Editor at bin/northstarcpp_editor_qt.exe; no
  northstarcpp_editor.exe anywhere in the QA candidate.
- Same-machine isolated-environment launches (restricted PATH, temporary
  APPDATA/LOCALAPPDATA): all PASS for Qt Editor --smoke,
  Qt Editor --player-check, Project Manager --smoke and
  Asset Browser --smoke.

First packaging attempt found a smoke-order bug: asset-zone check used
dist_smoke.nscn before Qt Editor authored the file. tools/build_dist.py
was corrected to create the scene before validating it, and the full
second QA package run returned 0.

## Outstanding release gates

1. Checks above ran on the developer Windows installation, NOT an
   independent clean Windows VM. Restricted PATH and temporary user dirs
   are useful local isolation, not external acceptance.
2. No security-reviewed and signed native V8 SDK/provider was supplied
   to the distribution builder.
3. Public promotion requires verified V8 security attestation, Qt/V8
   process isolation and an independently signed clean-Windows report.
   Existing release gates have not been bypassed.
4. release_approved is false. Production dist and installed DLLs were
   not overwritten; QA candidate is not for public distribution.
5. Full Vulkan/PBR editor render integration remains separate; the Qt
   shaded and wireframe views are CPU scene proxies.

## Acceptance overview

Legacy editor target and Project Manager fallback: CLOSED.
Full clean local Release build: CLOSED.
All local CTests: CLOSED.
Installed local QA artifact + DLL/manifest/smoke: CLOSED.
Independent clean-machine + signed V8 release: OPEN.
