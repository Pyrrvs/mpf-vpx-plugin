# mpf-vpx-plugin

Cross-platform VPX plugin that bridges Visual Pinball X to the Mission Pinball Framework (MPF) via BCP, enabling MPF-driven game logic on macOS, Linux, and Windows.

This plugin replaces the Windows-only [mpf-vpcom-bridge](https://github.com/missionpinball/mpf-vpcom-bridge) by using VPX's native plugin SDK. Existing table scripts using `CreateObject("MPF.Controller")` work unchanged.

## Installation

### 1. Install the plugin

Download the latest release for your platform from the [Releases](https://github.com/Pyrrvs/mpf-vpx-plugin/releases) page. Extract the `mpf/` folder into your VPX plugins directory:

| Platform | Plugins directory |
|----------|-------------------|
| **macOS** | `VPinballX_BGFX.app/Contents/Resources/plugins/` |
| **Linux** | `<vpx-install>/plugins/` |
| **Windows** | `<vpx-install>\plugins\` |

After installation you should have:

```
plugins/
  mpf/
    plugin.cfg
    plugin-mpf.dylib   (macOS)
    plugin-mpf.so       (Linux)
    plugin-mpf64.dll    (Windows 64-bit)
```

### 2. Enable the plugin

VPX comes in two flavors that affect how you configure plugins:

**Windows with editor** — open VPX, go to *Preferences > Plugins*, find "MPF Bridge" in the list, and enable it. Recording and recording path can also be configured from this screen.

**Standalone player** (macOS, Linux, Windows standalone) — the standalone player has no settings UI. Edit `VPinballX.ini` directly and add:

```ini
[Plugin.MPF]
Enable = 1
```

The ini file is located at:

| Platform | Path |
|----------|------|
| **macOS / Linux** | `~/.vpinball/VPinballX.ini` |
| **Windows** | `VPinballX.ini` in the VPX install directory |

#### macOS code signing

On macOS, VPX must be signed with library validation disabled for third-party plugins to load. If your VPX copy was re-signed locally, re-sign it with:

```bash
codesign --force --sign - --entitlements entitlements.plist \
    --preserve-metadata=identifier,requirements,flags,runtime \
    ~/Applications/VPinballX_BGFX.app
```

Where `entitlements.plist` contains:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
    "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

### 3. Install the VBScript helper

Copy `scripts/mpf_controller.vbs` into the same folder as your `.vpx` table file (or any directory VPX can load scripts from).

This helper converts the plugin's object-based `Changed*` responses into the 2D variant arrays that existing table scripts expect.

Add this line near the top of your table script (after any existing `ExecuteGlobal` lines):

```vbscript
ExecuteGlobal GetTextFile("mpf_controller.vbs")
```

Then replace the direct `Controller.Changed*` calls in your timer sub:

| Before | After |
|--------|-------|
| `Controller.ChangedLamps` | `MPF_ChangedLamps(Controller)` |
| `Controller.ChangedSolenoids` | `MPF_ChangedSolenoids(Controller)` |
| `Controller.ChangedGIStrings` | `MPF_ChangedGIStrings(Controller)` |
| `Controller.ChangedLEDs` | `MPF_ChangedLEDs(Controller)` |
| `Controller.ChangedFlashers` | `MPF_ChangedFlashers(Controller)` |
| `Controller.HardwareRules` | `MPF_HardwareRules(Controller)` |

All other `Controller` methods (`Switch`, `PulseSW`, `IsCoilActive`, `Run`, `Stop`, `Mech`) work unchanged.

## MPF configuration

In your MPF machine config:

```yaml
hardware:
    platform: virtual_pinball
```

## Usage

1. Start MPF: `mpf both` (the media controller satisfies the BCP client connection MPF waits for before starting its own BCP server).
2. Start VPX and load your table.
3. The table script's `CreateObject("MPF.Controller")` is handled by this plugin.
4. `Controller.Run` connects to MPF's BCP server (default: `localhost:5051`).

To specify a custom address/port in your table script:

```vbscript
Controller.Run "192.168.1.100", 5051
```

## Recording

The plugin can record all BCP traffic during a game session for debugging and test generation.

**Windows with editor** — enable recording and set the output path from *Preferences > Plugins > MPF Bridge*.

**Standalone player** — add these settings to `VPinballX.ini`:

```ini
[Plugin.MPF]
Enable = 1
EnableRecording = 1
RecordingPath = /path/to/recordings
```

Recordings are saved as JSONL files (one JSON object per line), named `YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl`.

Each event has a category for filtering:

| Category | Direction | Description |
|----------|-----------|-------------|
| `input` | vpx_to_mpf | Switch changes, pulse events, mech writes |
| `state` | both | Polled state: solenoids, lamps, LEDs, GI, flashers, hardware rules, coil active checks |
| `query` | both | Read-only queries: switch reads, mech reads |

Example events:

```json
{"ts":0.000,"wall":"2026-04-15T17:33:56.506674Z","cat":"input","dir":"vpx_to_mpf","cmd":"set_switch","params":{"number":"2","value":"bool:True"}}
{"ts":2.364,"cat":"state","dir":"mpf_to_vpx","cmd":"changed_solenoids","result":[["coil1",true]]}
```

## Building from source

Requirements: CMake 3.20+, C++20 compiler

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The built plugin is placed in `build/dist/mpf/`.

To build against a specific VPX version:

```bash
cmake -B build -DVPX_TAG=v10.8.1-3788-2151290
```

## License

GPLv3 — see [LICENSE](LICENSE).
