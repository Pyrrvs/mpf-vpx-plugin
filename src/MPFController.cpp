#include "MPFController.h"
#include "Log.h"

#include <stdexcept>

namespace MPF {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool ToBool(const std::string& s) {
    return s == "True" || s == "true" || s == "1";
}

static int ToInt(const std::string& s, int fallback = 0) {
    try {
        return std::stoi(s);
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
    if (!m_bcp.Connect(addr, port)) {
        MPF_LOGE("MPFController::Run - BCP connect failed, aborting");
        return;
    }
    MPF_LOGD("MPFController::Run - connected, sending start handshake");
    BCPResponse resp = m_bcp.SendAndWait("vpcom_bridge", {{"subcommand", "start"}}, "vpcom_bridge_response");
    if (resp.command.empty()) {
        MPF_LOGE("MPFController::Run - handshake failed (empty response)");
    } else {
        MPF_LOGI("MPFController::Run - handshake OK");
    }
    m_recorder.StartSession();
}

void MPFController::Stop() {
    m_recorder.StopSession();
    m_bcp.Send("vpcom_bridge", {{"subcommand", "stop"}});
    m_bcp.Disconnect();
}

// ---------------------------------------------------------------------------
// DispatchToMPF
// ---------------------------------------------------------------------------

std::string MPFController::DispatchToMPF(const char* category,
                                         const std::string& subcommand,
                                         const std::map<std::string, std::string>& extraParams)
{
    std::map<std::string, std::string> params = extraParams;
    params["subcommand"] = subcommand;

    if (m_recorder.IsEnabled()) {
        m_recorder.Record({m_recorder.Now(), category, "vpx_to_mpf",
                           subcommand, ParamsToJson(extraParams), ""});
    }

    BCPResponse resp = m_bcp.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");

    std::string resultVal;
    if (!resp.command.empty()) {
        auto it = resp.params.find("result");
        if (it != resp.params.end()) resultVal = it->second;
    }

    if (m_recorder.IsEnabled()) {
        m_recorder.Record({m_recorder.Now(), category, "mpf_to_vpx",
                           subcommand, "", resultVal.empty() ? "" : resultVal});
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
    DispatchToMPF("input", "set_switch", {
        {"number", std::to_string(number)},
        {"value", value ? "bool:True" : "bool:False"}
    });
}

void MPFController::SetSwitch(const std::string& number, bool value) {
    DispatchToMPF("input", "set_switch", {
        {"number", number},
        {"value", value ? "bool:True" : "bool:False"}
    });
}

void MPFController::PulseSW(int number) {
    DispatchToMPF("input", "pulsesw", {{"number", std::to_string(number)}});
}

void MPFController::PulseSW(const std::string& number) {
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
