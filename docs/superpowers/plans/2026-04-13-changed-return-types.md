# Changed* Return Types Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the Changed* methods to return scriptable objects that VBScript can consume via a helper layer, instead of raw strings that crash on UBound().

**Architecture:** Changed* methods return a `ChangedItems` scriptable object (Count/Id/State/Brightness). A VBScript helper (`mpf_controller.vbs`) converts these objects into real 2D variant arrays. BCPClient gains JSON-wrapper decoding. MPFController gains JSON parsing via nlohmann/json.

**Tech Stack:** C++20, nlohmann/json (single-header, vendored), existing PSC macro API, VBScript helper

**Spec:** `docs/superpowers/specs/2026-04-13-changed-return-types-design.md`

---

### Task 1: Vendor nlohmann/json and add BCPClient JSON-wrapper decoding

**Files:**
- Create: `src/json.hpp`
- Modify: `src/BCPClient.cpp`
- Modify: `tests/test_BCPClient.cpp`

This task adds the JSON library and extends `BCPClient::DecodeLine` to handle the `json=<json>` wrapper format that MPF uses for list-valued responses. No changes to ChangedItems yet — just the transport layer.

- [ ] **Step 1: Download nlohmann/json single header**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
curl -sL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp -o src/json.hpp
```

Verify the file is ~1MB+ (the full single-header release).

- [ ] **Step 2: Add test cases for JSON-wrapper decoding to tests/test_BCPClient.cpp**

Add these test cases at the end of the file:

```cpp
TEST_CASE("DecodeLine: json= wrapper extracts inner keys") {
    std::string line = "vpcom_bridge_response?json=%7B%22result%22%3A%20%5B%5B%22l-1%22%2C%20true%5D%5D%7D";
    BCPResponse resp = BCPClient::DecodeLine(line);
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params.count("result") == 1);
    // result should be the raw JSON value string: [["l-1", true]]
    CHECK(resp.params["result"].find("l-1") != std::string::npos);
    CHECK(resp.params["result"].find("true") != std::string::npos);
}

TEST_CASE("DecodeLine: json= wrapper with multiple keys") {
    // json={"result": "ok", "extra": 42}
    std::string line = "cmd?json=%7B%22result%22%3A%22ok%22%2C%22extra%22%3A42%7D";
    BCPResponse resp = BCPClient::DecodeLine(line);
    CHECK(resp.command == "cmd");
    CHECK(resp.params["result"] == "\"ok\"");
    CHECK(resp.params["extra"] == "42");
}

TEST_CASE("DecodeLine: non-json response still works") {
    std::string line = "vpcom_bridge_response?result=True&subcommand=switch";
    BCPResponse resp = BCPClient::DecodeLine(line);
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params["result"] == "True");
    CHECK(resp.params["subcommand"] == "switch");
}
```

- [ ] **Step 3: Modify BCPClient::DecodeLine to handle json= wrapper**

In `src/BCPClient.cpp`, add `#include "json.hpp"` at the top (after the existing includes), and modify `DecodeLine` to check for a `json` key after the normal URL-decode parsing:

```cpp
#include "json.hpp"

// ... existing code ...

BCPResponse BCPClient::DecodeLine(const std::string& line) {
    BCPResponse resp;
    size_t qpos = line.find('?');
    if (qpos == std::string::npos) {
        resp.command = line;
        return resp;
    }
    resp.command = line.substr(0, qpos);
    std::string query = line.substr(qpos + 1);

    // Split on '&'
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eqpos = pair.find('=');
        if (eqpos == std::string::npos) {
            resp.params[pair] = "";
        } else {
            std::string key = pair.substr(0, eqpos);
            std::string val = pair.substr(eqpos + 1);
            resp.params[key] = UrlDecode(val);
        }
    }

    // If there's a "json" key, parse the JSON and flatten inner keys
    auto jsonIt = resp.params.find("json");
    if (jsonIt != resp.params.end()) {
        try {
            auto j = nlohmann::json::parse(jsonIt->second);
            if (j.is_object()) {
                for (auto& [key, value] : j.items()) {
                    resp.params[key] = value.dump();
                }
            }
        } catch (...) {
            // If JSON parsing fails, leave the raw "json" key as-is
        }
        resp.params.erase("json");
    }

    return resp;
}
```

