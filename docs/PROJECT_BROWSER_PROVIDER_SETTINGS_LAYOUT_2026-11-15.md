# Project Browser — Expanded Provider Selection (2026-11-15)

## Problem

The Project Browser used a fixed 286 px details column and embedded the
engine API provider selections in a 72–260 px nested scroll area. When several
plugins were available, the fields became hard to read and select, and
horizontal and vertical scrollbars competed for space.

## Changes

- The details sidebar is 328 px wide and now contains a concise summary and
  **Configure Engine Providers...** action instead of a cramped list.
- The dedicated provider settings dialog is resizable (initially 800×660 px;
  minimum 580×440 px). Provider choices are grouped into separate cards,
  offer full-width combo boxes and readable renderer compatibility warnings,
  and use vertical scrolling without a horizontal scrollbar.
- **Save** applies project-specific overrides; **Cancel** leaves the saved
  configuration unchanged; **Reset All to Automatic** clears the selections
  only when the user subsequently saves.
- Missing or incompatible saved providers are still visibly identified.
- The first-launch provider-selection dialog is now 780×620 px by default
  (minimum 580×430 px), with vertically scrollable choices and a fixed
  bottom action row.
- API and plugin ABI identifiers remain unchanged.

## Verification

- Direct Ninja compilation of the Project Browser source — PASS.
- Isolated project-manager link and Qt runtime deployment — PASS.
- Native Windows Qt smoke test — PASS (exit code 0).
- Smoke checks the expanded dialog geometry, no horizontal scrolling,
  saved provider selection, Cancel, and Reset All.

## Build constraint

The full standard CMake/Ninja project-manager target currently traverses
an unrelated Editor link dependency which fails with unresolved
material_authoring symbols. The Project Browser executable was successfully
linked separately from its generated Ninja link command.
The Editor and full production distribution were not rebuilt or replaced.
