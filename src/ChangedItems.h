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

// JSON parsing helpers (use nlohmann/json internally via json.hpp already in src/)
std::vector<std::pair<std::string, bool>> ParseChangedList(const std::string& jsonStr);
std::vector<std::tuple<std::string, bool, float>> ParseChangedBrightnessList(const std::string& jsonStr);
std::vector<std::tuple<std::string, std::string, bool>> ParseHardwareRulesList(const std::string& jsonStr);

} // namespace MPF
