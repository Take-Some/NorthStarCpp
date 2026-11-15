# NorthStarCpp ContentBrowser — Project and Engine Shared Roots
Date: 2026-11-15

## Request

The embedded Qt Editor ContentBrowser must show two independent top-level
source directories: the current project's content and the engine-wide
sharedAssets collection, without copying engine files into every project.

## Implementation

- The folder tree now always has two named top-level entries:
  - `Project Assets`: the active project's `assets/` catalog, including
    the existing virtual `prefabs/` collection from the project root;
  - `Engine Shared Assets`: the actual sharedAssets mount discovered
    by the Editor's existing `sharedMaterialRoot()` implementation.
- `QtEditorWindow` configures the engine source once at startup via
  `QtContentBrowser::setSharedContentRoot()`. Project loading continues
  through `setProjectScene()`.
- The browser retains a separate `AssetCatalog` for each root and
  scans both asynchronously in its background thread pool. No file copies,
  duplicate imported models or secondary Asset Browser window are used.
- Tree items carry a source identifier, so identically named folders and
  assets in project/engine mounts do not collide inside the navigation tree.
  Changing the active root updates card grid, filter output, thumbnails,
  preview resolution, breadcrumb and filesystem operations.
- The two tree roots persist while indexing and while no project is open.
  The engine root remains accessible after closing the active project.
- Existing VFS-relative asset references are preserved for use by Player
  and Qt Editor: e.g. `models/weapon/pistol.ydd` is browsed directly from
  `sharedAssets/models/weapon/` and is not converted to OBJ.
- Auto-reveal searches the active catalog first, then switches to the
  other source if needed.
- Thumbnail completion uses a separate generation counter from the
  index scan. Switching roots drops stale thumbnails, without canceling
  the in-flight background scan of both sources.
- Source selection does not delete/rebuild QTreeWidget items inside a
  synchronous Qt click handler, preventing dangling clicked-item use.
- The Engine Shared Assets source is read-only in ContentBrowser:
  Create/Import/Rename/Duplicate/Delete are disabled in UI and rejected
  by the corresponding command handlers. Opening a shared material uses
  preview, not the project material editor. Copy Reference, preview,
  drag/drop and add-to-scene remain available.

## Test coverage

Existing Qt Editor command `--content-dock-smoke` has additional
assertions for:

1. two permanent tree root labels;
2. alpha project -> engine shared -> alpha project navigation;
3. a shared `models/weapon/pistol.ydd` fixture;
4. disabled Add/Import controls in the engine root;
5. project switching to beta with correct catalog invalidation;
6. engine shared catalog remaining usable without an open project.

## Verification

- `cmake -S . -B out/content-dual-roots-qa-20261115 -G Ninja`: PASS.
- Native compilation of `qt_content_browser.cpp`: PASS.
- Native compilation of `qt_editor_window.cpp`: PASS.
- Native compilation of `qt_editor_main.cpp`: PASS.
- Prior to native builds, all three passed `-fsyntax-only` checks.
- `git diff --check` on modified files: PASS.
- Full Editor link and the new end-to-end `--content-dock-smoke`
  were not completed in the current clean QA build: Ninja attempted
  to rebuild approximately 693 dependency steps (including Bullet,
  Jolt and GPU/runtime libraries); the bounded attempt timed out.
  Do not claim the interactive UI regression passed.

## Remaining considerations

- Source-specific VFS identifier collisions (same relative asset path
  in Project and Engine) still follow existing VFS project-overrides-engine
  semantics. Explicit source-qualified override authoring would require
  extending the canonical VFS reference contract across Player/Editor.
- Future enhancement: cache the unchanged engine-wide catalog across
  project switches instead of rescanning the engine mount each time.
- No production dist replacement, Git commit or push.
