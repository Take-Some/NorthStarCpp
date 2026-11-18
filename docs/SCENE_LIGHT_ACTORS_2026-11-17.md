# NorthStarCpp: typed scene lights (Point, Spot, Directional, Sky)

17 November 2026. Editor-authored scene lights are ECS components, never
renderer-specific hardcodes. The scene itself remains a native Scene V3.

## Place Actors

Category **Lights** and Outliner **Add > Lights** expose:

| Item | Scene payload v2 kind | Illumination |
| --- | --- | --- |
| Point Light | point | Local omnidirectional source with distance attenuation |
| Spot Light | spot | Local cone with falloff, editable axis and cone angle |
| Directional Light | directional | Scene-wide PBR direct lighting independent of distance |
| Sky Light | sky | Uniform sky/ambient fill over surfaces, subject to AO in deferred |

The standard actor transform handles positions and rotation; selecting a
light shows **Light | Dynamic Scene Source** in Details Properties:

- Type (switch between the four lights on an existing entity)
- Intensity (nonnegative linear multiplier)
- Range (Point/Spot only)
- Color (linear RGB, up to 100 for HDR)
- Direction XYZ (Spot/Directional, local vector; follows actor Euler rotation)
- Spot outer cone angle (1–89 degrees)
- Apply Light (one undoable Scene transaction)

Actors remain selectable, renameable, hideable and persistable. A hidden light
does not contribute to the viewport. Scene origin, animation, static mesh and
physics components remain independent of lighting components.

## Versioned scene component encoding

Component type: `engine.render.light`

v1 remains supported: `x y z range r g b intensity dx dy dz coneCos`
(negative cone sentinel means Point, >=0 means Spot).

v2: `kind x y z range r g b intensity dx dy dz coneCos`

Example:

```text
point 0 0 0 18 1 0.94 0.78 8 0 -1 0 -1
spot 0 0 0 26 1 0.94 0.78 12 0 -1 0 0.82
directional 0 0 0 1 1 0.94 0.78 2 0 -1 0 -2
sky 0 0 0 1 1 0.94 0.78 0.3 0 -1 0 -3
```

The codec normalizes legacy negative point markers, rejects invalid type/cone
combinations and nonfinite or invalid ranges, intensities and directions.
The 512-byte payload limit and scene-level entity validation remain.
Changing properties uses `editScene` and therefore the normal Undo/Redo,
scene validation and save pipeline.

## Renderer ABI and GPU shaders

The dynamic-light C ABI is unchanged:
`NorthStarRenderLight` remains **48 bytes**, ABI version 1. We reserve
`direction_cone.w` semantic tags:

- `-1`: Point
- `0..1`: Spot, cosine of outer cone angle
- `-2`: Directional
- `-3`: Sky

The global light tags are added to **every tiled light list**; neither
position nor attenuation range is used for global sources. D3D12 HLSL deferred
shader and Vulkan GLSL deferred/forward paths evaluate the two new tags.
Regenerated Vulkan SPIR-V headers are checked in as generated source
because the runtime loads embedded shader variants. D3D12 and Vulkan GPU
validation now accepts the reserved global tags while rejecting invalid
intensity and zero-length oriented axes.

## Current limitations

Sky Light currently means a uniform ambient approximation, **not** a
reflection-captured / irradiance-convolved HDRI sky. No light-specific local
shadows, cascaded shadow maps for newly placed Directional Lights, volumetric
scattering or IES profiles are provided by this change. The existing
fixed-direction shadowed sun remains part of the deferred defaults.

The D3D11 compatibility provider's lighting result has not been visually
verified for the two new global kinds. Full viewport image parity across
D3D12/Vulkan, shadow correctness, editor GPU acceptance, and performance on
large light scenes require separate profiling and visual QA.

## Tests

- `northstarcpp_light_kinds`: typed v2 roundtrip, legacy v1, invalid data,
  global tile coverage and rotation inheritance
- `QtEditorWindow::runPlaceActorsSmoke`: all four actor factories, color/
  intensity editing, Outliner filtering, Undo
- `tools/generate_vulkan_deferred_spirv.py`: shader compilation variants
- `tools/generate_vulkan_material_spirv.py`: material forward/GBuffer variants

The test suite must pass before replacing or publishing production DLLs.
A DLL may be held open by an existing editor process on Windows; never
terminate that process without explicit user approval.
