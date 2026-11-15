# NorthStarCpp — typed Material Graph and character asset namespace

Date: 2026-11-15
Workspace: C:/Users/AidenFox/Documents/NorthStarCpp
Editor: northstarcpp_editor_qt (Qt 6, C++17)
Active QA build: out/material-editor-v3-p0-20261115
Deployment: development source and isolated QA binaries only
Engine/plugin C ABI: unchanged
Production dist: not replaced
Git commit/push: not performed

## Character namespace: characters/abby

All seven imported source assets were migrated from scattered legacy paths
to one bundle beneath sharedAssets/characters/abby/:

- models/abby.ydd, models/abby.ymt
- materials/abby.ymat
- textures/abby.ytd
- animations/mm-explore.ycd, animations/traversal.ycd,
  animations/fall-medium.ycd

Migration ran dry-run, --apply and --verify. Every resource matched its
pre-move SHA-256. No legacy duplicate/alias remains. Binary NEF8 contents
were not modified. A bounded descriptor, abby.character.json, provides
a machine-readable identity, resource paths and animation state mapping.

The First Person Platform project now selects characterRoot=characters/abby
in scripts/playerConfig.js; the model/skeleton/material/texture/animation
URLs derive from that namespace. Its README was updated.

## Content Browser

- AssetKind::character appended (previous enum values preserved).
- A *.character.json source is classified as Character.
- Added Characters filter and colored CHR asset card badge.
- The Inspector reads a bounded JSON descriptor, validates its schema and
  character ID against its parent namespace, displays model, skeleton and
  animation count, and flags missing model/rig references.
- Asset Catalog regression verifies characters/abby hierarchy and exact
  manifest path. No fake project-local mesh/material created.

This is descriptor browsing and metadata inspection. Interactive animated
skeletal preview or drag-and-drop character prefab instantiation remains
future work.

## Imported resource rebasing

Imported YDD references still contain materials/character_abby.ymat@m00,
and imported YMAT texture bindings still use
textures/characters/abby.ytd@entry.

The loader now rebases strictly matching importer conventions:
- models/character.cpp maps imported material selectors for canonical
  characters/<id>/models/<id>.ydd into
  characters/<id>/materials/<id>.ymat@entry.
- material/material.cpp maps matching native YMAT textures into
  characters/<id>/textures/<id>.ytd@entry.
- Character ID validation forbids traversal and unrelated assets; the
  original encoded files are untouched.

The existing load_character_appearance and load_material_definition
contracts remain unchanged.

## Material Graph: typed pins V1

- Added UI nodes: Vector2, Vector3, Dot Product, Normalize, Component Mask
  (x/y/z/w), and explicit Cast (scalar, vector2, vector3, vector4).
- Existing scalar/color/parameter/texture/add/multiply/lerp/operators stay
  supported.
- Pins display source/destination type hints with color coding. The editor
  rejects known incompatible numeric connections and offers Cast.
- Added vector constant editing with bounded finite values and controls
  for Cast target and Component channel.
- Typed runtime graph compilation propagates logical scalar/vector widths,
  validates Dot and Lerp dimensional constraints, rejects incompatible
  vector widths and scalar Normalization, and keeps node-ID diagnostics.
- The final HLSL material graph macros still emit float4, preserving the
  current PBR shader interface and plugin ABI.
- Clipboard filtering recognizes the new node operations and inputs.

Typed pins are an initial local type system, not yet Unreal's full function
signature, sampler, vector conversion and nested Material Function support.

## QA results

Fresh isolated Release build:
- northstarcpp_editor_qt.exe: linked successfully
- render_d3d11.dll and render_d3d12.dll: linked successfully

CTest subset: 11/11 PASS; 0 failures, total wall 16.19 seconds:
- northstarcpp_asset_catalog
- northstarcpp_material_resources
- northstarcpp_skeletal_asset_interop
- northstarcpp_editor_asset_pipeline
- northstarcpp_material_graph_typed
- northstarcpp_material_graph_hlsl
- northstarcpp_character_namespace_rebase
- northstarcpp_qt_material_instance_authoring
- northstarcpp_qt_material_graph_authoring
- northstarcpp_qt_editor_authoring
- northstarcpp_qt_editor_gpu_surface

The HLSL test compiles the typed graph into real D3DCompiler
vs_5_0, ps_5_0 (forward) and ps_5_0 (GBuffer) shaders. These tests check
valid generated shader code but do not constitute a full GPU readback
or colorimetric validation.

WARP Player smoke against the real NorthStarFirstPersonPlatform project:
- --warp --character-smoke --explore --scene ... --shared-assets ...
- exit code 0
- Project-defined textured skeletal draw: PASS
- Engine startup READY with 21 phases passed, 0 warnings and 0 failures
- Selected native renderer: D3D12 WARP; active script provider V8 sidecar

Asset integrity check:
- python tools/migrate_character_assets.py --verify
- seven canonical resources and 8 descriptor resource references validated
- no outstanding legacy aliases.

git diff --check: exit 0; only LF/CRLF conversion warnings.

## Remaining tasks

P1 Preview Studio:
- HDRI/IBL cubemap environment, explicit exposure and lighting rig controls.
- GPU pixel readback and deterministic reference captures.
P1 Material Graph:
- Material Functions/subgraph files with parameter signatures and dependency
  evaluation; full typed sampler/texture and automatic conversion rules.
P1 Shader Compilation:
- actual asynchronous GPU shader/PSO creation and cache persistence across
  Editor/Player runs, stale-work cancellation, shader variant profiling.
P2 UX:
- Animated character asset preview, parameter Inspector improvements,
  asset drag-and-drop, group shortcuts and visual regression suite.

No working-tree reset or deployment occurred. Existing unrelated edits
were preserved.
