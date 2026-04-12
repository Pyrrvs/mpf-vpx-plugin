# mpf-vpx-plugin Design Spec

Cross-platform VPX plugin that bridges Visual Pinball X to the Mission Pinball Framework (MPF) via BCP, enabling MPF-driven game logic on macOS, Linux, and Windows.

## Context

The existing [mpf-vpcom-bridge](https://github.com/missionpinball/mpf-vpcom-bridge) is a Python-based Windows COM server. VPX table scripts call `CreateObject("MPF.Controller")` which resolves via Windows COM registration to the Python bridge, which forwards every call to MPF over BCP (TCP).

On non-Windows platforms, VPX runs natively (macOS/Linux standalone builds) with a vendored Wine VBScript engine. There is no Windows COM runtime. Instead, VPX provides a plugin SDK with `ScriptablePluginAPI::SetCOMObjectOverride()` that intercepts `CreateObject()` calls in VBScript and routes them to plugin-supplied objects. This is the same mechanism the PinMAME plugin uses (`SetCOMObjectOverride("VPinMAME.Controller", ...)`).

This plugin uses that mechanism to provide `CreateObject("MPF.Controller")` on all platforms.

## Architecture

### Components

Four source files with clean boundaries:

**BCPClient** (`src/BCPClient.h`, `src/BCPClient.cpp`)
Synchronous TCP client that speaks the BCP wire protocol. Owns the socket, handles connect/disconnect, send/receive.

- Wire format: `command?key=value&key=value\n` (newline-delimited, URL-encoded values).
- `SendAndWait(command, params, waitForCommand)` — sends a command, blocks reading lines until a response starting with `waitForCommand` arrives. Returns parsed key-value pairs.
- `Send(command, params)` — fire-and-forget for disconnect scenarios.
- Raw platform sockets: `<sys/socket.h>` on POSIX, Winsock (`ws2_32`) on Windows. A small internal `Socket` abstraction hides the `#ifdef` surface (`Connect`, `SendLine`, `ReadLine`, `Close`).
- Configurable timeout (default 5s) on `SendAndWait` to avoid infinite hangs if MPF crashes.
- No threading, no async. The VBScript call thread owns the socket for the duration of each call.

**MPFController** (`src/MPFController.h`, `src/MPFController.cpp`)
The scriptable Controller object that VBScript interacts with. Holds a `BCPClient*` and a `Recorder*`.

Registered via `PSC_CLASS_START` / `PSC_CLASS_END` macros. Each VBScript-facing method is a thin translation: marshal args into BCP params, call `BCPClient::SendAndWait()`, optionally record the event, unmarshal and return.

Method surface (mirrors the Python bridge):

| Type | Name | BCP subcommand | Recording category |
|------|------|----------------|--------------------|
| Method | `Run()` | `start` (after TCP connect) | — |
| Method | `Run(addr)` | `start` | — |
| Method | `Run(addr, port)` | `start` | — |
| Method | `Stop()` | `stop` (then disconnect) | — |
| Prop R | `Switch(number)` | `switch` | `query` |
| Prop R | `GetSwitch(number)` | `get_switch` | `query` |
| Prop W | `SetSwitch(number, value)` | `set_switch` | `input` |
| Method | `PulseSW(number)` | `pulsesw` | `input` |
| Prop R | `Mech(number)` | `mech` | `query` |
| Prop W | `SetMech(number, value)` | `set_mech` | `input` |
| Prop R | `GetMech(number)` | `get_mech` | `query` |
| Prop R | `ChangedSolenoids` | `changed_solenoids` | `state` |
| Prop R | `ChangedLamps` | `changed_lamps` | `state` |
| Prop R | `ChangedGIStrings` | `changed_gi_strings` | `state` |
| Prop R | `ChangedLEDs` | `changed_leds` | `state` |
| Prop R | `ChangedBrightnessLEDs` | `changed_brightness_leds` | `state` |
| Prop R | `ChangedFlashers` | `changed_flashers` | `state` |
| Prop R | `HardwareRules` | `get_hardwarerules` | `state` |
| Method | `IsCoilActive(number)` | `get_coilactive` | `state` |
| Stubs | `GameName`, `Version`, `Games`, `ShowTitle`, `ShowFrame`, `ShowDMDOnly`, `HandleMechanics`, `HandleKeyboard`, `DIP`, `Pause`, `SplashInfoLine` | — (local state) | — |

Run overloads use `PSC_FUNCTION0`/`PSC_FUNCTION1`/`PSC_FUNCTION2` following the PinMAME pattern. Default address is `localhost`, default port is `5051`.

Reference counting via `PSC_IMPLEMENT_REFCOUNT()`. Destructor disconnects BCP and flushes recorder if still open.

**Recorder** (`src/Recorder.h`, `src/Recorder.cpp`)
Non-blocking event recorder with background writer thread.

Event structure:
```cpp
struct RecordEvent {
    double timestamp;          // std::chrono::steady_clock relative to session start
    const char* category;      // "input", "state", "query"
    const char* direction;     // "vpx_to_mpf", "mpf_to_vpx"
    std::string command;       // BCP subcommand name
    std::string params;        // JSON-encoded params (pre-serialized by caller)
    std::string result;        // JSON-encoded result (pre-serialized by caller)
};
```

Categories:
- `input` — state-changing calls from VPX to MPF: `SetSwitch`, `PulseSW`, `SetMech`
- `state` — polled responses from MPF: `ChangedSolenoids`, `ChangedLamps`, `ChangedGIStrings`, `ChangedLEDs`, `ChangedBrightnessLEDs`, `ChangedFlashers`, `HardwareRules`, `IsCoilActive`
- `query` — read-only queries: `Switch`, `GetSwitch`, `Mech`, `GetMech`

Output format — JSONL, one object per line:
```jsonl
{"ts": 0.000, "wall": "2026-04-12T14:30:00.123456Z", "cat": "input", "dir": "vpx_to_mpf", "cmd": "set_switch", "params": {"number": "2", "value": "true"}}
{"ts": 0.002, "cat": "state", "dir": "mpf_to_vpx", "cmd": "changed_solenoids", "result": [["5", true]]}
```

The first event includes a `wall` field (wall-clock anchor from `std::chrono::system_clock`). All `ts` values are relative to session start via `steady_clock` for monotonic precision.

File naming: `<recording_path>/YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl`. One file per game session (`Run` to `Stop`).

Internals:
- Lock-free SPSC (single-producer single-consumer) ring buffer. The VBScript thread pushes events, the writer thread drains them. No mutex on the hot path.
- Writer thread wakes via condition variable on push, with a 100ms periodic fallback to ensure flush even if signal is missed.
- JSON serialization is hand-rolled via `snprintf` — the event structure is flat enough that a JSON library is unnecessary.
- On `StopSession()`, signals the writer thread to drain remaining events and close the file.
- If VPX crashes without calling Stop, periodic flush ensures most events are written.

**MPFPlugin** (`src/MPFPlugin.cpp`)
Plugin entry point. Thin glue.

`MPFPluginLoad(sessionId, api)`:
1. Stash `MsgPluginAPI*`
2. Fetch `ScriptablePluginAPI*` via `BroadcastMsg(SCRIPTPI_MSG_GET_API)`
3. Fetch `LoggingPluginAPI*` for shared logging (optional, graceful fallback)
4. Register plugin settings (recording enable, recording path)
5. Register `MPF_Controller` script class via `PSC_CLASS_START` macros
6. `scriptApi->SetCOMObjectOverride("MPF.Controller", MPF_Controller_SCD)`
7. Assign `CreateObject` factory lambda — instantiates `MPFController` configured from plugin settings

`MPFPluginUnload()`:
1. `scriptApi->SetCOMObjectOverride("MPF.Controller", nullptr)`
2. Unregister script class
3. Release message IDs
4. Null out API pointers

Plugin settings (registered via `MsgPluginAPI::RegisterSetting`):
- `Enable Recording` — bool, default `false`
- `Recording Path` — string, default empty (falls back to `recordings/` next to the plugin directory)

### Object lifecycle

```
VBScript: Set Controller = CreateObject("MPF.Controller")
  → VPX rewrites to CreatePluginObject("MPF.Controller")
  → factory lambda: new MPFController(recorder_settings)

VBScript: Controller.Run "localhost", 5051
  → MPFController::Run()
  → BCPClient::Connect("localhost", 5051)
  → BCPClient::SendAndWait("vpcom_bridge", {subcommand: "start"}, "vpcom_bridge_response")
  → Recorder::StartSession() (if recording enabled)

[game plays — VBScript calls methods, BCP flows, events recorded]

VBScript: Controller.Stop
  → Recorder::StopSession()
  → BCPClient::Send("vpcom_bridge", {subcommand: "stop"})
  → BCPClient::Disconnect()

VBScript releases Controller
  → refcount hits 0
  → destructor: disconnect if still connected, flush recorder if still open
```

## Repository structure

```
mpf-vpx-plugin/
  CMakeLists.txt
  plugin.cfg
  LICENSE                     # GPLv3 (already present)
  README.md
  .gitignore
  .github/
    workflows/
      build.yml               # CI: build + test matrix
      release.yml              # CD: build release artifacts on tag push
  src/
    MPFPlugin.cpp
    MPFController.h
    MPFController.cpp
    BCPClient.h
    BCPClient.cpp
    Recorder.h
    Recorder.cpp
  docs/
    superpowers/specs/
```

## Build system

CMake with FetchContent pulling VPX SDK headers from a configurable git tag.

```cmake
set(VPX_TAG "v10.8.0-2051-28dd6c3" CACHE STRING "VPX git tag to fetch SDK headers from")

FetchContent_Declare(vpx-sdk
    GIT_REPOSITORY https://github.com/vpinball/vpinball.git
    GIT_TAG ${VPX_TAG}
    GIT_SHALLOW TRUE
)
```

Only the `plugins/plugins/` headers are referenced via `target_include_directories`. The full VPX source is fetched but nothing else is compiled from it.

C++ standard: C++20 (required for `std::format`, `std::chrono` improvements, and matching VPX's own standard).

Platform-specific:
- macOS: output suffix `.dylib`
- Linux: default `.so`
- Windows: link `ws2_32` for Winsock, output `plugin-mpf64.dll` (x64) / `plugin-mpf.dll` (x86)

Install target copies `plugin.cfg` + binary into a `dist/mpf/` folder ready to drop into VPX's plugin directory.

### plugin.cfg

```ini
[configuration]
id = "MPF"
name = "MPF Bridge"
description = "Mission Pinball Framework controller bridge over BCP"
author = "Pyrrvs"
version = "0.1.0"
link = "https://github.com/Pyrrvs/mpf-vpx-plugin"

[libraries]
windows.x86 = "plugin-mpf.dll"
windows.x64 = "plugin-mpf64.dll"
linux.x64 = "plugin-mpf.so"
linux.aarch64 = "plugin-mpf.so"
macos.x64 = "plugin-mpf.dylib"
macos.arm64 = "plugin-mpf.dylib"
```

## CI/CD

### build.yml (on push + PR)

Matrix:

| Axis | Values |
|------|--------|
| os | `macos-latest`, `ubuntu-latest`, `windows-latest` |
| VPX_TAG | `v10.8.0-2051-28dd6c3`, `main` |

Steps: checkout, CMake configure with `-DVPX_TAG=...`, build, upload artifact.

Building against `main` gives early warning when the SDK headers change. Building against the stable tag ensures compatibility with what users run.

### release.yml (on tag push)

Triggered by tags matching `v*`. Builds release artifacts for all platforms against the stable VPX tag, creates a GitHub release, attaches the built plugin bundles (each a zip containing the `mpf/` folder with `plugin.cfg` + platform binary, ready to extract into VPX's `plugins/` directory).

## .gitignore

Standard C++/CMake ignores: `build/`, `dist/`, `CMakeCache.txt`, `CMakeFiles/`, `*.dylib`, `*.so`, `*.dll`, `.DS_Store`, IDE folders.

## README.md

Written after implementation is complete. Covers: what this is, how to build, how to install (drop plugin folder into VPX plugins directory), how to configure MPF (`hardware: platform: virtual_pinball`), plugin settings (recording toggle, recording path), compatibility with VPX versions.

## Out of scope (future work)

- **Python test generator** — reads JSONL recordings, generates MPF test cases from `input`+`state` event pairs.
- **Async/cached BCP client** — background thread maintains socket, caches state, VBScript calls return from cache. Optimization if synchronous round-trips cause rendering stutter.
- **ControllerPlugin API integration** — publish inputs/devices to other VPX plugins (DOF, DMDUtil, etc.) via the `CTLPI_*` message protocol.