- [ ] **Step 4: Build and run tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All existing tests still pass, plus the 3 new JSON-wrapper tests pass.

Note: First build after adding json.hpp will be slow (~30s extra) due to template-heavy nlohmann/json compilation. Subsequent builds are fast (cached).

- [ ] **Step 5: Commit**

```bash
git add src/json.hpp src/BCPClient.cpp tests/test_BCPClient.cpp
git commit -m "Add nlohmann/json, extend BCPClient::DecodeLine to handle json= wrapper"
```

---

### Task 2: ChangedItems and HardwareRuleItems — tests then implementation

**Files:**
- Create: `src/ChangedItems.h`
- Create: `src/ChangedItems.cpp`
- Create: `tests/test_ChangedItems.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create src/ChangedItems.h**

```cpp
#pragma once

#include <string>
#include <vector>
#include <tuple>
#include <cstdint>

namespace MPF {

class ChangedItems {
public:
    ChangedItems(std::vector<std::pair<std::string, bool>> items);
    ChangedItems(std::vector<std::pair<std::string, bool>> items,
                 std::vector<float> brightness);

    uint32_t AddRef() const { m_refCount++; return m_refCount; }
    uint32_t Release() const { m_refCount--; uint32_t rc = m_refCount; if (rc == 0) delete this; return rc; }

    int GetCount() const;
    std::string GetId(int index) const;
    bool GetState(int index) const;
    float GetBrightness(int index) const;

private:
    std::vector<std::pair<std::string, bool>> m_items;
    std::vector<float> m_brightness;
    mutable uint32_t m_refCount = 1;
};

class HardwareRuleItems {
public:
    HardwareRuleItems(std::vector<std::tuple<std::string, std::string, bool>> rules);

    uint32_t AddRef() const { m_refCount++; return m_refCount; }
    uint32_t Release() const { m_refCount--; uint32_t rc = m_refCount; if (rc == 0) delete this; return rc; }

    int GetCount() const;
    std::string GetSwitch(int index) const;
    std::string GetCoil(int index) const;
    bool GetHold(int index) const;

private:
    std::vector<std::tuple<std::string, std::string, bool>> m_rules;
    mutable uint32_t m_refCount = 1;
};

// JSON parsing helpers
std::vector<std::pair<std::string, bool>> ParseChangedList(const std::string& jsonStr);
std::vector<std::tuple<std::string, bool, float>> ParseChangedBrightnessList(const std::string& jsonStr);
std::vector<std::tuple<std::string, std::string, bool>> ParseHardwareRulesList(const std::string& jsonStr);

} // namespace MPF
```

Note: We inline AddRef/Release rather than using PSC_IMPLEMENT_REFCOUNT() because the PSC macro requires including ScriptablePlugin.h which pulls in the VPX SDK. ChangedItems is a data class tested independently — it doesn't need the VPX SDK headers. The PSC registration in MPFPlugin.cpp will bind to these methods by name.

- [ ] **Step 2: Create tests/test_ChangedItems.cpp**

```cpp
#include "doctest.h"
#include "ChangedItems.h"

using namespace MPF;

// ---------------------------------------------------------------------------
// ChangedItems
// ---------------------------------------------------------------------------

TEST_CASE("ChangedItems: Count returns number of items") {
    auto* ci = new ChangedItems({{"l-1", true}, {"l-2", false}});
    CHECK(ci->GetCount() == 2);
    ci->Release();
}

TEST_CASE("ChangedItems: Id returns string at index") {
    auto* ci = new ChangedItems({{"coil1", true}, {"coil2", false}});
    CHECK(ci->GetId(0) == "coil1");
    CHECK(ci->GetId(1) == "coil2");
    ci->Release();
}

TEST_CASE("ChangedItems: State returns bool at index") {
    auto* ci = new ChangedItems({{"l-1", true}, {"l-2", false}});
    CHECK(ci->GetState(0) == true);
    CHECK(ci->GetState(1) == false);
    ci->Release();
}

