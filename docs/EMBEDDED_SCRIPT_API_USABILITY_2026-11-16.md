# Embedded NorthStarCpp Script API — usability pass

2026-11-16. Authoritative sources: `engine/northstarcpp/resources/api`.
CMake embeds its JS and DTS into the native runtime; legacy
`sharedAssets/scripts` is not the executable SDK source.

## Delivered

- `UIRoot.group()`: nested UI builder without repeated parent IDs.
- `UIRoot.has()` and `snapshot()`: safe inspection of the script UI tree.
- Atomic nested `batch()` with rollback on exceptions.
- Change detection in `set()`, `setProgress()`, `mount()` and low-level
  `UI.mount()` to avoid unnecessary `engine.ui.apply()` calls.
- Better duplicate-element and missing-parent diagnostics.
- Transactional `UI.unmount()` / `UI.clear()` and `UI.hasLayer()`.
- `Scene.require(index)` and `Scene.requireByName(name)`.
- `Player.hasPawn` and `Player.requirePawn()`.
- `Input.normalized2D()`, used by the standard movement root.
- Updated TypeScript authoring declarations and SDK README examples.

The native engine continues to know only generic presentation structures,
not missions, HP, inventory or advertising rules.

## Sealed engine API and project scripts

The current loader explicitly seals `scripts/ui/UI.js` and
`scripts/character/Character.js`; project files cannot replace these
engine infrastructure modules. Gameplay policy roots remain overridable.
Game-specific interfaces are created through `UI.createRoot()`.

## Verification

| Test | Result |
| --- | --- |
| Embedded JavaScript syntax | 18/18 PASS |
| Embedded API generation | 19 packaged files |
| C++ Player and embedded catalog smoke target | PASS |
| CTest selected group | 9/9 PASS |
| Native V8/Jolt/D3D12 UI startup | PASS |
| Project copy of sealed UI root ignored | PASS |
| Input enum declaration generator | PASS |

Full Release regression, TypeScript compiler check (tsc unavailable),
interactive controls, GPU visual acceptance, render-to-texture and
complete manual gameplay verification are outside this test scope.

No production installation, commit, or push was performed.
