# NorthStarCpp — engine.ui (provider ABI 1)

## Scope
`engine.ui` defines a single provider-neutral contract for creating, updating, and
rendering user interface elements. It does not require Chromium, Qt, Vulkan,
D3D12, or a particular markup language.

Public headers:
- `engine/northstarcpp/modules/api/include/northstarcpp/ui_plugin.h` (C ABI)
- `engine/northstarcpp/modules/api/include/northstarcpp/ui.hpp` (C++ helpers)
- `engine/northstarcpp/modules/api/include/northstarcpp/module.h` (capability constant)

## Provider discovery
Like other engine capabilities, providers expose
`northstar_module_query(NORTHSTARCPP_MODULE_ABI, "engine.ui", NORTHSTARCPP_UI_ABI)`.
The returned `NorthStarModule` must advertise `engine.ui`, API version 1,
and an API table of at least `sizeof(NorthStarUIApi)`; `api_size` is
checked by the existing module loader. External providers can override
builtin providers according to the existing module selection policy.

## Contract
1. One context owns a hierarchy of elements; `0` is an invalid element
   handle and is used for a root's parent.
2. Each element has a stable name, kind, parent and provider-defined properties.
   Parents and children must belong to the same context.
3. `set_property` accepts typed scalar and UTF-8 string values. Providers
   copy string input before returning. `get_property_json` writes
   caller-owned output, reports required bytes, and does not allocate across
   module boundaries.
4. Providers consume incoming events with `dispatch_event`. UI-generated
   events reach the host callback only during `pump_events`. The callback
   must not recursively enter the provider. Events and their string storage
   are borrowed for callback duration.
5. `resize` sets logical dimensions and DPI scale; `render` may report
   an opaque texture when `NORTHSTAR_UI_FEATURE_GPU_TEXTURE` is present.
   Texture ownership and synchronization must be specified by each GPU
   provider before integrating with a renderer.
6. All provider entrypoints return a status code and never throw across
   the C ABI. `destroy_context` invalidates every element and texture.
7. ABI 1 API calls are serialized on the provider UI thread. Multithreaded
   callers must schedule their work onto that thread.

## Supported families
The base kinds are root, panel, text, button, image, input and custom.
Backend-specific capabilities are declared via feature flags rather than
requiring every implementation to support every platform.

## Native provider implementation (2026-11-15)
The first provider is compiled into `northstarcpp_runtime` using the existing
`northstarcpp_builtin_module(... ui native ...)` registration path.
It is selected via `northstarcpp::Module{"engine.ui"}`, with the existing
external plugin override policy unchanged.

Implementation: `engine/northstarcpp/modules/ui/native/native.cpp`.
Validation: `tests/ui_native_smoke.cpp`, CTest `northstarcpp_ui_native`.
The provider owns a tree of elements, typed scalar/string properties, a bounded
4096-event queue, context lifecycle, and recursive child destruction.
It validates handle/context ownership, status codes, callback reentrancy,
UTF-8 string buffer boundaries, DPI and sizes. For property reads, required
byte count includes the terminating NUL.

## Native raster ABI 2 / editor preview (2026-11-15)

The original engine.ui ABI 1 remains byte-for-byte compatible. A second
versioned table, `NORTHSTARCPP_UI_RASTER_ABI=2`, is exposed by the native
provider and queried through `Module::query_extension<NorthStarUIRasterApi>(2)`.

Implementation:
- `modules/ui/native/native_raster.inc` — deterministic CPU rasterization,
  nested rectangles, padding/gap, primitive 5x7 Latin glyphs, element hit tests,
  press/release-to-click event delivery, and an RGBA8 framebuffer
- `modules/editor/qt_native_ui_preview.hpp` — Qt image presenter and pointer
  adapter around the engine.ui provider. Open through **View → Native UI Preview**
- `tests/ui_native_smoke.cpp` — validates actual RGBA pixels, element rectangles,
  pointer click events and stale handle rejection
- `--native-ui-smoke` on `northstarcpp_editor_qt` — opens the dock, verifies
  nonempty pixels and delivers one mouse click
- CTests `northstarcpp_ui_native` and `northstarcpp_qt_editor_native_ui`