TEST_CASE("ChangedItems: out of bounds returns defaults") {
    auto* ci = new ChangedItems({{"l-1", true}});
    CHECK(ci->GetId(99) == "");
    CHECK(ci->GetState(99) == false);
    CHECK(ci->GetBrightness(99) == 0.0f);
    ci->Release();
}

TEST_CASE("ChangedItems: Brightness returns float when populated") {
    auto* ci = new ChangedItems(
        {{"led1", true}, {"led2", false}},
        {0.8f, 0.2f}
    );
    CHECK(ci->GetBrightness(0) == doctest::Approx(0.8f));
    CHECK(ci->GetBrightness(1) == doctest::Approx(0.2f));
    ci->Release();
}

TEST_CASE("ChangedItems: Brightness returns 0 when not populated") {
    auto* ci = new ChangedItems({{"l-1", true}});
    CHECK(ci->GetBrightness(0) == 0.0f);
    ci->Release();
}

TEST_CASE("ChangedItems: empty items") {
    auto* ci = new ChangedItems({});
    CHECK(ci->GetCount() == 0);
    ci->Release();
}

// ---------------------------------------------------------------------------
// HardwareRuleItems
// ---------------------------------------------------------------------------

TEST_CASE("HardwareRuleItems: Count/Switch/Coil/Hold") {
    auto* hr = new HardwareRuleItems({
        {"sw1", "coil1", true},
        {"sw2", "coil2", false}
    });
    CHECK(hr->GetCount() == 2);
    CHECK(hr->GetSwitch(0) == "sw1");
    CHECK(hr->GetCoil(0) == "coil1");
    CHECK(hr->GetHold(0) == true);
    CHECK(hr->GetSwitch(1) == "sw2");
    CHECK(hr->GetCoil(1) == "coil2");
    CHECK(hr->GetHold(1) == false);
    hr->Release();
}

TEST_CASE("HardwareRuleItems: out of bounds") {
    auto* hr = new HardwareRuleItems({{"sw1", "coil1", true}});
    CHECK(hr->GetSwitch(99) == "");
    CHECK(hr->GetCoil(99) == "");
    CHECK(hr->GetHold(99) == false);
    hr->Release();
}

// ---------------------------------------------------------------------------
// ParseChangedList
// ---------------------------------------------------------------------------

