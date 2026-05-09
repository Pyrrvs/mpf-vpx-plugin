#include "MPFController.h"
#include "Log.h"

#include <stdexcept>

namespace MPF {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// BCP encodes typed values with a prefix: "bool:True", "int:42", "float:1.5".
// Strip the prefix so helpers can parse the raw value.
static std::string StripBcpTypePrefix(const std::string& s) {
    auto pos = s.find(':');
    if (pos != std::string::npos) return s.substr(pos + 1);
    return s;
}

static bool ToBool(const std::string& s) {
    std::string v = StripBcpTypePrefix(s);
    return v == "True" || v == "true" || v == "1";
}

static int ToInt(const std::string& s, int fallback = 0) {
    try {
        return std::stoi(StripBcpTypePrefix(s));
    } catch (...) {
        return fallback;
    }
}

std::string MPFController::ParamsToJson(const std::map<std::string, std::string>& params) {
    if (params.empty()) return "{}";
    std::string out = "{";
    bool first = true;
    for (const auto& [key, val] : params) {
        if (!first) out += ',';
        out += '"';
        out += key;
        out += "\":\"";
        // Minimal JSON escaping for values
        for (char c : val) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                default:   out += c;
            }
        }
        out += '"';
        first = false;
    }
    out += '}';
    return out;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

MPFController::MPFController(bool recordingEnabled, const std::string& recordingPath) {
    m_recorder.SetEnabled(recordingEnabled);
    m_recorder.SetOutputDirectory(recordingPath);
}

