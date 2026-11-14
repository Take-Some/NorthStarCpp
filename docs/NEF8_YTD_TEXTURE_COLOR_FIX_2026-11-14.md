# NorthStarCpp - NEF8 YTD texture color correction
Date: 2026-11-14
Status: focused QA PASS; no production installation

## Root cause

sharedAssets/textures/skydome.ytd is an NEF8 ListFile with 5 textures.
Its moon_new entry was tagged NETD ID 104 (BC7), but the bytes are
DXT5/BC3 data, also 16 bytes per 4x4 tile. Decode as BC7 produces
the red/blue corrupted moon preview seen in the browser.

Evidence from mip 0 (128x128; 1024 blocks):
- 184 blocks have an invalid BC7 mode prefix (zero first byte).
- 773 blocks appear as BC7 mode 0 only by coincidence.
- 900 blocks have classic BC3 constant-alpha headers.
- Independent bc7decomp and bcdec libraries agree on the 840 accidental
  syntactically-valid BC7 blocks, 0 mismatches. BC7 logic itself is intact.
- BC3 interpretation reconstructs a real grayscale cratered Moon.

## Implemented fix

The shared NETD parser in engine/northstarcpp/modules/host/
shared_asset_textures.cpp now identifies the highly characteristic
BC3-mislabeled-as-BC7 pattern. It requires at least 32 sampled blocks,
at least 12.5 percent invalid BC7 prefixes, at least half apparent
BC7 mode-0 blocks, and at least two-thirds uniform BC3 alpha headers.
It inspects at most 2048 blocks and only uses the first mip.

If all criteria match, effective in-memory Texture.format_id is changed
from 104 to 103. This fixes the editor and runtime consumers of
shared_assets::Texture. Real BC7 remains 104. The original NEF8 bytes
are not modified, and no asset names are special-cased.

The corrected moon_new QA thumbnail has RGB center (129,131,129):
out/nef8-ue-preview-qa-20261114/editor-visual-regression/
nef8-texture-moon_new.ppm

The importing/writing pipeline responsible for the original incorrect
type tag should also be fixed when its sources are available. This
compatibility classifier does not redefine all NETD format 104 as BC3.

## Material color space

engine/northstarcpp/modules/editor/editor_asset_material_preview.cpp
now uses the original Texture.srgb flag and IEC 61966-2-1 inverse
transfer only for sRGB textures. Linear masks/data values stay linear;
the old unconditional pow(value,2.2) operation was incorrect.
A synthetic same-RGB, different-sRGB-flag regression was added.

## GUI concurrency

Rare Qt Content Browser crashes during concurrent material sphere
generation were reproduced at 3/15 runs. A mutex now protects the
material-only read/decode/render section while the two-worker Qt
pool remains available for textures and meshes.
The revised browser passed 20/20 browsing stress runs; complex Qt
smokes have RUN_SERIAL to avoid unrelated GUI process contention.
A full sanitizer/root-cause audit is still warranted: the mutex is
containment rather than a formal proof of thread safety.

## QA / acceptance

QA executable:
out/nef8-ue-preview-qa-20261114/
northstarcpp_asset_browser_cards_qa.exe

Incremental Ninja build PASS. Focused CTest 9/9 PASS with -j 4.
Checked engine.assets API, BC3-compatible Moon data, legitimate BC7
preview path, material sRGB behavior, thumbnails, Qt cards, modal,
folder artwork, and NEF8 catalog.

Production dist and deployed DLLs were NOT replaced. This does not
approve the separate V8 15.8 or full clean-Windows release gates.
