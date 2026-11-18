# NorthStarCpp Qt Editor — UE 5.3.2 UX alignment

Date: 2026-11-18

## Source evaluation

Reference repository: https://github.com/Mccrae123/Unreal-Engine-5.3.2

The repository root and its README/LICENSE were inspected. For a
function-level comparison, these **specific original UE 5.3.2 sources**
were opened read-only from the repository's release branch:

- `Engine/Source/Editor/LevelEditor/Private/SLevelEditor.cpp`
- `Engine/Source/Editor/LevelEditor/Private/SLevelEditor.h`
- `Engine/Source/Editor/LevelEditor/Private/LevelEditorActions.cpp`
- `Engine/Source/Editor/ContentBrowser/Private/SContentBrowser.cpp`
- `Engine/Source/Editor/PropertyEditor/Private/SDetailsView.cpp`

Identified source-level responsibilities in `SLevelEditor.cpp` include
`BindCommands`, `RegisterMenus`, `GetTabManager`,
`CreateSceneOutliner`, `CreateActorDetails`, `SyncDetailsToSelection`,
`TryInvokeTab`, `SaveViewportTabInfo`, and `RestoreViewportTabInfo`.
Content Browser exposes independent command binding, folder navigation,
keyboard handling, and docking. These are architecture references,
**not code copied or linked into NorthStarCpp**. Unreal source remains
under Epic Games' licensing terms.

## Existing NorthStar capabilities preserved

- Native Qt `QMainWindow` and `QDockWidget`, stable object names
- Central GPU scene viewport, W/A/S/D fly camera, gizmos and snapping
- World Outliner, Place Actors, Details/Properties
- Docked Content Browser and already functional `Ctrl+Space` drawer
- Project and engine-shared NEF8 assets, YMAT material editing
- Native Play/Stop/Pause/Eject action semantics (unimplemented commands
  remain disabled; command palette must honor disabled actions)
- Saved QSettings window geometry/docking, F6 PIE, keyboard shortcuts
- Scene V3, entity/physics/rendering integration

## Implemented UI improvement (first stage)

### Search editor actions — Ctrl+Shift+P

`qt_editor_command_palette.hpp` is a native Qt modal palette. It enumerates
existing `QAction` objects, filters by action name or shortcut,
sorts exact/prefix matches first, omits disabled/unavailable actions,
supports Up/Down/Enter/Escape and executes the selected original QAction
after dismissing the palette. No duplicated game logic, undo logic or
scene-mutating proxy action is introduced.

Toolbar adds **Search Actions** with tooltip and real action trigger.

### Focus / layout workflows

`Window > Editor Layouts`:

- Default Level Editing (**Ctrl+Shift+1**): Outliner at left, Details at
  right, Content Browser at bottom; hides secondary debug panels.
- Asset Editing (**Ctrl+Shift+2**): Content Browser at left, viewport in
  center, Details at right, Outliner hidden for more workspace.
- Immersive Viewport (**F11**): hides docks and toolbars; restores the
  previous exact Qt dock layout on exit.
- Save Layout As… / Load Saved Layout: persistent named `QSettings`
  states with stable QDockWidget identifiers, no custom serialization
- Focus World Outliner (**Ctrl+Alt+O**) / Focus Details (**Ctrl+Alt+D**)
- Existing `Ctrl+Space` Content Drawer preserved; no competing shortcut

The F11 state is **not persisted** as the default panel layout upon close.
Press F11 again to restore normal editing.

## Changed files

- `engine/northstarcpp/modules/editor/qt_editor_command_palette.hpp` (new)
- `engine/northstarcpp/modules/editor/qt_editor_window.hpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window.cpp`
- `engine/northstarcpp/modules/editor/qt_editor_window_smoke.cpp`

## Verification

1. `ninja -C out/build northstarcpp_editor_qt` — **PASS**
2. `QT_QPA_PLATFORM=offscreen out/build/northstarcpp_editor_qt.exe --smoke`
   — **PASS**, prints:
   `Qt unified editor + scene + asset placement + undo/redo: PASS`
3. \`northstarcpp_editor_qt.exe --place-actors-smoke\` — **PASS**:
   camera, lights, native object placement and drag/drop.
4. \`northstarcpp_editor_qt.exe --gpu-smoke\` — **PASS** on the normal
   Windows desktop: native GPU viewport and D3D12 sky/resources.
   The same GPU smoke returns 8 when forced to
   \`QT_QPA_PLATFORM=offscreen\`; do not use the headless Qt backend for
   native GPU image acceptance.
5. Regression assertions added to the existing Qt editor smoke:
   - command palette exists with shortcut, results list and search
   - real Play QAction is searchable
   - nonexistent queries return empty results
   - F11 hides Outliner and safely restores docks and checked state
   - both named built-in workspace presets registered
6. No real user scene or proprietary source assets changed by this update.
7. Compiler/linker emits existing Boost duplicate-section diagnostics,
   but editor links successfully (exit code 0).
8. Normal interactive manual visual acceptance on the user's monitor
   **not performed**. Headless smoke is not a replacement for GUI QA.

## Next UX iteration, not claimed as complete

- Context-sensitive Details search and reset-to-default property actions
- Customizable viewport toolbar profiles and keyboard shortcut editor
- Actor drag/drop feedback and searchable Scene Outliner filtering
- Multi-asset selection and batch Details editing with undo grouping
- Window menu managed panel focus across arbitrary dock groups
- Accessibility, scaling, input latency and DPI visual QA

**Not UE5 feature parity:** This is a targeted first UX pass. Nothing in
the underlying NorthStar engine is replaced by Unreal Engine modules.
