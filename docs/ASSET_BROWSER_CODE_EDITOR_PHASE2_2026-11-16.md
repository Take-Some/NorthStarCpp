# NorthStar Qt Editor — Text Document Tabs / IntelliSense / GLSL
Date: 2026-11-16
Repository: C:\Users\Kayla\Documents\NorthStar\NorthStarCpp

## Document integration
QtTextAssetWorkspace owns a Qt native dock panel with tabs for multiple source files. It is tabified with the native Content Browser and does not replace the central Viewport. Double-clicking a physical .md/.js/.vert/.frag asset in any editor-owned Content Browser (including the extra dock instances and Content Drawer) opens/focuses a native document tab. The standalone QA browser retains the modal editor.

Canonical file paths prevent duplicate tabs. Modified tabs display an asterisk. Closing the main editor asks for confirmation when document source is dirty. Engine Shared Assets remain read-only.

## Editing
Single reusable implementation: qt_text_asset_editor.cpp.
- Syntax highlight, line numbers, current line, tab stop, Ctrl+S.
- Find Next, Replace, Replace All, Ctrl+F and Ctrl+H.
- UTF-8 validation, original BOM and CRLF preservation, atomic QSaveFile writes.
- No overwrite of externally changed source; 8 MiB size limit, binary rejection.
- Markdown rendered preview and read-only source tab.

## NorthStar IntelliSense
Completion suggestions and declaration signatures are indexed from the engine's embedded types/northstar-engine.d.ts at runtime (including Camera, UI, Scene, Input). Trigger via Ctrl+Space or typed member prefix. CodeEditor accepts ShortcutOverride for Ctrl+Space so QMainWindow's Content Drawer shortcut still works elsewhere.

Scope: source-aware class/method/property completion, not full TypeScript LSP semantics, type inference or project reference renames.

## Shader validation and reload boundary
The source editor launches installed glslc asynchronously via QProcess with source snapshot in a temporary .vert/.frag file, Vulkan 1.2 target, original asset directory include path and a temporary SPIR-V output. Diagnostics capture errors/warnings; double-click jumps to parsed source line. Saving a shader triggers validation. Only a successful validation emits a shader-ready event. Missing or failing glslc never triggers it.

The Qt editor responds to a valid save by invalidating viewport asset sources and requesting a GPU redraw. Arbitrary edited .vert/.frag files are not yet dynamically bound to active Vulkan/D3D12 PSOs: the renderer providers have no generic hot-swap ABI and some Vulkan shaders are embedded at build time. Implementing true pipeline swap requires extending the provider interface with shader dependency tracking, asynchronous PSO compilation and GPU-safe retirement.

## Files
- engine/northstarcpp/modules/editor/qt_text_asset_editor.hpp and .cpp
- engine/northstarcpp/modules/editor/qt_text_asset_workspace.hpp and .cpp
- engine/northstarcpp/modules/editor/qt_content_browser.hpp and .cpp
- engine/northstarcpp/modules/editor/qt_editor_window.hpp and .cpp
- engine/northstarcpp/modules/editor/qt_editor_window_setup.cpp
- engine/northstarcpp/CMakeLists.txt
- tests/qt_text_asset_editor_smoke.cpp

## Verification
Build: _qa_content_browser_p2
- Qt content browser library and text smoke executable: compiled.
- Text asset smoke: PASS, 0 failures, Qt offscreen.
  Includes real glslc tests: valid fragment sends shader-ready event; invalid fragment reports navigable error and does not send reload.
- Existing Other/Material Function icon smoke: PASS.
- Qt editor setup and main window files: independently compiled using CMake-generated compiler commands.

Main Qt editor linking and desktop visual acceptance not completed. Windows blocked attempts to overwrite in-use northstarcpp_player.exe and render_d3d12.dll during full build. Production editor EXE was not replaced. No Git commit/push. Unrelated working-tree changes untouched.
