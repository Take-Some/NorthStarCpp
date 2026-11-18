# Native Multi-Object Prefab Preview — 2026-11-15

## Objective

The current NorthStarCpp Qt Content Browser previews the entire native
Scene V3 prefab composition in one interactive 3D view, not only its first
object or a generic isometric proxy. The existing native engine.assets
provider, Scene V3 and editor preview renderer remain authoritative.
Neither OBJ conversion nor a new game runtime ABI is introduced.

## Implemented

1. **Native prefab composition**. `editor_prefab_composition.hpp/.cpp`
   builds one combined drawable and one ordered material list from all
   visible Scene V3 actors. Multiple YDD/YDR entries per model and all
   geometry sections are preserved. Per-asset model and material caches
   avoid repeated decoding for multiple instances.
2. **Transforms and layers**. Per-actor position, authored Euler XYZ
   rotation, uniform `size`, nonuniform `node.scale`, and normalized
   inverse-transpose normals are applied once, consistent with the
   existing runtime/instantiate_prefab position contract. Invisible
   actors and layers are excluded. Parent IDs remain logical hierarchy
   identifiers; source scene content is never modified.
3. **Materials**. UV coordinates, mesh section boundaries, source
   YMAT/YTD Base Color bindings and material factors survive composition.
   `northstarcpp.editor.material` Scene V3 per-entity overrides take
   precedence, matching the Player. Missing source resources are
   diagnosed and drawn with visible placeholder geometry.
4. **Lighting and visualization**. The existing software studio
   renderer draws the entire composition through one orbit camera,
   rasterization pass and depth buffer. Face budget increased from
   30,000 to 120,000. Prefab modal has interactive orbit, zoom,
   Wireframe toggle, Reset and studio key-light intensity slider
   (0–250%). The same full 3D composition appears as an asynchronously
   computed asset card thumbnail, falling back to the schematic if the
   asset provider cannot supply native models.
5. **Nonblocking asset decode**. Native model and material resolution
   runs on a QtConcurrent worker in the prefab modal. All widget state,
   material previews and error/diagnostic UI are updated only on the
   main Qt thread. Closing the dialog does not leave worker callbacks
   bound to deleted widgets.
6. **Fault handling**. The project Scene V3 parser is validated before
   preview loading. If a correctly parsed prefab cannot obtain a model
   or provider, the browser retains a schematic composition and a
   warning rather than incorrectly labeling it a corrupt prefab.
   Source safety and path restrictions are unchanged.
7. **Shared Qt ownership**. The existing Material Function editor and
   shared Material Authoring / Material Instances sources were moved
   into the Content Browser library so the standalone browser and
   integrated Qt Editor can link one implementation instead of
   duplicating it.

## Acceptance

Fixture: `Projects/NorthStarFirstPersonPlatform/prefabs/weapon/pistol.nprefab`.

Two native pistol instances, one with an independent transform:
- **43,804 triangles**
- **16 model/material sections**
- **16 of 16 textured sections resolved**
- **0 missing-asset warnings**

`northstarcpp_prefab_composition` verifies real native YDD/YMAT/YTD
loading, per-object transforms, normals and UVs, a shared depth buffer,
light-dependent rendered pixels, duplicated source-model cache reuse,
entity material overrides, layer visibility, immutable source
serialization and missing-model placeholder diagnostics.

`northstarcpp_prefab_composition_qt` constructs an isolated temporary
two-object native prefab, checks the Content Browser card, waits for
asynchronous 3D preview generation, opens the real Qt modal, checks both
actors and changes lighting while verifying the actual widget pixels.

QA build: `out/asset-warnings-qa-20261115`.
Latest scoped CTest: **11/11 passed** (Prefab UI/geometry, catalog,
cards, folder icons, NEF8, authoring, warnings).

## Boundaries / next iteration

- Studio 3D is a **software editor preview**, using native UV / Base
  Color textures and ambient/directional/fill/specular lighting. It is
  not Vulkan/D3D12 full GPU PBR, does not execute custom Material Graph
  shaders, and does not produce runtime shadows or simulate physics.
- Unsupported scene component geometry uses transparent diagnostics
  plus editor proxy cubes; native YDD/YDR models are the tested fully
  composed path. A genuine production YDR fixture and comparison with
  GPU Player remain open.
- 120,000 triangles and 600,000 source vertices are explicit preview
  caps. Oversized compositions fall back to a schematic and a warning.
- Future: an isolated GPU Preview Studio render target using the same
  engine.render Render Graph, including all native material graph
  variants, runtime lights, skeletal animation and component geometry.
- The full Qt Editor production executable was not rebuilt/deployed
  in this task; Content Browser and its standalone QA host were built
  and directly tested.

No production `dist` replacement. No commit or push.
