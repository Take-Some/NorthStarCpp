# NorthStarCpp — `engine.logger` (ABI 1)

**Provider:** `logger_multisink.dll` · **Version:** `1.0.0` · **Language:** C++17 / C-compatible plugin ABI

`engine.logger` is the replaceable, native logging capability used by NorthStarCpp applications and games. It is discovered using the existing `northstar_module_query` loader. No Python runtime, network connection, external logging service, or legacy engine component is needed in shipped games.

## Files and contracts

| Component | Path |
|---|---|
| C API / ABI | `sdk/include/northstarcpp/logger_plugin.h` |
| C++ SDK wrapper | `sdk/include/northstarcpp/logger.hpp` |
| Host bridge | `engine/northstarcpp/modules/host/logger.cpp` |
| DLL provider | `engine/northstarcpp/modules/logger/multisink/multisink.cpp` |
| SDK unit tests | `tests/logger_smoke.cpp` |
| App lifecycle / fallback tests | `tests/logger_application_smoke.py` |

## Outputs

The standard provider writes to **up to four sinks at once**:

| Sink | Format | Purpose |
|---|---|---|
| `NORTHSTAR_LOG_SINK_TEXT` | `<session>-<pid>.log` (UTF-8) | Human-readable, one timestamped line per physical line; no ANSI coloring |
| `NORTHSTAR_LOG_SINK_STRUCTURED` | `<session>-<pid>.ulog` (UTF-8 JSON Lines) | Machine-readable, one JSON object per event |
| `NORTHSTAR_LOG_SINK_CONSOLE` | Standard error | Windows console / redirected pipe |
| `NORTHSTAR_LOG_SINK_DEBUGGER` | `OutputDebugStringW` | IDE / debugging output |

Every event records a UTC ISO-8601 timestamp (millisecond resolution), severity, channel, PID, TID and message.

Example text:

```text
2026-11-13T20:52:28.954Z [INFO] [engine] [pid:652 tid:572] NorthStarCpp game player starting
2026-11-13T20:52:29.676Z [INFO] [engine] [pid:652 tid:572] Application exited with code 0
```

Example structured record:

```json
{"timestamp_utc":"2026-11-13T20:52:28.954Z","level":"INFO","channel":"engine","message":"NorthStarCpp game player starting","pid":652,"tid":572}
```

Multi-line messages remain readable in `.log` and are JSON-escaped in `.ulog`. File logging is synchronized by a mutex. ERROR/FATAL events flush synchronously; explicit `flush()` and normal context destruction flush the remaining buffers. Each file rotates independently by byte limit with configurable numbered backups (`.1` to `.16`).

The built-in default output location is:

```text
%LOCALAPPDATA%\KaylasSystems\NorthStarCpp\Logs\
```

If `LOCALAPPDATA` is not available, the provider uses the Windows temporary directory as its base. The default session filename includes the process ID, preventing unrelated simultaneously running applications from writing to the same file.

## Colored terminal output

The console sink uses a semantic True Color palette:

| Component | Color |
|---|---|
| UTC timestamp | Cyan / teal |
| Square brackets | Muted slate gray |
| Severity | TRACE gray, DEBUG blue, INFO green, WARN amber, ERROR red, FATAL highlighted white-on-red |
| Channel | Violet |
| Process/thread metadata | Muted steel blue |
| Message | Light neutral text |

An ANSI SGR True Color formatter is used when the Windows terminal supports
ENABLE_VIRTUAL_TERMINAL_PROCESSING. Older Win32 console hosts fall back to
SetConsoleTextAttribute. Every physical line gets its own colored prefix and
the terminal attributes are reset after each segment. Terminal escape/control
characters inside the message are sanitized for console/text output.

Colors never enter the UTF-8 .log or JSON Lines .ulog files, and the
debugger sink remains plain text. Redirected stderr is plain by default.

- FORCE_COLOR=1 or CLICOLOR_FORCE=1: force ANSI even for redirected stderr
- NO_COLOR=1 or CLICOLOR=0: disable console colors, overriding force

The public engine.logger ABI 1 and portable C++ SDK remain unchanged.

## C++ SDK

```cpp
#include <northstarcpp/logger.hpp>

northstarcpp::LoggerOptions settings;
settings.session = "MyGame";
settings.minimum_level = northstarcpp::LogLevel::debug;
settings.rotate_bytes = 16ull * 1024 * 1024;
settings.backup_count = 3;

northstarcpp::Logger log(settings);
log.info("startup", "Starting game");
log.debug("assets", "Loaded texture streaming cells");
log.warn("physics", "Potentially unstable dynamic body");
log.error("assets", "Material failed to load");
log.flush();
```

