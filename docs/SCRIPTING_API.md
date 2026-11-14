# engine.scripting — provider-neutral JavaScript execution

Copyright (C) 2026 Kayla's Systems — Kayla Verner.

## Scope

`engine.scripting` is a first-class NorthStarCpp capability using the same
`NorthStarModule` ABI and directory-based DLL discovery as `engine.perlin`
and `engine.logger`. The public interfaces are:

- C ABI: `<northstarcpp/scripting_plugin.h>`, ABI version 2 (native host-call bridge).
- C++ SDK: `<northstarcpp/scripting.hpp>`, `northstarcpp::ScriptRuntime`.
- Native provider source: `engine/northstarcpp/modules/scripting/v8/v8.cpp`.
- Environment override: `NORTHSTARCPP_SCRIPTING_PLUGIN`.

The host/runtime library does not link to V8. When a suitable V8 provider DLL
is present, the loader discovers `engine.scripting` dynamically and validates
the capability string, host ABI, provider ABI and API-table size.

## Native V8 SDK and build

The main engine remains MinGW-w64 MINGW64/MSVCRT, C++17. For MSYS2's
UCRT64 V8 SDK, use the **separate UCRT64 GCC / C++20 provider build** in
`tools/v8_provider/CMakeLists.txt`. This avoids mixing V8's C++ ABI with
the engine's MSVCRT runtime. The native `scripting_v8.dll` exports only
`NorthStarModule` and the versioned `NorthStarScriptingApi` C ABI.

See **[V8 Provider — UCRT64 Build](V8_PROVIDER_UCRT64.md)** for exact
configuration commands, V8 dependencies, testing, DLL lifetime, deployment
and security constraints.

The previous root-CMake `NORTHSTARCPP_V8_ROOT` monolith path remains for
toolchain-compatible monolithic V8 SDKs. It does not accept MSYS2 UCRT64's
dynamic V8 import libraries; use the separate build for those.

## C++ integration

```cpp
#include <northstarcpp/scripting.hpp>
#include <iostream>

northstarcpp::ScriptingOptions options;
options.heap_limit_mb = 64;
options.timeout_ms = 1000;
northstarcpp::ScriptRuntime runtime(options);

std::cout << runtime.provider_name() << "\n";  // V8
std::cout << runtime.evaluate("let x = 21; x * 2") << "\n"; // 42
std::cout << runtime.evaluate("x += 1") << "\n"; // 22
std::cout << runtime.evaluate_file("scripts/intro.js") << "\n";
// Register a specific, allowlisted native API method.
runtime.bind("engine.demo", "sum", [](std::string_view arguments) {
    if (arguments == "[4,5]") return std::string("9");
    throw std::invalid_argument("Expected [4,5]");
});
std::cout << runtime.evaluate("engine.demo.sum(4, 5)") << "\\n"; // 9
runtime.evaluate("engine.on('update', dt => { globalThis.lastDt = dt; })");
runtime.emit_event("update", "0.016");
runtime.unbind("engine.demo", "sum");
runtime.reset(); // Fresh JavaScript global context
```

Registered methods supply the builtin behavior. An installed provider may replace individual methods with the C ABI in script_service_plugin.h. See [script services](MODULES.md#script-services).

Each runtime owns one V8 isolate and one persistent global context. Variables
persist across `evaluate` calls until `reset`. Separate runtime instances have
isolated JavaScript globals. The C++ result is a JSON-serialized completion
value. String results are JSON-quoted, `undefined` becomes `null`, and
non-serializable results cause a runtime error. Do not evaluate the same
`ScriptRuntime` concurrently. The V8 provider serializes its own calls.

`evaluate_file` reads a file at an explicitly supplied native host path. It
does not grant JavaScript file I/O. The caller is responsible for selecting
trusted file paths. For project-mounted resources, use
`runtime.evaluate_asset(assets, "scripts/intro.js")` to read source through
the existing `engine.assets` VFS. The API accepts UTF-8 JavaScript source
without granting file I/O to scripts. The V8 provider currently runs
classic scripts, not ECMAScript modules; `.mjs` files with `import` or
`export` statements require a future V8 module loader.

## Limits and error behavior

- Source limit: at most 1 MiB per call (configurable downward).
- Deadline: 1–60000 ms per evaluation; V8 `TerminateExecution` cancels
  long-running synchronous code from a watchdog thread.
- V8 heap configuration: 16–512 MiB per context; this is a **soft V8 heap
  setting, not a process memory cap**. Native allocation / OOM handling
  requires separate process isolation for untrusted scripts.
- Script exceptions, JSON serialization failures and timeouts are exposed
  through provider `last_error()` and C++ `std::runtime_error`.
- The result JSON string is borrowed from the provider; the C++ wrapper
  immediately makes an owning copy.
- Provider exceptions never cross the C ABI. After first initialization the V8 provider DLL is pinned for the process
  lifetime because its platform owns background worker threads.

**Trust model:** Embedded V8 is *not a secure sandbox* for hostile scripts.
The initial binding surface exposes ordinary JavaScript, the `engine` global
and explicit C++ callback registrations via `bind(capability, method, handler)`.
For example, an explicit `bind("engine.ecs", "spawn", ...)` would enable
`engine.ecs.spawn(...)`, but no engine APIs are implicitly granted. There is
no Node.js module loader, filesystem, or network API. The `engine.on/off/emit`
event bus is local to each script context. Before running third-party code,
the host must install a deliberate permissions layer for allowed bindings. Watchdog timeouts do not guarantee survival from V8 native OOM.

## Script game state persistence

The Player exposes engine.save.write(slot, JSON.stringify(state)), engine.save.read(slot) (JSON string or null), engine.save.exists(slot), and engine.save.remove(slot). Slots are restricted to project-private per-user storage, not arbitrary native paths. This persists project-owned JSON state, not automatic ECS/physics snapshots. See [Game saving](SAVE_GAME.md).

## Verification and next steps

When V8 is unavailable, `northstarcpp_scripting_abi` tests a test-only mock
provider and `northstarcpp_scripting_reject_incompatible` checks a mismatched
plugin. These tests do **not** claim successful JavaScript execution.

With V8 enabled, `northstarcpp_scripting_v8` additionally tests actual JS
execution, globals, JSON output, host API binding/revocation, event dispatch,
exception diagnostics, reset, UTF-8, and a terminating infinite loop.

Both `.js` and `.mjs` are classified as script source assets by the
Content Browser, accepted by `shared_assets::load_source`, and included in
CMake runtime packaging. To execute a script already mounted through the
engine VFS, call `runtime.evaluate_asset(assets, "scripts/intro.js")`, which validates
and reads UTF-8 script source through the existing asset VFS, without giving
JavaScript direct native file-system access.

The native player now discovers `scripts/main.js` next to project scenes,
registers explicit ECS, physics, scene, logger, input and Perlin bridges, and
emits `start/update/stop` and per-entity lifecycle events. Scene V3 script
components reference project-relative `scripts/*.js` resources. Project
scripts can hot-reload without tearing down the previous context on a failed
reload. Scripted game export checks for a V8 provider and copies scripts.

Archive script lookup is tested with both engine.assets/V8 smoke and the
actual rebuilt `northstarcpp_player.exe`; the same 40-object gameplay QA
project passed start/update/stop in both source and archive modes.
Public redistribution is not verified. The available MSYS2 V8 package is
flagged for a security issue, so release registration rejects versions older
than 15.3.48. See
[V8 Provider — UCRT64 Build](V8_PROVIDER_UCRT64.md).