TEST_CASE("ParseChangedList: string IDs with bool states") {
    auto result = ParseChangedList(R"([["l-1", true], ["l-2", false]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].first == "l-1");
    CHECK(result[0].second == true);
    CHECK(result[1].first == "l-2");
    CHECK(result[1].second == false);
}

TEST_CASE("ParseChangedList: numeric IDs coerced to string") {
    auto result = ParseChangedList(R"([[42, true], [7, false]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].first == "42");
    CHECK(result[1].first == "7");
}

TEST_CASE("ParseChangedList: int states coerced to bool") {
    auto result = ParseChangedList(R"([["a", 1], ["b", 0]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].second == true);
    CHECK(result[1].second == false);
}

TEST_CASE("ParseChangedList: empty array") {
    auto result = ParseChangedList("[]");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: false sentinel (no changes)") {
    auto result = ParseChangedList("false");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: quoted false sentinel") {
    auto result = ParseChangedList("\"false\"");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: malformed JSON returns empty") {
    auto result = ParseChangedList("not json at all");
    CHECK(result.empty());
}

// ---------------------------------------------------------------------------
// ParseChangedBrightnessList
// ---------------------------------------------------------------------------

TEST_CASE("ParseChangedBrightnessList: float brightness values") {
    auto result = ParseChangedBrightnessList(R"([["led1", 0.8], ["led2", 0.2]])");
    REQUIRE(result.size() == 2);
    CHECK(std::get<0>(result[0]) == "led1");
    CHECK(std::get<1>(result[0]) == true);  // 0.8 > 0.5
    CHECK(std::get<2>(result[0]) == doctest::Approx(0.8f));
    CHECK(std::get<0>(result[1]) == "led2");
    CHECK(std::get<1>(result[1]) == false);  // 0.2 <= 0.5
    CHECK(std::get<2>(result[1]) == doctest::Approx(0.2f));
}

TEST_CASE("ParseChangedBrightnessList: empty and false") {
    CHECK(ParseChangedBrightnessList("[]").empty());
    CHECK(ParseChangedBrightnessList("false").empty());
}

// ---------------------------------------------------------------------------
// ParseHardwareRulesList
// ---------------------------------------------------------------------------

TEST_CASE("ParseHardwareRulesList: 3-tuples") {
    auto result = ParseHardwareRulesList(R"([["sw1", "coil1", true], ["sw2", "coil2", false]])");
    REQUIRE(result.size() == 2);
    CHECK(std::get<0>(result[0]) == "sw1");
    CHECK(std::get<1>(result[0]) == "coil1");
    CHECK(std::get<2>(result[0]) == true);
}

TEST_CASE("ParseHardwareRulesList: numeric IDs coerced") {
    auto result = ParseHardwareRulesList(R"([[1, 2, true]])");
    REQUIRE(result.size() == 1);
    CHECK(std::get<0>(result[0]) == "1");
    CHECK(std::get<1>(result[0]) == "2");
}

TEST_CASE("ParseHardwareRulesList: empty and false") {
    CHECK(ParseHardwareRulesList("[]").empty());
    CHECK(ParseHardwareRulesList("false").empty());
}
```

- [ ] **Step 3: Create src/ChangedItems.cpp**

```cpp
#include "ChangedItems.h"
#include "json.hpp"

namespace MPF {

// ---------------------------------------------------------------------------
// ChangedItems
// ---------------------------------------------------------------------------

ChangedItems::ChangedItems(std::vector<std::pair<std::string, bool>> items)
    : m_items(std::move(items)) {}

ChangedItems::ChangedItems(std::vector<std::pair<std::string, bool>> items,
                           std::vector<float> brightness)
    : m_items(std::move(items)), m_brightness(std::move(brightness)) {}

int ChangedItems::GetCount() const {
    return static_cast<int>(m_items.size());
}

std::string ChangedItems::GetId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return "";
    return m_items[index].first;
}

bool ChangedItems::GetState(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return false;
    return m_items[index].second;
}

float ChangedItems::GetBrightness(int index) const {
    if (index < 0 || index >= static_cast<int>(m_brightness.size())) return 0.0f;
    return m_brightness[index];
}

// ---------------------------------------------------------------------------
// HardwareRuleItems
// ---------------------------------------------------------------------------

HardwareRuleItems::HardwareRuleItems(std::vector<std::tuple<std::string, std::string, bool>> rules)
    : m_rules(std::move(rules)) {}

int HardwareRuleItems::GetCount() const {
    return static_cast<int>(m_rules.size());
}

std::string HardwareRuleItems::GetSwitch(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return "";
    return std::get<0>(m_rules[index]);
}

std::string HardwareRuleItems::GetCoil(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return "";
    return std::get<1>(m_rules[index]);
}

bool HardwareRuleItems::GetHold(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return false;
    return std::get<2>(m_rules[index]);
}

// ---------------------------------------------------------------------------
// Helper: coerce a JSON value to a string ID
// ---------------------------------------------------------------------------

static std::string JsonToStringId(const nlohmann::json& val) {
    if (val.is_string()) return val.get<std::string>();
    if (val.is_number_integer()) return std::to_string(val.get<int64_t>());
    if (val.is_number_float()) return std::to_string(val.get<double>());
    return val.dump();
}

static bool JsonToBool(const nlohmann::json& val) {
    if (val.is_boolean()) return val.get<bool>();
    if (val.is_number()) return val.get<double>() != 0.0;
    return false;
}

// ---------------------------------------------------------------------------
// ParseChangedList
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, bool>> ParseChangedList(const std::string& jsonStr) {
    std::vector<std::pair<std::string, bool>> result;
    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"")
        return result;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.is_array()) return result;
        for (const auto& item : j) {
            if (!item.is_array() || item.size() < 2) continue;
            std::string id = JsonToStringId(item[0]);
            bool state = JsonToBool(item[1]);
            result.emplace_back(std::move(id), state);
        }
    } catch (...) {
        // Malformed JSON — return empty
    }
    return result;
}