Link against `NorthStarCpp::northstarcpp_runtime` and ensure the compatible logger DLL is present in the application's configured plugin directory. The C API `NorthStarLoggerApi` contains `create`, `destroy`, `write`, `flush`, `set_minimum_level`. Messages are UTF-8 with explicit lengths; pointers may be reused as soon as `write()` returns. Levels are TRACE, DEBUG, INFO, WARN, ERROR and FATAL. Payload size is limited to 64 KiB; channel length to 128 bytes.

## bootstrap.json: application logging configuration

NorthStarCpp reads `bootstrap.json` **next to the executable**, never from
the working directory. Release packages include `bin/bootstrap.json`. If
it is absent, the same defaults apply: the log console is **hidden**, and
text .log, structured .ulog and debugger output remain enabled.

```json
{
  "plugins": {"path": "./"},
  "logging": {
    "enabled": true,
    "level": "INFO",
    "directory": "",
    "sinks": {"text": true, "structured": true, "debugger": true},
    "console": {"visible": false},
    "rotation": {"maxBytes": 16777216, "backups": 3}
  }
}
```

### Supported settings

| Setting | Default | Purpose |
|---|---|---|
| `logging.enabled` | `true` | Enable/disable automatic application logging |
| `logging.level` | `INFO` | TRACE / DEBUG / INFO / WARN / ERROR / FATAL (case-insensitive) |
| `logging.directory` | `""` | Default Local AppData logs folder; relative paths are EXE-relative |
| `logging.console.visible` | `false` | Create the console only when requested |
| `logging.sinks.console` | Inherits visibility | Output events to console; false by default |
| `logging.sinks.text` | `true` | Write plain UTF-8 .log |
| `logging.sinks.structured` | `true` | Write JSON Lines .ulog |
| `logging.sinks.debugger` | `true` | OutputDebugString |
| `logging.rotation.maxBytes` | `16777216` | Threshold in bytes, 0 disables rotation (maximum 1 GiB) |
| `logging.rotation.backups` | `3` | Backups retained (0–16) |

Set `"logging": {"level": "DEBUG", "console": {"visible": true}}` to
enable a colored console. Its console sink follows visibility unless
`logging.sinks.console` explicitly overrides it. An attached parent
terminal is never hidden or detached. Journals remain ANSI-free.

Configuration is read at application startup. Invalid logging settings and
file-access failures are reported through the fallback output without aborting
normal application startup. The `NORTHSTARCPP_LOG_DIRECTORY` environment
override takes precedence over the JSON directory. Directly created
`Logger` objects retain their own `LoggerOptions`.

## Application integration

The game Player, scene Editor, Qt Project Manager and Qt Camera Timeline Editor initialize a logger in their entry points, write startup/normal-exit events, and report abnormal exit and unhandled exceptions as ERROR/FATAL. **Logger initialization errors never abort application startup.** All applications preserve a fallback output through standard error; Player/Editor also use `OutputDebugStringA`, and Qt Project Manager uses `qWarning`.

Programmatically triggered smoke tests do not normally write logs to the user's profile. Set the testing override below to force an isolated log location and enable logging during the smoke flow.

```powershell
$env:NORTHSTARCPP_LOG_DIRECTORY = 'C:\Temp\NorthStarLogs'
.\northstarcpp_player.exe --check-scene --scene 'C:\Games\MyGame\main.nscene'
```

The environment variable `NORTHSTARCPP_LOG_DIRECTORY` sets the output directory of engine-hosted processes. If it is invalid or inaccessible, applications report the initialization error through their fallback channel and continue. It does **not** change the behavior of independently created `Logger` instances whose caller provided explicit options.

To explicitly select a different logging provider:

```powershell
$env:NORTHSTARCPP_LOGGER_PLUGIN = 'C:\Plugins\custom_logger.dll'
```

The alternate DLL must export the correct capability name, module descriptor and ABI 1 function table. An incompatible or missing override raises an error for direct SDK consumers and fails open in application entry points.

## Build, tests and packaging

```powershell
cmake --build out/camera-native-release --target all --parallel 1
ctest --test-dir out/camera-native-release -R 'northstarcpp_logger_' --output-on-failure
python tools/build_dist.py --from-build out/camera-native-release --no-zip --jobs 1
```

`logger_multisink.dll` is registered by the same CMake module registry as other engine capabilities and installed into `dist/.../bin`. `engine.logger` is included in game-packaging capability validation so standalone games receive their logging provider.

**Validation scope:** automated checks cover provider ABI validation and swapping, concurrent messages, UTF-8 and JSON Lines, level filters, rotation, runtime startup/exit, FATAL on invalid startup arguments, inaccessible log directory and a missing provider. Unexpected process termination (hard crash) cannot be guaranteed to flush buffered records; BugTrap handles crash capture separately.