The native raster implementation currently uses absolute x/y and width/height
properties plus vertical sibling flow, parent clipping, padding and gap.
It is a minimal layout implementation, not flexbox/grid. Text currently
supports a compact built-in Latin/digit font and placeholder characters for
unsupported glyphs; Unicode shaping, font atlas caching and complex text layout
are not implemented.

**GPU rendering and compositing are not implemented.** The raster API writes
CPU-owned RGBA8 pixels and the Qt adapter presents them with QImage/QPainter.
The original ABI 1 `render` continues to return `NORTHSTAR_UI_UNSUPPORTED`
and provider feature flags remain zero. This avoids advertising unavailable
GPU capabilities. Game Runtime and the existing HUD are unchanged.

## Incremental layout / compositing groundwork (2026-11-15)

The CPU reference layout now supports `layout=column|row|grid`,
`columns`, `grid_row_height`, `width_percent`, `height_percent`,
`min_width`, `min_height`, `max_width`, `max_height`,
`anchor_right`, `anchor_bottom`, `scroll_x`, `scroll_y`,
`padding`, and `gap`. These are typed UI element properties.
The implementation is deterministic with parent clipping, but is **not**
a full flexbox/grid constraint solver and lacks input scrolling, intrinsic
text measurement, wrapping and accessibility geometry.

`northstarcpp/ui_cpu_compositor.hpp` adds an independently testable
straight-alpha RGBA8 source-over-opaque reference for renderer parity
checks. This is strictly a **CPU** reference and does not provide Vulkan
or D3D12 GPU texture upload, synchronization, rendering or presentation.
The pixel blend formula operates on byte channels; production GPU shaders
should perform blending consistently with the selected linear/sRGB policy.

UTF-8 text is now parsed by code point with invalid sequences handled
safely. Glyphs beyond the built-in Latin raster font are still drawn as
single replacement glyphs. FreeType and HarfBuzz headers are available in
the local MSYS2 environment, but they are not linked or integrated into
the engine. Complex text shaping, proper Cyrillic and CJK glyphs,
selection and IME remain unimplemented.

Validation: rebuilt `northstarcpp_ui_native_smoke` and passed its CTest.
The Qt Editor executable could not be relinked in this pass because the
existing `northstarcpp_editor_qt.exe` is locked (Permission denied);
do not treat the earlier Qt smoke as a new-build result.

## Optional shaped text and Input ABI 3 (2026-11-15)

`native_font.inc` provides FreeType glyph rasterization and HarfBuzz shaping
for supported fonts, including Cyrillic. Glyph bitmaps are cached per
thread/size (up to 2048 glyphs). A font is selected via
`NORTHSTARCPP_UI_FONT` or platform fallbacks (Segoe UI/Arial on Windows,
DejaVu/Liberation on Linux). The provider still supports the bitmap fallback
when its optional font dependencies are absent. CMake uses pkg-config for
`freetype2` and `harfbuzz` and defines
`NORTHSTARCPP_UI_HAS_FONT_SHAPING` only after finding both. On the current
Windows MSYS2 build both packages were detected and that definition was
present in the generated Ninja build.

This does not yet include a GPU glyph atlas, line breaking, rich typography,
text selection/caret rendering or font asset packaging. Dynamic library
distribution for FreeType/HarfBuzz requires release staging checks.

`NORTHSTARCPP_UI_INPUT_ABI=3` is an additive `NorthStarUIInputApi`
provider extension with keyboard UTF-8 text, IME preedit/commit, wheel scrolling,
and focused handle query. Focus selects a native INPUT element on mouse press;
backspace removes one UTF-8 scalar, and changed text is queued as a CHANGE
event. Context destruction clears focus and old handles. Qt
`NativeUIPreview` now forwards key, IME, and wheel events to this extension.
The logical provider passes direct API input regression tests. Qt Editor
relink and fresh Qt input smoke tests have not yet completed in the latest
pass.

`tests/ui_native_smoke.cpp` includes a 1000-frame CPU raster benchmark at
320x180, with p50/p95/p99 reporting, but its latest rebuild was interrupted
by a build timeout; no new metrics should be cited as verified.