MPFController::~MPFController() {
    if (m_bcp.IsConnected()) {
        m_recorder.StopSession();
        m_bcp.Disconnect();
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MPFController::Run() {
    Run("localhost", 5051);
}

void MPFController::Run(const std::string& addr) {
    Run(addr, 5051);
}

void MPFController::Run(const std::string& addr, int port) {
    MPF_LOGI("MPFController::Run(%s, %d)", addr.c_str(), port);
    m_bcp.ConnectWithRetry(addr, port, /*intervalMs=*/2000);
    MPF_LOGD("MPFController::Run - connected, sending start handshake");
    BCPResponse startResp = m_bcp.SendAndWait(
        "vpcom_bridge", {{"subcommand", "start"}}, "vpcom_bridge_response");
    if (startResp.command.empty()) {
        MPF_LOGE("MPFController::Run - start handshake failed (empty response)");
    } else {
        MPF_LOGI("MPFController::Run - start OK");
    }

    SendResetHandshake();
    ReplaySwitchMirror();
    m_recorder.StartSession();
}

void MPFController::Stop() {
    m_recorder.StopSession();
    m_bcp.Send("vpcom_bridge", {{"subcommand", "stop"}});
    m_bcp.Disconnect();
}

void MPFController::SendResetHandshake() {
    BCPResponse resp = m_bcp.SendAndWait(
        "vpcom_bridge", {{"subcommand", "reset"}}, "vpcom_bridge_response");
    auto errIt = resp.params.find("error");
    if (errIt == resp.params.end()) {
        MPF_LOGI("MPFController::SendResetHandshake - reset OK");
        return;
    }
    const std::string& err = errIt->second;
    if (err.find("Unknown command") != std::string::npos &&
        err.find("reset") != std::string::npos) {
        if (!m_resetUnsupportedWarned) {
            MPF_LOGW("MPFController: this MPF lacks vpx_reset; state will not be "
                     "cleaned between sessions. Upgrade MPF to a build with "
                     "vpcom_bridge?subcommand=reset.");
            m_resetUnsupportedWarned = true;
        }
        return;
    }
    MPF_LOGW("MPFController: vpx_reset returned error: %s; continuing", err.c_str());
}

void MPFController::ReplaySwitchMirror() {
    if (m_switchMirror.empty()) return;
    MPF_LOGI("MPFController::ReplaySwitchMirror - replaying %zu switches",
             m_switchMirror.size());
    for (const auto& [num, value] : m_switchMirror) {
        m_bcp.Send("vpcom_bridge", {
            {"subcommand", "set_switch"},
            {"number", num},
            {"value", value ? "bool:True" : "bool:False"}
        });
    }
}

// ---------------------------------------------------------------------------
// DispatchToMPF
// ---------------------------------------------------------------------------

// Returns true if the result carries meaningful data worth recording.
// Empty strings and empty JSON arrays are per-frame poll noise.
static bool IsResultSignificant(const std::string& result) {
    return !result.empty() && result != "[]";
}

std::string MPFController::DispatchToMPF(const char* category,
                                         const std::string& subcommand,
                                         const std::map<std::string, std::string>& extraParams)
{
    std::map<std::string, std::string> params = extraParams;
    params["subcommand"] = subcommand;

    double tsBefore = 0;
    bool shouldRecord = m_recorder.IsEnabled()
        && subcommand != "get_coilactive"
        && subcommand != "switch";
    if (shouldRecord) {
        tsBefore = m_recorder.Now();
    }

    BCPResponse resp = m_bcp.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");

    std::string resultVal;
    if (!resp.command.empty()) {
        auto it = resp.params.find("result");
        if (it != resp.params.end()) resultVal = it->second;
    }

    if (shouldRecord) {
        // For "input" events (set_switch, pulsesw) always record.
        // For polls (changed_*, switch reads) only record when result has data.
        bool isInput = (category[0] == 'i'); // "input"
        if (isInput || IsResultSignificant(resultVal)) {
            m_recorder.Record({tsBefore, category, "vpx_to_mpf",
                               subcommand, ParamsToJson(extraParams), ""});
            m_recorder.Record({m_recorder.Now(), category, "mpf_to_vpx",
                               subcommand, "", resultVal.empty() ? "" : resultVal});
        }
    }

    auto errIt = resp.params.find("error");
    if (errIt != resp.params.end()) return "";

    return resultVal;
}

// ---------------------------------------------------------------------------
// Switch access
// ---------------------------------------------------------------------------

bool MPFController::GetSwitch(int number) {
    std::string result = DispatchToMPF("query", "switch", {{"number", std::to_string(number)}});
    return ToBool(result);
}

bool MPFController::GetSwitch(const std::string& number) {
    std::string result = DispatchToMPF("query", "switch", {{"number", number}});
    return ToBool(result);
}

void MPFController::SetSwitch(int number, bool value) {
    m_switchMirror[std::to_string(number)] = value;
    DispatchToMPF("input", "set_switch", {
        {"number", std::to_string(number)},
        {"value", value ? "bool:True" : "bool:False"}
    });
}

void MPFController::SetSwitch(const std::string& number, bool value) {
    m_switchMirror[number] = value;
    DispatchToMPF("input", "set_switch", {
        {"number", number},
        {"value", value ? "bool:True" : "bool:False"}
    });
}

void MPFController::PulseSW(int number) {
    m_switchMirror[std::to_string(number)] = false;
    DispatchToMPF("input", "pulsesw", {{"number", std::to_string(number)}});
}

void MPFController::PulseSW(const std::string& number) {
    m_switchMirror[number] = false;
    DispatchToMPF("input", "pulsesw", {{"number", number}});
}

// ---------------------------------------------------------------------------
// Mech access
// ---------------------------------------------------------------------------

int MPFController::ReadMech(int number) {
    std::string result = DispatchToMPF("query", "mech", {{"number", std::to_string(number)}});
    return ToInt(result, 0);
}

void MPFController::SetMech(int number, int value) {
    DispatchToMPF("input", "set_mech", {
        {"number", std::to_string(number)},
        {"value", std::to_string(value)}
    });
}

int MPFController::GetMech(int number) {
    std::string result = DispatchToMPF("query", "get_mech", {{"number", std::to_string(number)}});
    return ToInt(result, 0);
}

// ---------------------------------------------------------------------------
// Polled state
// ---------------------------------------------------------------------------

ChangedItems* MPFController::GetChangedSolenoids() {
    std::string raw = DispatchToMPF("state", "changed_solenoids");
    auto items = ParseChangedList(raw);
    return new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedLamps() {
    std::string raw = DispatchToMPF("state", "changed_lamps");
    auto items = ParseChangedList(raw);
    return new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedGIStrings() {
    std::string raw = DispatchToMPF("state", "changed_gi_strings");
    auto items = ParseChangedList(raw);
    return new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedLEDs() {
    std::string raw = DispatchToMPF("state", "changed_leds");
    auto items = ParseChangedList(raw);
    return new ChangedItems(std::move(items));
}

ChangedItems* MPFController::GetChangedBrightnessLEDs() {
    std::string raw = DispatchToMPF("state", "changed_brightness_leds");
    auto parsed = ParseChangedBrightnessList(raw);
    if (parsed.empty()) return new ChangedItems({});
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
    return new ChangedItems(std::move(items));
}

HardwareRuleItems* MPFController::GetHardwareRules() {
    std::string raw = DispatchToMPF("state", "get_hardwarerules");
    auto rules = ParseHardwareRulesList(raw);
    return new HardwareRuleItems(std::move(rules));
}

bool MPFController::IsCoilActive(int number) {
    std::string result = DispatchToMPF("state", "get_coilactive", {{"number", std::to_string(number)}});
    return ToBool(result);
}

bool MPFController::IsCoilActive(const std::string& number) {
    std::string result = DispatchToMPF("state", "get_coilactive", {{"number", number}});
    return ToBool(result);
}

} // namespace MPF
