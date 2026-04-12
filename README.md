# mpf-vpx-plugin

Cross-platform VPX plugin that bridges Visual Pinball X to the Mission Pinball Framework (MPF) via BCP, enabling MPF-driven game logic on macOS, Linux, and Windows.

This plugin replaces the Windows-only [mpf-vpcom-bridge](https://github.com/missionpinball/mpf-vpcom-bridge) by using VPX's native plugin SDK. Existing table scripts using `CreateObject("MPF.Controller")` work unchanged.

## Installation

1. Download the latest release for your platform from the [Releases](https://github.com/Pyrrvs/mpf-vpx-plugin/releases) page.
2. Extract the `mpf/` folder into your VPX plugins directory:
   - **macOS:** `VPinballX_BGFX.app/Contents/Resources/plugins/`
   - **Linux:** `<vpx-install>/plugins/`
   - **Windows:** `<vpx-install>/plugins/`
3. Enable the plugin in VPX settings.

## MPF Configuration

In your MPF machine config:

```yaml
hardware:
    platform: virtual_pinball
```

## Usage

1. Start MPF (`mpf both`), wait until the display has been initialized.
2. Start VPX and load your table.
3. The table script's `CreateObject("MPF.Controller")` will be handled by this plugin.
4. `Controller.Run` connects to MPF's BCP server (default: `localhost:5051`).

To specify a custom address/port in your table script:

```vbscript
Controller.Run "192.168.1.100", 5051
```

## Recording

The plugin can record BCP events during game sessions for debugging and test generation.

Enable recording in VPX's plugin settings:
- **Enable Recording:** toggle on
- **Recording Path:** directory for output files (default: `recordings/` next to the plugin)

Recordings are saved as JSONL files (one JSON object per line), named `YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl`.

Each event has a category for easy filtering:
- `input` — state changes from VPX to MPF (SetSwitch, PulseSW, SetMech)
- `state` — polled state from MPF (ChangedSolenoids, ChangedLamps, etc.)
- `query` — read-only queries (Switch, GetSwitch, GetMech)

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
