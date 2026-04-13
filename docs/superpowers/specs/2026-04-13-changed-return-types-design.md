# Changed* Return Types Fix — Design Spec

Fix the `Changed*` methods (ChangedLamps, ChangedSolenoids, ChangedGIStrings, ChangedLEDs, ChangedBrightnessLEDs, ChangedFlashers, HardwareRules) to return data that VBScript tables can consume as 2D arrays.

## Problem

VPX table VBScript expects `Controller.ChangedLamps` etc. to return a 2D SAFEARRAY indexable as `arr(i, 0)` (string ID) and `arr(i, 1)` (bool state). The current plugin returns raw strings, causing `UBound()` to fail with `VBSE_ACTION_NOT_SUPPORTED`.

VPX's plugin ABI (`DynamicScript.cpp`) only supports numeric element types in script arrays — string arrays hit `assert(false)` at runtime. A direct 2D string+bool SAFEARRAY cannot be returned through the PSC layer.

## Solution

Return a scriptable `ChangedItems` object from C++, and provide a VBScript helper layer (`mpf_controller.vbs`) that converts the object into a real 2D variant array. Tables change one word per `Changed*` call: `Controller.ChangedLamps` → `MPF_ChangedLamps(Controller)`.

## Architecture

### ChangedItems (C++)

A single reusable scriptable class returned by all `Changed*` methods.

```cpp
class ChangedItems {
    PSC_IMPLEMENT_REFCOUNT()
public:
    ChangedItems(std::vector<std::pair<std::string, bool>> items);
    ChangedItems(std::vector<std::pair<std::string, bool>> items,
                 std::vector<float> brightness);

    int GetCount() const;
    std::string GetId(int index) const;
    bool GetState(int index) const;
    float GetBrightness(int index) const;  // 0.0 when not applicable

private:
    std::vector<std::pair<std::string, bool>> m_items;
    std::vector<float> m_brightness;  // populated only by ChangedBrightnessLEDs
};
```

Registered as `MPF_ChangedItems` scriptable class with:
- `PSC_PROP_R(int, Count)`
- `PSC_PROP_R_ARRAY1(string, Id, int)`
- `PSC_PROP_R_ARRAY1(bool, State, int)`
- `PSC_PROP_R_ARRAY1(float, Brightness, int)`

When there are no changes, `GetChanged*` methods return `nullptr`, which the PSC layer converts to VBScript `Nothing`.

### HardwareRuleItems (C++)

`HardwareRules` returns 3-tuples `(switch_number, coil_number, hold_bool)` from MPF — a different shape than the 2-column `Changed*` methods. A separate class handles this:

```cpp
class HardwareRuleItems {
    PSC_IMPLEMENT_REFCOUNT()
public:
    HardwareRuleItems(std::vector<std::tuple<std::string, std::string, bool>> rules);
    int GetCount() const;
    std::string GetSwitch(int index) const;   // column 0
    std::string GetCoil(int index) const;     // column 1
    bool GetHold(int index) const;            // column 2
private:
    std::vector<std::tuple<std::string, std::string, bool>> m_rules;
};
```

Registered as `MPF_HardwareRuleItems` with:
- `PSC_PROP_R(int, Count)`
- `PSC_PROP_R_ARRAY1(string, Switch, int)`
- `PSC_PROP_R_ARRAY1(string, Coil, int)`
- `PSC_PROP_R_ARRAY1(bool, Hold, int)`

The VBScript helper `MPF_HardwareRules` builds a 3-column 2D array:
```vbs
ReDim arr(n - 1, 2)
arr(i, 0) = r.Switch(i)
arr(i, 1) = r.Coil(i)
arr(i, 2) = r.Hold(i)
```

### JSON parsing

**New dependency:** `nlohmann/json` single header vendored at `src/json.hpp`.

**BCPClient::DecodeLine change:** After URL-decoding query params, if a `json` key exists, parse its JSON value and flatten the inner keys into the `params` map as raw JSON strings. Example:

Wire: `vpcom_bridge_response?json={"result": [["l-1", true]]}`
Decoded: `params["result"] = "[["l-1", true]]"` (raw JSON string of the value)

This keeps BCPClient as a thin transport layer.

**MPFController::ParseChangedList:** New method that parses the JSON array string into a vector of pairs.

```cpp
std::vector<std::pair<std::string, bool>> ParseChangedList(const std::string& jsonStr);
```

