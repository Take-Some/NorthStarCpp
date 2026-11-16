# NorthStar Content Browser — UE 5.3.2 parity, phase P1

Date: 2026-11-16
Repository: `NorthStarCpp`
Build: `_qa_editor_mixed` (Windows, Qt6, Ninja)

## Target and license

Implement NorthStar-native analogs of UE 5.3.2 Content Browser behavior, not
copy Epic Games source. The NorthStar implementation remains C++17/Qt6 and
GPLv3-compatible. Reference previously inspected:
`SContentBrowser.cpp`, `SAssetView.cpp`, `SPathView.cpp`.

## Implemented

### Named Collections
- Local persistent, project-/engine-root-scoped named asset-reference sets
  using Qt QSettings.
- Create and select Collections from the Content Browser; add/remove current
  multi-selection via the context menu. The collection view works across
  arbitrary folders and respects the active source's read-only policy.
- Bounded 128 collection names, 2048 unique safe virtual paths per collection;
  no physical asset duplication; no writes to shared engine assets.

### Advanced asset search
- Standalone bounded parser in `asset_browser_query.hpp`, compiled **once per
  visible catalog rebuild**, not repeatedly for every row.
- Implicit AND, explicit AND/OR/NOT, ! and - negation, parentheses, quoted
  terms, `name:`, `path:`, `type:`, `kind:`, `ext:`, `is:`, and size
  comparison expressions such as `size>10MB`.
- Input and nesting limits with parse errors surfaced as search tooltip.
  Uses indexed record properties; never decodes asset payloads per query.
- Tests for precedence, negation, quoted terms, comparison units and invalid
  syntax.

### Batch operations
- Grid changed to extended selection with multi-reference drag MIME/text.
- Context menu: Copy N Asset References, Duplicate N Assets with preflight,
  conflict rejection and best-effort rollback, and Move N Assets to Recycle Bin
  after user confirmation.
- Limited to 256 project-owned, regular physical files. The provider excludes
  symbolic links, NEF8 virtual children, ListFile directories, engine shared
  assets and externally mounted prefabs. No blind recursive deletion.
- Ctrl+C copies selected asset references when the asset grid is focused.

### Source control
- Git menu (Status, Diff, Stage, Unstage) for editable physical project assets.
- Git invoked using `QProcess` argument arrays with a bounded runtime and
  output size, never via a shell. Explicit approval is required for Stage and
  Unstage. No automatic commit, push or checkout.
- Git work now runs under `QtConcurrent` with result callbacks on the UI
  thread, preventing the GUI thread from waiting for Git.
- Unit smoke creates a temporary Git repository, stages and unstages an
  asset with spaces and shell metacharacters in its name, and verifies status
  and cached diff.

### Indexed asset metadata
- Asset Catalog already indexed virtual NEF8 records, NAC track descriptors,
  prefab validity, kind, path and size.
- Added `AssetRecord::modified_epoch_ms`, populated during background
  indexing for files, folders, prefabs and virtual children.
- Asset Details now displays indexed modification time, virtual asset ID
  and source mount without filesystem `stat` for each selection.
- This is a **snapshot registry extension**, not yet a dependency-aware,
  live-updating Asset Registry service.

### Content Drawer and independent browser instances
- `Ctrl+Space` toggles the editor's embedded Content Browser dock.
- `New Content Browser Tab` creates another docked `QtContentBrowser`,
  with independent search, selection, filters, source and navigation state.
- Roots of newly opened projects synchronize from the primary browser to
  additional instances; no standalone browser executable is spawned.
- Native editor `--content-dock-smoke` covers both toggling and creation,
  project switch and source-root identity.
- The Drawer is a dock-reveal **MVP**: a separate auto-closing overlay and
  remembered extra-tab layout are not yet implemented.

### Layout behavior
- Previously fixed scrollable Asset Details continues to preserve the
  editor dock's height on selection, with long names, material function
  metadata, filtering, Collections and selection changes.

## Verification

Reconfigure/build:
```powershell
cmake -S . -B _qa_editor_mixed -G Ninja
ninja -C _qa_editor_mixed -j 4 northstarcpp_asset_browser_advanced_smoke northstarcpp_asset_browser northstarcpp_editor_qt northstarcpp_asset_catalog_index_smoke
```

Targeted CTest (10 tests):
```powershell
ctest --test-dir _qa_editor_mixed -R 'northstarcpp_(asset_browser_advanced|asset_catalog_index|editor_content_browser_tabs_qt|content_browser_stable_dock_qt|collision_icons_qt|nac_icons_qt|embedded_ycd_ymt_icons_qt|asset_category_icons_qt|folder_icons_qt|nef8_content_browser_qt)' --output-on-failure
```

This includes real Qt editor tab/root/drawer E2E, live Qt dock height
regression, advanced query/Collections/Git/metadata unit smoke and
NAC/YBN/YTD/YDD/YMAT visual/icon/index regressions.

## Remaining UE-grade parity

- Shared/team Collections and content-based smart Collections (current
  Collections are local and manually maintained).
- Complete UE search language, dynamic query suggestions and cross-provider
  metadata tag queries.
- Change notifications/incremental updates, dependency edges, redirectors,
  reference repair and an indexed Asset Registry database with persistent
  derived data.
- Git status decorations on thumbnails, branch/merge conflict UI and
  provider-independent source control backends (Perforce, etc.).
- Multi-asset rename/move/migrate with reference rewriting and transactional
  dependency validation; load and scale tests for tens of thousands of tiles.
- True animated overlay Content Drawer with outside-click auto-dismissal;
  optional persistence of additional Content Browser tab layouts.
- Hardware/Desktop QA of interaction and visual parity. The automated UI
  tests run on the Qt offscreen platform.

No original UE code copied, no production exe/DLL replaced, no Git commit or
push. Changes are kept in the local source working tree.
