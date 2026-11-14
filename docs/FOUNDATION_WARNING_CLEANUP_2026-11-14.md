# Foundation compiler warning cleanup — 2026-11-14

## Scope
The supplied 228-line build excerpt contains 41 occurrences of
-Wconversion-null (char <- NULL). They originate from 13 expressions
in the three foundation files below, repeated in different translation
units. No other warning category was present in that excerpt.

## Source-level corrections
- buffer_string_inline.h: 11 assignments to *m_end converted from NULL to a
  character zero terminator (single-quoted backslash-zero).
- fixed_string_inline.h: 1 assignment to m_buffer[0] corrected.
- core/sources/buffer_string.cpp: 1 assignment to *m_end corrected.

All original files live in:
engine/northstarcpp/modules/foundation/northstarcpp/foundation

Pointer-valued NULL initializers remain unchanged. No warning-suppression
pragmas or compiler -Wno flags have been introduced. The normal string
zero-termination semantics are preserved.

The first-party northstarcpp_foundation_core target enforces
-Werror=conversion-null for GCC, so this class of mistake fails future builds.

## Build verification
Release build directory: out/gcc-release
Command: cmake --build out/gcc-release --target all --parallel 5
Exit: 0; rebuilt 115 steps; 0 emitted compiler warnings and 0 errors.
Build log: out/warning-cleanup-20261114/full-release-build.log

Command: ctest --test-dir out/gcc-release --parallel 5 --output-on-failure
Result: 107/107 tests PASS.
Test log: out/warning-cleanup-20261114/full-release-ctest.log

A separate build compiled northstarcpp_math and northstarcpp_foundation_core
from scratch without warnings, and all 3 selected regression tests passed.

## Accuracy and release status
The existing Release build tree was updated incrementally; the successful
115-step build is not a completely clean compile of all translation units.
Therefore the zero-warning result applies to those executed compile steps,
not to untouched objects. Production dist and installed DLLs are unchanged.