Handles:
- String IDs: `"l-1"` → `"l-1"`
- Numeric IDs: `42` → `"42"` (coerced to string)
- Bool state: `true`/`false`
- Int state: `1`/`0` → `true`/`false`
- Empty array `[]` → empty vector
- `"false"` string (MPF's "no changes" sentinel) → empty vector

**MPFController::ParseChangedBrightnessList:** Variant that returns `(string id, bool state, float brightness)` triples. `state = brightness > 0.5`.

**MPFController::ParseHardwareRulesList:** Parses `[["sw1", "coil1", true], ...]` into `std::vector<std::tuple<std::string, std::string, bool>>`.

### MPFController method changes

All `Changed*` methods change return type from `std::string` to `ChangedItems*`:

```cpp
ChangedItems* GetChangedSolenoids();
ChangedItems* GetChangedLamps();
ChangedItems* GetChangedGIStrings();
ChangedItems* GetChangedLEDs();
ChangedItems* GetChangedBrightnessLEDs();
ChangedItems* GetChangedFlashers();
HardwareRuleItems* GetHardwareRules();
```

Each method:
1. Calls `DispatchToMPF` (unchanged — still records the event)
2. Parses the raw JSON result string via `ParseChangedList`
3. Returns `new ChangedItems(parsed)` if non-empty, `nullptr` if empty

### PSC registration changes (MPFPlugin.cpp)

Register `MPF_ChangedItems` and `MPF_HardwareRuleItems` classes before `MPF_Controller`. Update `Changed*` property return types:

```cpp
// Before:
PSC_PROP_R(MPF_Controller, string, ChangedLamps)

// After:
PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedLamps)
```

### VBScript helper layer

File: `scripts/mpf_controller.vbs`

Tables include it:
```vbs
ExecuteGlobal GetTextFile("mpf_controller.vbs")
```

Contains wrapper functions for each `Changed*` method:

```vbs
Function MPF_ChangedLamps(Controller)
    Dim r : Set r = Controller.ChangedLamps
    If r Is Nothing Then
        MPF_ChangedLamps = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedLamps = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedLamps = arr
End Function
```

Same pattern for: `MPF_ChangedSolenoids`, `MPF_ChangedGIStrings`, `MPF_ChangedLEDs`, `MPF_ChangedFlashers`, `MPF_HardwareRules`.

`MPF_ChangedBrightnessLEDs` uses `.Brightness(i)` for column 1 instead of `.State(i)`.

**Table modification required:** one word per `Changed*` call:

```vbs
' Before:
ChgLamp = Controller.ChangedLamps

' After:
ChgLamp = MPF_ChangedLamps(Controller)
```

All downstream code (`UBound`, `(ii, 0)`, `(ii, 1)`, `UpdateLamps`) works unchanged because the helper returns a real VBScript 2D variant array via `ReDim`.

**Installation:** Copy `mpf_controller.vbs` into VPX's `scripts/` directory (`VPinballX_BGFX.app/Contents/Resources/scripts/`) or next to the `.vpx` table file.

## File changes

| File | Action | What |
|------|--------|------|
| `src/json.hpp` | Create | Vendored nlohmann/json v3.11.3 single header |
| `src/ChangedItems.h` | Create | ChangedItems and HardwareRuleItems classes |
| `src/ChangedItems.cpp` | Create | Implementation of both classes |
| `src/BCPClient.cpp` | Modify | DecodeLine handles `json=` wrapper |
| `src/MPFController.h` | Modify | Changed* return ChangedItems*, add ParseChangedList |
| `src/MPFController.cpp` | Modify | Changed* methods parse JSON, build ChangedItems |
| `src/MPFPlugin.cpp` | Modify | Register MPF_ChangedItems class, update return types |
| `scripts/mpf_controller.vbs` | Create | VBScript helper wrappers |
| `tests/test_ChangedItems.cpp` | Create | Tests for ChangedItems + ParseChangedList |
| `tests/test_BCPClient.cpp` | Modify | Test json= wrapper decoding |
| `tests/test_MPFController.cpp` | Modify | Mock returns JSON, verify ChangedItems* |
| `CMakeLists.txt` | Modify | Add ChangedItems.cpp and test file |

## What doesn't change

Recorder.h/cpp, BCPClient.h, MockBCPServer.h, .github/workflows/, plugin.cfg.

## Testing

- **ChangedItems unit tests:** construct with known data, verify Count/Id/State/Brightness accessors, verify out-of-bounds returns defaults
- **ParseChangedList tests:** valid JSON arrays, mixed string/int IDs, bool/int states, empty array, `"false"` sentinel, malformed JSON returns empty
- **BCPClient DecodeLine tests:** `json=` wrapped response parses correctly, nested JSON values flattened
- **MPFController integration tests:** mock server returns JSON-wrapped responses, verify Changed* returns ChangedItems with correct data
- **VBScript integration:** manual smoke test with demo table + helper script in VPX

## Out of scope

- Upstream VPX patch for string arrays (separate effort, tracked in `docs/superpowers/notes/`)
- Porting to master ABI (deferred, tracked in `docs/superpowers/notes/`)
- README update for helper installation (follow-up commit after implementation)
