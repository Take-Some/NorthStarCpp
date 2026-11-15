# NorthStarCpp — Project Browser renderer compatibility and official build

Date: 2026-11-15

## Scope

The official Qt Project Browser now presents a **Compare Render Features**
window and live compatibility warnings for selectable engine.render providers.
It avoids treating base Render ABI 5 compatibility as full rendering feature
parity. The previous per-project provider preferences and strict selection
behavior are preserved.

## Provider feature contract

Added `engine/northstarcpp/modules/api/include/northstarcpp/render_features_api.h`:
a standalone optional extension `NORTHSTARCPP_RENDER_FEATURES_MODULE_ABI`
(`0x52465001`), with a fixed-size POD table of compiled-feature bits.
The base Render ABI 5, module host ABI 1, and existing extension ABIs
remain unchanged.

D3D12, D3D11 and Vulkan publish declarations in their existing
`northstar_module_query` entrypoints. The declarations distinguish
implemented shader/pass code from actual runtime hardware acceptance.
No new feature was implemented in this change; the bits describe work
already present in those providers.

Project Browser's live inventory validates the optional extension tables
for:
- geometry triangles
- textured/PBR material batches
- Material Graph v2 batches
- GPU skinned materials
- world surface textures
- character models
- dynamic lights
- GPU timing diagnostics

For each, it checks the plugin's exported function table and ABI version.
It separately inspects the optional renderer feature declaration for
G-buffer, deferred rendering, CSM, PCSS, SSAO, HDR, bloom, exposure,
tonemapping, FXAA and transient GPU aliasing.

The GUI deliberately uses separate statuses:
- **ABI exposed**: an extension function table is present and validated.
- **ABI unavailable**: that extension cannot be negotiated.
- **Declared**: the provider reports an implemented GPU feature, without
  claiming it is enabled at runtime or supported by the active adapter.
- **Not implemented**: the provider declares that feature absent.
- **Not reported**: no valid feature declaration was provided.

This is a source- and ABI-based capability inventory, not equivalent to
pixel parity, performance testing or in-game acceptance.

## User-facing behavior

The per-project `ENGINE API PROVIDERS` dropdown includes provider version,
source kind and experimental Vulkan status. A new **Compare Render Features**
dialog displays the complete capability matrix across available renderers.

Switching a renderer updates a compatibility summary in Project Browser.
The first-open multi-provider dialog presents the same warning alongside
the selection. When the chosen provider does not publish required PBR
material or Material Graph extensions, launching the editor requests
explicit confirmation to use reduced rendering functionality. If a
selected provider is unavailable or ABI-incompatible, the existing
strict validation still blocks launch.

The main Browser smoke tests the actual extension inventory:
D3D12 reports Material Graph and declared HDR, D3D11 reports the World
Surfaces API, Vulkan does not report HDR or Material Graph, but does
publish the GPU timing ABI. This prevents accidental hardcoded
"Vulkan supports everything" claims.

## Build and verification

**Official built artifacts**, in:
`out/project-manager-qt/`

- `northstarcpp_project_manager.exe`
- `northstarcpp_editor_qt.exe`
- `northstarcpp_player.exe`
- `render_d3d11.dll`
- `render_d3d12.dll`
- `render_vulkan.dll`

Qt deployment copied/verified 32 DLLs.

CMake release targets were built successfully, after an intermediate
transient contention during Content Browser static archive replacement.
A subsequent final relink attempt after a whitespace-only change to
\`qt_project_browser.cpp\` failed because Windows locked the already
built \`northstarcpp_project_manager.exe\` (\`ld.exe: Permission denied\`).
The official executable built successfully before that change and the
official CTest \`northstarcpp_project_manager_window\` passed on the
existing executable afterward. The only additional source difference is
removal of trailing whitespace. To replace the executable, release its
process/file lock and rerun the Project Browser target build.

Official CTest selection:
`northstarcpp_(project_manager_window|project_manager_catalog|provider_select_.*)`

**8/8 passed**. Coverage includes the Qt GUI with validated feature
manifest assertions, catalog persistence, real D3D11/Vulkan/Jolt/AssetManager
module selection and strict missing-provider/path-conflict rejection.

The build did not re-run a full engine-wide test suite or an actual Night City
GPU scene; neither functional Vulkan parity nor real-world hardware support
can be inferred from these tests.

## Change control

Changes remain in the live NorthStarCpp source and its QA/release build tree.
No production `dist` replacement, Git commit or push was performed. All
previously existing unrelated uncommitted source edits were preserved.

## Follow-up

- Add a dedicated feature-manifest ABI C++ unit test and a second runtime
  validation step that creates the chosen renderer and checks GPU feature
  availability before gameplay.
- Extend capability discovery to arbitrary signed/manifested third-party DLLs.
- Extend per-project compatibility requirements to distinguish mandatory
  rendering features from optional ones.
- Measure identical scenes and GPU timestamps in D3D12/Vulkan, then run
  1000-frame Night City QA once that project is available.
