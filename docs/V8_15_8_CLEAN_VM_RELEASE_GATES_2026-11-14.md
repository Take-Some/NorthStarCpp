# NorthStarCpp: V8 15.8 and clean Windows release gates
Date: 2026-11-14
Decision: RELEASE BLOCKED

## Verified V8 source

Official V8 branch: refs/branch-heads/15.8
Pinned revision: 19c06df29e0050413f5f041888f4c990de970314
Pinned source record:
out/scripting-release-clean-20261114/qa-process-isolated-v3/v8-upstream-source-pin.json

Source revision was checked with the official V8 Git remote. This is a
source pin, not a verified SDK, binary build or security approval.

## Build prerequisites

Source builder tools preflight:
tools/v8_provider/preflight_stable_build.py

The development host is Windows 10 Pro 19045. GN, gclient, fetch from
depot_tools and a compatible Windows C++ compiler were not available.
The preflight recorded insufficient free disk capacity against the
project's conservative 30 GiB build-space policy.

A V8 15.8 source checkout, dependency synchronization, native library
build and ABI-compatible scripting_v8 DLL have NOT been completed.
Official Windows GN/clang-cl libraries cannot automatically replace
the existing UCRT64 GNU C++ provider import libraries. The C ABI
boundary must be tested with the chosen toolchain.

## Production two-stage release pipeline

tools/build_dist.py now supports:

Phase A: --qa-only, producing a non-shipping candidate under out/
and never replacing dist.
Phase B: --promote-qa, requiring security-approved V8 provider and an
independently signed clean-machine report that matches the exact QA
manifest SHA-256. No direct publication is allowed.

The build-from-existing-tree route rejects pending Ninja targets.
Signed V8 approval and clean Windows VM evidence are mandatory for
production promotion. No fake signing keys have been created.

## Native-only QA acceptance actually completed

Folder:
out/qa-release-candidate-20261114/
  NorthStarCpp-0.4.0-windows-x64-Release-QA/

Latest completed Ninja all-target build: PASS.
Full CTest after NEF8 chunked-scene recompilation: 82/82 PASS.
Qt runtime deployment, installed package smoke, SDK smoke and import
inspection: PASS.
Manifest: 200 files / 9 registered plugins; re-verification PASS.
V8 scripting DLL: NOT INCLUDED. This is a native-only QA artifact,
not an approved scripting distribution.

The logger lifecycle integration smoke now uses RUN_SERIAL, preventing
GUI-process contention when CTest is invoked with parallel workers.

## Clean Windows VM acceptance

PowerShell 5.1 guest harness:
tools/v8_provider/clean_windows_acceptance.ps1

The script checks package content hashes, native Editor/Player launch,
Qt tools, source and archive JS lifecycle, presence of a VM and absence
of development compilers. It writes an unsigned evidence JSON file.
A release engineer must review VM provenance and sign the raw JSON with
a separate trusted Ed25519 key. Verification rejects mismatched package
manifest hashes, missing VM evidence, missing developer-tool absence,
expired evidence and missing/invalid signatures.

Windows Sandbox/Hyper-V/other clean VM executables were not present on
the host. A local script rehearsal returned exit code 2 by design:
four runtime checks passed; VM provenance, developer-tool absence and
the signed-stage manifest were not present. No independent guest result
has been produced.

## Remaining work

1. Provision an adequate Windows V8 build machine and the official
   depot_tools/GN toolchain, fetch the reviewed V8 15.8 stable revision
   and its dependencies, build d8/SDK, test native scripting_v8 ABI.
2. Obtain an independently signed V8 SDK security approval.
3. Produce the V8-enabled QA candidate from a clean Ninja build.
4. Install/test the exact candidate in an independent clean Windows VM
   and obtain a signed acceptance bound to its package manifest.
5. Only then run a controlled promotion. Production dist and existing
   DLLs remain unchanged.
