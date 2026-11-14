# NorthStarCpp — engine.perlin (ABI 1)

The native `engine.perlin` capability generates deterministic, seeded, gradient-based
noise for terrain, cloud masks, procedural textures, vegetation placement and other
authoring/runtime systems. Improved Perlin is linked into the engine. A compatible external provider can replace it through the standard northstar_module_query capability contract.

## Public API

C: `<northstarcpp/perlin_plugin.h>`, version `NORTHSTARCPP_PERLIN_ABI == 1`.
C++: `<northstarcpp/perlin.hpp>`, link `NorthStarCpp::northstarcpp_runtime`.

| Entry point | Behavior | Range |
| --- | --- | --- |
| `sample1(x)` | Signed 1D Perlin gradient noise | [-1, 1] |
| `sample2(x,y)` | Signed 2D Perlin gradient noise | [-1, 1] |
| `sample3(x,y,z)` | Signed 3D Perlin gradient noise | [-1, 1] |
| `fractal1(...)`, `fractal2(...)`, `fractal3(...)` | Multioctave fBm, turbulence or ridged mode | [-1, 1] for fBm; [0, 1] otherwise |
| `fill2(...)` | Batch-generate a 2D grid (dense or explicit row stride) | Same as selected fractal mode |
| `provider_name()`, `provider_version()` | Provider identity | `Improved Perlin` / `1.0.0` |

Example C++:

```cpp
#include <northstarcpp/perlin.hpp>
#include <vector>

northstarcpp::PerlinNoise noise({123456u, 256, 256, 256});
float waveform = noise.sample1(12.25);
float height = noise.sample2(12.25, -4.75);
float volume = noise.sample3(0.25, 1.5, 3.25);

northstarcpp::PerlinFractal terrain;
terrain.octaves = 6;
terrain.mode = northstarcpp::PerlinMode::fbm;
terrain.frequency = 0.025;
terrain.lacunarity = 2.0;
terrain.persistence = 0.5;

auto heights = noise.fill2(0, 0, 1, 1, 512, 512, terrain);
```

For non-owning, row-strided buffers:

```cpp
std::vector<float> buffer(528 * 512);
noise.fill2(0, 0, 1, 1, 512, 512, 528, terrain, buffer.data());
```

## Seeding and tiling

The default seed is 0. The reference provider uses SplitMix64 to seed a
Fisher–Yates permutation over 256 lattice entries and a quintic fade curve.
Equal seeds/configurations and coordinates produce identical floating-point
results on the same ABI/provider. A different seed changes the gradient field.

`period_x`, `period_y` and `period_z` are integer lattice periods in [1, 256];
zero in the C ABI means 256. For example, setting X=17 and Y=31 gives
`sample2(x + 17, y) == sample2(x, y)` and
`sample2(x, y + 31) == sample2(x, y)` within float rounding.

**Fractal tiling:** Octave frequencies must be compatible with the desired
world-space repetition. With frequency 1 and integer lacunarity 2, the same
lattice periods repeat across every octave. Arbitrary nonintegral frequencies
do not guarantee tileable fBm textures even though each source lattice is
periodic.

## Fractal settings

- `octaves`: 1–16, defaults to 5.
- `frequency`: finite positive input scaling (default 1).
- `lacunarity`: finite >= 1, defaults to 2.
- `persistence`: finite within [0,1], defaults to 0.5.
- `mode`: `fbm` (signed weighted sum); `turbulence` (weighted absolute value);
  `ridged` (weighted squared `1 - abs(noise)`).

Fractal outputs are amplitude-normalized, and each call is deterministic.
Sampling and filling are read-only operations; the initialized instance can
be shared between CPU workers without locking.

**Validation:** All coordinates, grid steps and final octave-scaled coordinates
must be finite and within +/-1e9. Grid dimensions must be nonzero, and at most
4,194,304 cells can be generated per call. A supplied stride is measured in
`float` elements and must be at least the width. A C API function returns 1 on
success and 0 on error, with failure details available from the provider's
`last_error()`; the C++ wrapper raises an exception. User-provided output
buffers must have room for `(height-1)*row_stride + width` float elements.

## Provider lifecycle and deployment

The distribution declares Improved Perlin as builtin in northstarcpp.plugins.json. Place the external module in the plugin directory for automatic selection, or select it explicitly:

```powershell
$env:NORTHSTARCPP_PERLIN_PLUGIN = "C:\\path\\to\\my_perlin_provider.dll"
```

The same contract applies to Linux .so files. External modules export NorthStarPerlinApi and advertise engine.perlin ABI 1. This table drives native sampling, procedural environment and world authoring, and the JSON bridge. See [module selection](MODULES.md).

## Verification

```powershell
cmake --build out/shader-generator-resources-20261113 --target northstarcpp_perlin_smoke
ctest --test-dir out/shader-generator-resources-20261113 -R northstarcpp_perlin --output-on-failure
```

The regression suite covers ABI negotiation, 1D/2D/3D calls, invalid parameters, deterministic
seeded samples, periods and negative coordinates, fBm/turbulence/ridged modes,
packed and strided grids, and multithreaded sampling.

**Integration status:** [Clouds, terrain generator, and YSC JSON dispatcher](PERLIN_INTEGRATION.md).

**Scope:** This is a reusable CPU procedural-noise capability. The
`engine.render` skydome remains GPU-driven and does not automatically switch
to this provider. A GPU Perlin implementation, texture caching, 4D noise and
compute-driven pipelines are separate future extensions.