// ---------------------------------------------------------------------------
// ParseChangedBrightnessList
// ---------------------------------------------------------------------------

std::vector<std::tuple<std::string, bool, float>> ParseChangedBrightnessList(const std::string& jsonStr) {
    std::vector<std::tuple<std::string, bool, float>> result;
    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"")
        return result;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.is_array()) return result;
        for (const auto& item : j) {
            if (!item.is_array() || item.size() < 2) continue;
            std::string id = JsonToStringId(item[0]);
            float brightness = item[1].is_number() ? item[1].get<float>() : 0.0f;
            bool state = brightness > 0.5f;
            result.emplace_back(std::move(id), state, brightness);
        }
    } catch (...) {}
    return result;
}

// ---------------------------------------------------------------------------
// ParseHardwareRulesList
// ---------------------------------------------------------------------------

std::vector<std::tuple<std::string, std::string, bool>> ParseHardwareRulesList(const std::string& jsonStr) {
    std::vector<std::tuple<std::string, std::string, bool>> result;
    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"")
        return result;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (!j.is_array()) return result;
        for (const auto& item : j) {
            if (!item.is_array() || item.size() < 3) continue;
            std::string sw = JsonToStringId(item[0]);
            std::string coil = JsonToStringId(item[1]);
            bool hold = JsonToBool(item[2]);
            result.emplace_back(std::move(sw), std::move(coil), hold);
        }
    } catch (...) {}
    return result;
}

} // namespace MPF
```

- [ ] **Step 4: Update CMakeLists.txt — add ChangedItems to both targets**

Plugin target:
```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
    src/MPFController.cpp
    src/ChangedItems.cpp
)
```

Test target:
```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
    tests/test_Recorder.cpp
    tests/test_MPFController.cpp
    tests/test_ChangedItems.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
    src/MPFController.cpp
    src/ChangedItems.cpp
)
```

- [ ] **Step 5: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All tests pass including the ~20 new ChangedItems/Parse* tests.

- [ ] **Step 6: Commit**

```bash
git add src/ChangedItems.h src/ChangedItems.cpp tests/test_ChangedItems.cpp CMakeLists.txt
git commit -m "Add ChangedItems, HardwareRuleItems, and JSON parsing with tests"
```

---

### Task 3: Rewire MPFController Changed* methods

**Files:**
- Modify: `src/MPFController.h`
- Modify: `src/MPFController.cpp`
- Modify: `tests/test_MPFController.cpp`

- [ ] **Step 1: Update MPFController.h — change return types**

Replace the polled state section:

```cpp
// Before:
    std::string GetChangedSolenoids();
    std::string GetChangedLamps();
    std::string GetChangedGIStrings();
    std::string GetChangedLEDs();
    std::string GetChangedBrightnessLEDs();
    std::string GetChangedFlashers();
    std::string GetHardwareRules();

// After:
    ChangedItems* GetChangedSolenoids();
    ChangedItems* GetChangedLamps();
    ChangedItems* GetChangedGIStrings();
    ChangedItems* GetChangedLEDs();
    ChangedItems* GetChangedBrightnessLEDs();
    ChangedItems* GetChangedFlashers();
    HardwareRuleItems* GetHardwareRules();
```

Add `#include "ChangedItems.h"` at the top.

- [ ] **Step 2: Update MPFController.cpp — implement new return types**

Replace the polled state section (lines 179-209):