## GPU UI compositor and runtime opt-in (2026-11-15)

New optional renderer contract `northstarcpp/render_ui_api.h`,
`NORTHSTARCPP_RENDER_UI_MODULE_ABI`, extends `engine.render` without
modifying renderer ABI 5. `set_rgba` copies caller-owned straight-alpha
RGBA8 pixels at submission; a null image clears the overlay. Providers that
omit the extension are unchanged and must not advertise GPU UI support.

D3D12 implementation: `modules/render/d3d12/renderer_ui.cpp`; records
a fullscreen triangle with a sampled RGBA texture, staging/upload heap,
COPY_DEST -> PIXEL_SHADER_RESOURCE transitions and alpha blending in the
final pass after scene tonemapping/HUD. The renderer keeps resources per
instance. The existing frame fence serializes upload reuse.
`northstarcpp_d3d12_warp` rebuilt and passed a real GPU readback test
(mode 9) sampling a known red UI image, after the caller destroyed its
original pixels.

Vulkan implementation: `modules/render/vulkan/renderer_ui.cpp`,
`ui_overlay.vert`, `ui_overlay.frag`, generated `ui_overlay_spirv.hpp`.
Creates a staging buffer, VK_FORMAT_R8G8B8A8_UNORM sampled image,
image layout transitions, descriptor set, alpha-blended dynamic-rendering
pipeline. Adds `engine.ui overlay` before GPU readback in Vulkan Render
Graph; the existing completed-frame fence/queue synchronization owns staging
reuse. `northstarcpp_vulkan_hardware` rebuilt and passed GPU readback
mode 8 on a known red overlay.

The Win32 viewport has an **opt-in** demonstration path using
`NORTHSTARCPP_UI_DEMO=1`. When FPS rendering is active and the selected
renderer supports `NorthStarRenderUiApi`, the viewport initializes
`engine.ui`, creates a small status panel, rasterizes the UI and submits
its RGBA8 pixels before drawing the scene. If the variable is unset, default
gameplay/editor behavior remains unchanged. `northstarcpp_viewport`
compiled successfully. Complete Player integration and the game input bridge
have **not** passed a launch smoke yet; Player build hit a timeout.

Input ABI 3 provides keyboard, IME preedit/commit, focus and scroll to Qt
preview and native UI context. A complete Player input adapter, caret,
selection, keyboard navigation and clipboard are not yet implemented.

Font rendering in `native_font.inc` uses FreeType/HarfBuzz when available
and bitmap fallback otherwise. It is CPU glyph caching, **not** a GPU atlas;
font dependency redistribution and complex scripts need further QA.

CPU UI-raster benchmark: 320×180, 1000 frames, CTest success;
p50 0.0218 ms, p95 0.0348 ms, p99 0.1272 ms.
These are CPU-only measurements; GPU UI compositor p50/p95/p99, 1000+
gameplay frames, memory-growth and long-run resize tests remain open.

## Next integration step

- Validate Player opt-in with both D3D12 and Vulkan in actual game scene,
  including mouse/keyboard/IME native window event routing.
- Extend layout to intrinsic text measurement, flex grow/shrink, content
  scroll bounds, percentages under transforms, and accessibility metadata.
- Add real editing caret, text selection, IME visual underline, clipboard
  and keyboard navigation; resolve shaping font redistribution/asset loading.
- Benchmark 1000+ scene frames p50/p95/p99 across D3D12 and Vulkan,
  verify color space (sRGB vs linear), alpha parity, resize, memory residency,
  synchronization and provider overrides.
- Eliminate per-frame texture upload when UI is unchanged, and implement
  fence-safe frame-in-flight staging. Run GPU validation layers.

## Acceptance tests for provider implementation
- Create/destroy context and nested elements; reject stale handles.
- Round-trip all typed properties including Unicode strings.
- Dispatch input and observe ordered callbacks; validate no reentrancy.
- Resize and DPI changes; present offscreen on Vulkan/D3D12 with GPU fences.
- Confirm provider replacement (builtin / DLL), ABI mismatch rejection,
  error propagation, unload safety and no leaks after repeated teardown.
