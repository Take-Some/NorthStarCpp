# NorthStarCpp 2026 — Engine API/ABI provider synchronization

Date: 2026-11-14
Scope: first-class owned engine modules, host ABI loader, Player and Qt Editor.

## Integration

The host now exports `northstarcpp::inspect_engine_providers()` in the
first-class public API header:

`engine/northstarcpp/modules/api/include/northstarcpp/provider_inspector.hpp`

Implementation:

`engine/northstarcpp/modules/host/provider_inspector.cpp`

This is a read-only inspection of new module resolution. It uses the exact
same `Module::query<Api>(version)` and ABI table validation as the game,
including automatically discovered DLL/SO providers, author-selected
overrides, and native builtin fallbacks. It neither creates physics/render
devices nor changes a provider in an existing running instance.

It reports capability ID, optional requested implementation, provider name,
provider version, requested API ABI, builtin or external source, and error
details. The host `Module` exposes read-only `source()` and
`is_external()` accessors, not duplicate plugin discovery logic.

## Editor functionality

Qt Editor contains a dockable **Engine Providers** inspector under View:

- Refresh ABI-resolved providers.
- Inspect source path and version; incompatible providers show errors.
- Copy a tab-separated report.
- **Compare with Player** launches `northstarcpp_player.exe
  --inspect-providers` asynchronously, respecting the configured Player
  executable, and compares capability/selector/provider/version/ABI/availability.
  Mismatches are written to Output Log.
- A ten-second watchdog stops a stuck diagnostic process without freezing
  the authoring interface.

The comparison identifies configuration/environment differences between the
Editor and Player. It does not hot-reload a running game. External paths are
shown but not treated as mismatches when equivalent providers are deployed
to different installation directories.

## Game Player functionality

`northstarcpp_player.exe --inspect-providers` prints a TSV report with the
same provider-resolution model. It does not start a scene or create a GPU
device. This mode can be used in CI, plugin packaging and troubleshooting.

## ABI policy

1. Explicit author-supplied DLL/SO (environment variable before bootstrap).
2. Auto-discovered compatible external provider.
3. First-class builtin provider.

A rejected explicit override is reported as an error; it is never silently
replaced by the builtin. Invalid auto-discovered modules do not hide a
valid builtin. The inspector also checks D3D12 and D3D11 provider requests
independently on Windows.

## Regression coverage

- `northstarcpp_provider_catalog_sync`: all declared `engine.*`
  capability macros must appear in the shared inspector.
- `northstarcpp_provider_inspector_builtin`: builtin geometry and
  mandatory core capabilities are resolved.
- `northstarcpp_provider_inspector_override`: an author DLL selects its
  own geometry ABI implementation.
- `northstarcpp_player_provider_inspection`: builtin, valid author override
  and incompatible explicit override all produce correct Player reports.
- `northstarcpp_qt_editor_authoring`: the dock has a working ABI provider
  list, alongside existing scene editing regression checks.

Initial verification: Release `all` target passed; full CTest
**111/111 PASS**, zero failures, on
`out/first-class-api-qa-20261114` before the final editor watchdog.
The final editor rebuild and follow-up tests are performed separately.

## Remaining work

- Convert more original systems into versioned first-class services:
  skeletal animation, GPU model/skin streaming and specialized UI controls.
- Add live provider diagnostics and controlled reinitialization for
  systems where hot-swap is safe. The present ABI contract guarantees
  replacement at module creation, not arbitrary mid-frame hot-swapping.
- Linux `.so` clean-machine acceptance, GPU/device-removal stress and
  real-game performance profiling.
- Produce a fresh signed release candidate and pass the production
  distribution gates; this task does **not** replace production `dist`.