```cpp
// ---------------------------------------------------------------------------
// Polled state
// ---------------------------------------------------------------------------

ChangedItems* MPFController::GetChangedSolenoids() {
    std::string raw = DispatchToMPF("state", "changed_solenoids");
    auto items = ParseChangedList(raw);
    return items.empty() ? nullptr : new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedLamps() {
    std::string raw = DispatchToMPF("state", "changed_lamps");
    auto items = ParseChangedList(raw);
    return items.empty() ? nullptr : new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedGIStrings() {
    std::string raw = DispatchToMPF("state", "changed_gi_strings");
    auto items = ParseChangedList(raw);
    return items.empty() ? nullptr : new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedLEDs() {
    std::string raw = DispatchToMPF("state", "changed_leds");
    auto items = ParseChangedList(raw);
    return items.empty() ? nullptr : new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedBrightnessLEDs() {
    std::string raw = DispatchToMPF("state", "changed_brightness_leds");
    auto parsed = ParseChangedBrightnessList(raw);
    if (parsed.empty()) return nullptr;
    std::vector<std::pair<std::string, bool>> items;
    std::vector<float> brightness;
    items.reserve(parsed.size());
    brightness.reserve(parsed.size());
    for (auto& [id, state, bright] : parsed) {
        items.emplace_back(std::move(id), state);
        brightness.push_back(bright);
    }
    return new ChangedItems(std::move(items), std::move(brightness));
}

ChangedItems* MPFController::GetChangedFlashers() {
    std::string raw = DispatchToMPF("state", "changed_flashers");
    auto items = ParseChangedList(raw);
    return items.empty() ? nullptr : new ChangedItems(std::move(items));
}

HardwareRuleItems* MPFController::GetHardwareRules() {
    std::string raw = DispatchToMPF("state", "get_hardwarerules");
    auto rules = ParseHardwareRulesList(raw);
    return rules.empty() ? nullptr : new HardwareRuleItems(std::move(rules));
}
```

- [ ] **Step 3: Update mock server handler in tests/test_MPFController.cpp**

Change the mock handler to return JSON-wrapped responses for the Changed* subcommands. Replace the existing MakeMPFHandler (or equivalent):

For `changed_solenoids`, `changed_lamps`, `changed_leds`, `changed_gi_strings`, `changed_flashers`, return a proper JSON-wrapped response:

```cpp
if (subcmd == "changed_solenoids")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["coil1", true], ["coil2", false]]})");
if (subcmd == "changed_lamps")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["l-1", true], ["l-2", false]]})");
if (subcmd == "changed_gi_strings")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["gi1", true]]})");
if (subcmd == "changed_leds")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["led1", true]]})");
if (subcmd == "changed_brightness_leds")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["led1", 0.8], ["led2", 0.2]]})");
if (subcmd == "changed_flashers")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["f1", true]]})");
if (subcmd == "get_hardwarerules")
    return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["sw1", "coil1", true]]})");
```

- [ ] **Step 4: Update MPFController test cases**

Replace the existing `ChangedSolenoids returns result string` test and add proper object tests:

```cpp
TEST_CASE("MPFController: ChangedSolenoids returns ChangedItems") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    ChangedItems* result = ctrl.GetChangedSolenoids();
    REQUIRE(result != nullptr);
    CHECK(result->GetCount() == 2);
    CHECK(result->GetId(0) == "coil1");
    CHECK(result->GetState(0) == true);
    CHECK(result->GetId(1) == "coil2");
    CHECK(result->GetState(1) == false);
    result->Release();

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: ChangedLamps returns ChangedItems with string IDs") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    ChangedItems* result = ctrl.GetChangedLamps();
    REQUIRE(result != nullptr);
    CHECK(result->GetCount() == 2);
    CHECK(result->GetId(0) == "l-1");
    CHECK(result->GetState(0) == true);
    result->Release();

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: ChangedBrightnessLEDs has brightness values") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    ChangedItems* result = ctrl.GetChangedBrightnessLEDs();
    REQUIRE(result != nullptr);
    CHECK(result->GetCount() == 2);
    CHECK(result->GetBrightness(0) == doctest::Approx(0.8f));
    CHECK(result->GetState(0) == true);   // 0.8 > 0.5
    CHECK(result->GetBrightness(1) == doctest::Approx(0.2f));
    CHECK(result->GetState(1) == false);  // 0.2 <= 0.5
    result->Release();

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: HardwareRules returns HardwareRuleItems") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    HardwareRuleItems* result = ctrl.GetHardwareRules();
    REQUIRE(result != nullptr);
    CHECK(result->GetCount() == 1);
    CHECK(result->GetSwitch(0) == "sw1");
    CHECK(result->GetCoil(0) == "coil1");
    CHECK(result->GetHold(0) == true);
    result->Release();

    ctrl.Stop();
    server.Stop();
}
```

