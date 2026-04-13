#include "ChangedItems.h"
#include "json.hpp"

namespace MPF {

// ---------------------------------------------------------------------------
// JSON helpers
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
// ChangedItems
// ---------------------------------------------------------------------------

ChangedItems::ChangedItems(std::vector<std::pair<std::string, bool>> items)
    : m_items(std::move(items))
{
}

ChangedItems::ChangedItems(std::vector<std::pair<std::string, bool>> items,
                           std::vector<float> brightness)
    : m_items(std::move(items))
    , m_brightness(std::move(brightness))
{
}

int ChangedItems::GetCount() const {
    return static_cast<int>(m_items.size());
}

std::string ChangedItems::GetId(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return "";
    return m_items[static_cast<size_t>(index)].first;
}

bool ChangedItems::GetState(int index) const {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return false;
    return m_items[static_cast<size_t>(index)].second;
}

float ChangedItems::GetBrightness(int index) const {
    if (index < 0 || index >= static_cast<int>(m_brightness.size())) return 0.0f;
    return m_brightness[static_cast<size_t>(index)];
}

// ---------------------------------------------------------------------------
// HardwareRuleItems
// ---------------------------------------------------------------------------

HardwareRuleItems::HardwareRuleItems(std::vector<std::tuple<std::string, std::string, bool>> rules)
    : m_rules(std::move(rules))
{
}

int HardwareRuleItems::GetCount() const {
    return static_cast<int>(m_rules.size());
}

std::string HardwareRuleItems::GetSwitch(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return "";
    return std::get<0>(m_rules[static_cast<size_t>(index)]);
}

std::string HardwareRuleItems::GetCoil(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return "";
    return std::get<1>(m_rules[static_cast<size_t>(index)]);
}

bool HardwareRuleItems::GetHold(int index) const {
    if (index < 0 || index >= static_cast<int>(m_rules.size())) return false;
    return std::get<2>(m_rules[static_cast<size_t>(index)]);
}

// ---------------------------------------------------------------------------
// ParseChangedList
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, bool>> ParseChangedList(const std::string& jsonStr) {
    std::vector<std::pair<std::string, bool>> result;

    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"" || jsonStr == "[]")
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
        return {};
    }

    return result;
}

// ---------------------------------------------------------------------------
// ParseChangedBrightnessList
// ---------------------------------------------------------------------------

std::vector<std::tuple<std::string, bool, float>> ParseChangedBrightnessList(const std::string& jsonStr) {
    std::vector<std::tuple<std::string, bool, float>> result;

    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"" || jsonStr == "[]")
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
    } catch (...) {
        return {};
    }

    return result;
}

// ---------------------------------------------------------------------------
// ParseHardwareRulesList
// ---------------------------------------------------------------------------

std::vector<std::tuple<std::string, std::string, bool>> ParseHardwareRulesList(const std::string& jsonStr) {
    std::vector<std::tuple<std::string, std::string, bool>> result;

    if (jsonStr.empty() || jsonStr == "false" || jsonStr == "\"false\"" || jsonStr == "[]")
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
    } catch (...) {
        return {};
    }

    return result;
}

} // namespace MPF
