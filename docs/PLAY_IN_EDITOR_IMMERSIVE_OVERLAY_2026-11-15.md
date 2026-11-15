# Play In Editor — Immersive Full-Editor Overlay
Date: 2026-11-15

## User-visible contract

Press **F6 / Game > Play In Editor** to run NorthStarCpp Player
inside the whole Qt Editor client, not inside only Scene Viewport
and never underneath ContentBrowser, Scene Hierarchy, Inspector,
Output or toolbars. The Editor's Windows titlebar remains as the
parent application's normal window frame; immersive mode does not
request OS/monitor fullscreen or desktop always-on-top.

Press **Escape** once to stop the simulation, release native input
capture, close the embedded Player and restore the exact authoring
workspace (dock positions and tab groups, visibility, toolbars,
menu/status bars and Viewport focus). **Game > Launch Standalone
Player** remains supported.

## Win32/Qt architecture

The engine-owned `QtPlaySurface` is a native Qt child widget of
`QtEditorWindow`, sized to the entire `QMainWindow::rect()`.
The Player is a separate process with its original GPU renderer,
physics, scene loading and scripting. With `--embed-parent HWND`,
the Player creates its native window as `WS_CHILD` under this
full-window Qt surface, not a popup or a child of Scene Viewport.

Qt6 docks and the native GPU viewport can own distinct HWNDs.
`QWidget::raise()` alone cannot guarantee correct native sibling
z-order. Two complementary measures are now used:

1. `beginPlayOverlay()` saves `QMainWindow::saveState(1)`,
   records menu/status visibility and temporarily hides ALL
   direct QDockWidget and QToolBar children plus menu/status bars.
   The central editor canvas remains alive but fully obscured.
2. `QtPlaySurface::raiseAboveEditorChrome()` uses Qt raise
   and Windows `SetWindowPos(HWND_TOP)` on the child overlay
   when any native sibling is above it, with neither
   `HWND_TOPMOST` nor a second top-level OS window.
   Z-order is checked during the native child discovery loop
   and after main window resize.

When Player exits (normally, via Escape, failed startup or crash),
`endPlayOverlay()` stops child monitoring, hides and releases the
overlay and restores `QMainWindow::restoreState(1)` plus the saved
menu/status visibility. Editor close restores chrome before
persisting workspace settings. The existing Player exit/timeout
management remains in place.

Escape from native Player is delivered via registered Win32
`NorthStarCpp.PlayInEditor.Exit.v1` notification, not assumed
to reach Qt keyboard shortcuts while focus belongs to a
cross-process GPU child. Qt processes this as a queued exit action.

## Expanded acceptance

QA Editor executable:
`out/ymat-entry-editor-qa-20261115/northstarcpp_editor_pie_qa.exe`
(QA-only name used to avoid replacing a running editor executable).

The `--embedded-game-smoke` integration test verifies:
- overlay fills complete Qt Editor client, not only the Viewport;
- all editor chrome is invisible during gameplay;
- a real cross-process Player HWND is discovered/attached;
- five native Win32 child-window hit tests over previously docked
  UI regions select the Player/overlay, not dock windows;
- Escape IPC is delivered and Player exits;
- Viewport and all original dock visibility/areas/tab groups,
  toolbar placement, menu and status visibility are restored.

All these checks PASS.

`--embedded-render-smoke` also PASS: the real Player is started
with WARP GPU rendering, attaches to the Editor and responds to
Escape. `--gpu-smoke` and `--content-dock-smoke` PASS after
the changes.

## Related editor scene rendering issue

The Qt Editor had been initializing the QPainter **Shaded (Proxy)**
viewport mode by default: this mode intentionally renders every
scene entity as a colored cube, without native material sampling.
The Editor now prefers **Engine GPU — Native Models & Materials**
on Windows. Existing workspace settings migrate once from the
old implicit proxy default; subsequent explicit proxy selections
are respected. Initialization is scheduled after the Qt Editor
window is shown.

The native Viewport already contains the same VFS YDD/YDR geometry
and YMAT/YTD material stream as Player. Direct placement and
dependency checks now also resolve sharedAssets YDD/YDR sources,
not only assets copied under each project. A native GPU smoke
passed; complete screenshot parity for an individual pistol
YDD/YMT or a particular multi-object prefab remains to be verified
with those resources in an actual Editor scene.

## Validation and non-goals

- QA Qt Editor C++ compilation and private executable link: PASS.
- Embedded native HWND/Escape and complete window z-order: PASS.
- Real embedded Player with WARP rendering and Escape: PASS.
- Qt GPU Viewport smoke: PASS.
- Qt ContentBrowser dock regression: PASS.
- No OS fullscreen, exclusive fullscreen, global topmost, or
  replacement of the standalone Player process.
- No production dist replacement, Git commit or push.