Also add `#include "ChangedItems.h"` at the top of the test file.

- [ ] **Step 5: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/MPFController.h src/MPFController.cpp tests/test_MPFController.cpp
git commit -m "Rewire Changed* methods to return ChangedItems/HardwareRuleItems objects"
```

---

### Task 4: Update MPFPlugin PSC registration

**Files:**
- Modify: `src/MPFPlugin.cpp`

- [ ] **Step 1: Add MPF_ChangedItems and MPF_HardwareRuleItems PSC class registrations**

In `src/MPFPlugin.cpp`, add the class definitions for both result types before the existing `PSC_CLASS_START(MPF_Controller)` block. Also add the `#include "ChangedItems.h"`:

```cpp
#include "ChangedItems.h"

// ... existing code ...

// Alias types for PSC macros
using MPF_ChangedItems = ChangedItems;
using MPF_HardwareRuleItems = HardwareRuleItems;

PSC_CLASS_START(MPF_ChangedItems)
    PSC_PROP_R(MPF_ChangedItems, int, Count)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, string, Id, int)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, bool, State, int)
    PSC_PROP_R_ARRAY1(MPF_ChangedItems, float, Brightness, int)
PSC_CLASS_END(MPF_ChangedItems)

PSC_CLASS_START(MPF_HardwareRuleItems)
    PSC_PROP_R(MPF_HardwareRuleItems, int, Count)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, string, Switch, int)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, string, Coil, int)
    PSC_PROP_R_ARRAY1(MPF_HardwareRuleItems, bool, Hold, int)
PSC_CLASS_END(MPF_HardwareRuleItems)
```

- [ ] **Step 2: Update Changed* PSC registrations in MPF_Controller block**

Replace the polled state lines:

```cpp
// Before:
    PSC_PROP_R(MPF_Controller, string, ChangedSolenoids)
    PSC_PROP_R(MPF_Controller, string, ChangedLamps)
    PSC_PROP_R(MPF_Controller, string, ChangedGIStrings)
    PSC_PROP_R(MPF_Controller, string, ChangedLEDs)
    PSC_PROP_R(MPF_Controller, string, ChangedBrightnessLEDs)
    PSC_PROP_R(MPF_Controller, string, ChangedFlashers)
    PSC_PROP_R(MPF_Controller, string, HardwareRules)

// After:
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedSolenoids)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedLamps)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedGIStrings)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedLEDs)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedBrightnessLEDs)
    PSC_PROP_R(MPF_Controller, MPF_ChangedItems, ChangedFlashers)
    PSC_PROP_R(MPF_Controller, MPF_HardwareRuleItems, HardwareRules)
```

- [ ] **Step 3: Register the new classes in MPFPluginLoad**

In the `MPFPluginLoad` function, register both new classes before the controller:

```cpp
MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    msgApi = api;

    getScriptApiMsgId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
    msgApi->BroadcastMsg(sessionId, getScriptApiMsgId, &scriptApi);

    auto regLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    RegisterMPF_ChangedItemsSCD(regLambda);
    RegisterMPF_HardwareRuleItemsSCD(regLambda);
    RegisterMPF_ControllerSCD(regLambda);

    // ... rest of CreateObject factory + SubmitTypeLibrary + SetCOMObjectOverride unchanged
```

And in `MPFPluginUnload`, unregister them:

```cpp
    auto unregLambda = [](ScriptClassDef* scd) { scriptApi->UnregisterScriptClass(scd); };
    UnregisterMPF_ControllerSCD(unregLambda);
    UnregisterMPF_HardwareRuleItemsSCD(unregLambda);
    UnregisterMPF_ChangedItemsSCD(unregLambda);
```

