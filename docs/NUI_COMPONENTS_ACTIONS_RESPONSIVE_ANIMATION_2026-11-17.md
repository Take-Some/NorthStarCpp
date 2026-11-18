# NorthStarCpp — Reusable XML UI components, events, breakpoints, animation

Date: 2026-11-17. Scope: NorthStar UI XML v3 extension.
Release build and GPU acceptance still pending.

## New XML sections

The persisted .nui format remains UTF-8 XML version=3. Optional sections:

- Components — reusable **single-widget** templates with a stable name, type
  (panel, text, button, image, input) and typed Property values.
- Elements/Use — creates a named instance of a Component with its own ID,
  optional parent and style, plus typed local property overrides.
- Actions/Action — declarative mapping of (id,event) to a **named engine
  event** via Events.emit. No executable scripts are embedded in XML.
- Animations/Animation — numeric-property timelines, linear, easeOutCubic,
  easeInOut; mount or manual trigger; named actions may play an animation.
- Breakpoints/Breakpoint/Override — apply typed property overrides when the
  native viewport width is within the declared inclusive range.

The first complete example is:
  sharedAssets/ui/component_gallery.nui

That example declares one menuAction Button template, two Use instances,
a small-window breakpoint at 720 px, one mount animation, and an action-linked
button animation.

## Runtime behavior

XML->descriptor validation happens in C++:
  engine/northstarcpp/modules/shared/ui_xml_codec.hpp

UI.loadAsset merges properties in this order:
  component template < named style < instance properties < matching breakpoint.

Design tokens are resolved after property merging. All final nodes go through
the existing engine.ui.apply and native renderer. No new GPU or scripting ABI
was introduced.

Actions register callbacks on their expanded stable element IDs and emit
Events.emit(name,{source:"nui",id,layer,event}). Application code handles
that named event with Events.on(name,handler). Only click, press and change
are exposed for declarative mapping. Re-registering on resize or XML hot
reload removes the previous generated callbacks, leaving user-installed
callbacks untouched.

Animation progression is driven by the existing engine ui.update game
event. It interpolates x,y,width,height or font_scale for up to 30 seconds
per clip. Numeric changes are set through UIRoot.setTransient, so the
animation does not overwrite persistent gameplay .set/.setText bindings.
The author can call UI.playAssetAnimation(root,name) to play a manual track,
or UI.advanceAnimations(dt) to advance deterministically in tests.

Breakpoints recompose on viewport size changes using engine.ui.viewportSize,
which reads the Win32 viewport client area and returns width and height.
Scripted UI checks viewport changes on a 500ms cadence. A packaged Player
still supports responsive recomposition even if disk hot reload is disabled.
Project-owned .nui assets remain higher-priority than shared assets.

The standalone FPS mode independently decodes Use component properties and
applies breakpoint overrides to its typed UiDocument. Visible FPS UI is
recomposed on resize.

UIRoot.dispose releases the owning UI asset tracking state, including in
packaged games, to prevent retaining orphaned roots.

## Qt Editor integration

The Qt serializer and native preview preserve Component, Use, Action,
Animation and Breakpoint XML information without discarding it.

The UI Designer retains hierarchy/typed properties/dragging and adds
**Edit XML Source...** for the full versioned document. Invalid edits are
rejected before replacing the live native preview, and the undo/redo
history remains enabled.

For now the full advanced authoring view is XML source, not a dedicated
visual graph editor. A single-widget component is implemented; multi-node
prefab templates, nested component composition and external component asset
imports remain future extensions.

## Compatibility and safety

- Existing XML v3 assets with only Tokens/Styles/Elements remain valid.
- The complete C++ compiler, Python authoring bridge, Qt serializer and XSD
  schema accept the same extension sections and preserve them on round trip.
- Limits: 64 components, 128 actions, 128 animations, 64 breakpoints,
  128 breakpoint override targets per breakpoint, 256 final nodes and 256 KiB.
- XML is bounded and rejects DTD/entity declarations, invalid parent order,
  unknown templates, invalid property types, nonfinite animation numbers,
  unsupported event mapping and invalid referenced element IDs.
- XML never executes JavaScript. Named events dispatch to project scripts
  in the existing restricted scripting runtime.

## Tests

Run from repo root:

    node tools/dev/nui_composed_runtime_smoke.mjs

Run the embedded SDK integration with NUI_SDK_MIRROR=1.

Other checks:
- tests/nui_xml_v3_smoke.cpp
- tests/nui_xml_qt_roundtrip_smoke.cpp
- tests/fps_nui_asset_smoke.cpp
- tools/dev/ui_asset_smoke.mjs
- tools/dev/ui_project_invariants.mjs
- powershell -ExecutionPolicy Bypass -File tools/dev/nui_schema_smoke.ps1

These cover template inheritance and overrides, button events, timeline
interpolation, viewport resize, duplicate handler prevention, XML parse,
XSD validation, Qt source roundtrip and FPS native decoding.

## Remaining QA and development

Full Release build, Vulkan and D3D12 visual checks, live UI input in a
packaged Player, responsive DPI tests, keyboard focus and animation GPU
frame profiling have not been completed by these headless/syntax tests.

Next major additions: multi-widget component prefabs; reusable external UI
component files; breakpoint preview/simulator in Qt; multi-track and color
interpolation; easing curves editor; animated layout/opacity and style
transitions; performance instrumentation for large UI graphs.