Note: The exact function names (`RegisterMPF_ChangedItemsSCD` vs `RegisterMPF_ChangedItems`) depend on how the PSC_CLASS_START macro in the tagged VPX SDK generates them. Look at the existing `RegisterMPF_ControllerSCD` call on line 106 of the current MPFPlugin.cpp for the naming pattern. It may be `RegisterMPF_ChangedItems` (without SCD suffix) — match whatever the existing pattern uses.

- [ ] **Step 4: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Plugin compiles with the new PSC registrations. All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/MPFPlugin.cpp
git commit -m "Register MPF_ChangedItems and MPF_HardwareRuleItems scriptable classes in PSC"
```

---

### Task 5: VBScript helper layer

**Files:**
- Create: `scripts/mpf_controller.vbs`

- [ ] **Step 1: Create scripts/mpf_controller.vbs**

```vbs
' MPF Controller VBScript Helper
' Converts MPF plugin Changed* object results into 2D variant arrays
' compatible with existing VPX table code.
'
' Usage: Add this line near the top of your table script:
'   ExecuteGlobal GetTextFile("mpf_controller.vbs")
'
' Then replace Controller.ChangedLamps with MPF_ChangedLamps(Controller), etc.

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

Function MPF_ChangedSolenoids(Controller)
    Dim r : Set r = Controller.ChangedSolenoids
    If r Is Nothing Then
        MPF_ChangedSolenoids = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedSolenoids = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedSolenoids = arr
End Function

Function MPF_ChangedGIStrings(Controller)
    Dim r : Set r = Controller.ChangedGIStrings
    If r Is Nothing Then
        MPF_ChangedGIStrings = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedGIStrings = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedGIStrings = arr
End Function

Function MPF_ChangedLEDs(Controller)
    Dim r : Set r = Controller.ChangedLEDs
    If r Is Nothing Then
        MPF_ChangedLEDs = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedLEDs = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedLEDs = arr
End Function

Function MPF_ChangedBrightnessLEDs(Controller)
    Dim r : Set r = Controller.ChangedBrightnessLEDs
    If r Is Nothing Then
        MPF_ChangedBrightnessLEDs = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedBrightnessLEDs = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.Brightness(i)
    Next
    MPF_ChangedBrightnessLEDs = arr
End Function

Function MPF_ChangedFlashers(Controller)
    Dim r : Set r = Controller.ChangedFlashers
    If r Is Nothing Then
        MPF_ChangedFlashers = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_ChangedFlashers = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 1)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Id(i)
        arr(i, 1) = r.State(i)
    Next
    MPF_ChangedFlashers = arr
End Function

Function MPF_HardwareRules(Controller)
    Dim r : Set r = Controller.HardwareRules
    If r Is Nothing Then
        MPF_HardwareRules = Empty
        Exit Function
    End If
    Dim n : n = r.Count
    If n = 0 Then
        MPF_HardwareRules = Empty
        Exit Function
    End If
    Dim arr() : ReDim arr(n - 1, 2)
    Dim i
    For i = 0 To n - 1
        arr(i, 0) = r.Switch(i)
        arr(i, 1) = r.Coil(i)
        arr(i, 2) = r.Hold(i)
    Next
    MPF_HardwareRules = arr
End Function
```

- [ ] **Step 2: Commit**

```bash
git add scripts/mpf_controller.vbs
git commit -m "Add VBScript helper layer for Changed* to 2D array conversion"
```

---

### Task 6: End-to-end verification

**Files:** None (verification only)

- [ ] **Step 1: Clean build and full test suite**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Expected: All tests pass.

- [ ] **Step 2: Verify plugin binary**

```bash
nm -gU build/dist/mpf/plugin-mpf.dylib | grep -i "Plugin"
```

Expected: `MPFPluginLoad` and `MPFPluginUnload` visible.

- [ ] **Step 3: Install updated plugin to VPX**

```bash
cp build/dist/mpf/plugin-mpf.dylib ~/Applications/VPinballX_BGFX.app/Contents/Resources/plugins/mpf/
cp scripts/mpf_controller.vbs ~/Applications/VPinballX_BGFX.app/Contents/Resources/scripts/
```

(May need sudo or re-signing — follow the same procedure as initial install.)

- [ ] **Step 4: Push**

```bash
git push origin main
```
