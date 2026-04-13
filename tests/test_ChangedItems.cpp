#include "doctest.h"
#include "ChangedItems.h"

using namespace MPF;

// ===========================================================================
// ChangedItems tests
// ===========================================================================

TEST_CASE("ChangedItems: Count returns number of items") {
    std::vector<std::pair<std::string, bool>> items = {{"a", true}, {"b", false}};
    ChangedItems ci(items);
    CHECK(ci.GetCount() == 2);
}

TEST_CASE("ChangedItems: Id returns string at index") {
    std::vector<std::pair<std::string, bool>> items = {{"led-1", true}, {"led-2", false}};
    ChangedItems ci(items);
    CHECK(ci.GetId(0) == "led-1");
    CHECK(ci.GetId(1) == "led-2");
}

TEST_CASE("ChangedItems: State returns bool at index") {
    std::vector<std::pair<std::string, bool>> items = {{"a", true}, {"b", false}};
    ChangedItems ci(items);
    CHECK(ci.GetState(0) == true);
    CHECK(ci.GetState(1) == false);
}

TEST_CASE("ChangedItems: Out of bounds returns defaults") {
    std::vector<std::pair<std::string, bool>> items = {{"x", true}};
    ChangedItems ci(items);
    CHECK(ci.GetId(5) == "");
    CHECK(ci.GetState(5) == false);
    CHECK(ci.GetBrightness(5) == 0.0f);
    CHECK(ci.GetId(-1) == "");
    CHECK(ci.GetState(-1) == false);
    CHECK(ci.GetBrightness(-1) == 0.0f);
}

TEST_CASE("ChangedItems: Brightness returns float when populated") {
    std::vector<std::pair<std::string, bool>> items = {{"led1", true}, {"led2", false}};
    std::vector<float> brightness = {0.8f, 0.2f};
    ChangedItems ci(items, brightness);
    CHECK(ci.GetBrightness(0) == doctest::Approx(0.8f));
    CHECK(ci.GetBrightness(1) == doctest::Approx(0.2f));
}

TEST_CASE("ChangedItems: Brightness returns 0 when not populated") {
    std::vector<std::pair<std::string, bool>> items = {{"a", true}};
    ChangedItems ci(items);
    CHECK(ci.GetBrightness(0) == 0.0f);
}

TEST_CASE("ChangedItems: Empty items gives count 0") {
    std::vector<std::pair<std::string, bool>> items;
    ChangedItems ci(items);
    CHECK(ci.GetCount() == 0);
}

// ===========================================================================
// HardwareRuleItems tests
// ===========================================================================

TEST_CASE("HardwareRuleItems: Count, Switch, Coil, Hold") {
    std::vector<std::tuple<std::string, std::string, bool>> rules = {
        {"sw1", "coil1", true},
        {"sw2", "coil2", false}
    };
    HardwareRuleItems hr(rules);
    CHECK(hr.GetCount() == 2);
    CHECK(hr.GetSwitch(0) == "sw1");
    CHECK(hr.GetCoil(0) == "coil1");
    CHECK(hr.GetHold(0) == true);
    CHECK(hr.GetSwitch(1) == "sw2");
    CHECK(hr.GetCoil(1) == "coil2");
    CHECK(hr.GetHold(1) == false);
}

TEST_CASE("HardwareRuleItems: Out of bounds returns defaults") {
    std::vector<std::tuple<std::string, std::string, bool>> rules = {{"sw1", "coil1", true}};
    HardwareRuleItems hr(rules);
    CHECK(hr.GetSwitch(5) == "");
    CHECK(hr.GetCoil(5) == "");
    CHECK(hr.GetHold(5) == false);
    CHECK(hr.GetSwitch(-1) == "");
    CHECK(hr.GetCoil(-1) == "");
    CHECK(hr.GetHold(-1) == false);
}

// ===========================================================================
// ParseChangedList tests
// ===========================================================================

TEST_CASE("ParseChangedList: String IDs with bool states") {
    auto result = ParseChangedList(R"([["l-1", true], ["l-2", false]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].first == "l-1");
    CHECK(result[0].second == true);
    CHECK(result[1].first == "l-2");
    CHECK(result[1].second == false);
}

TEST_CASE("ParseChangedList: Numeric IDs coerced to string") {
    auto result = ParseChangedList(R"([[42, true], [7, false]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].first == "42");
    CHECK(result[0].second == true);
    CHECK(result[1].first == "7");
    CHECK(result[1].second == false);
}

TEST_CASE("ParseChangedList: Int states coerced to bool") {
    auto result = ParseChangedList(R"([["a", 1], ["b", 0]])");
    REQUIRE(result.size() == 2);
    CHECK(result[0].second == true);
    CHECK(result[1].second == false);
}

TEST_CASE("ParseChangedList: Empty array returns empty") {
    auto result = ParseChangedList("[]");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: false sentinel returns empty") {
    auto result = ParseChangedList("false");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: quoted false sentinel returns empty") {
    auto result = ParseChangedList("\"false\"");
    CHECK(result.empty());
}

TEST_CASE("ParseChangedList: Malformed JSON returns empty") {
    auto result = ParseChangedList("{not valid json");
    CHECK(result.empty());
}

// ===========================================================================
// ParseChangedBrightnessList tests
// ===========================================================================

TEST_CASE("ParseChangedBrightnessList: Float brightness values") {
    auto result = ParseChangedBrightnessList(R"([["led1", 0.8], ["led2", 0.2]])");
    REQUIRE(result.size() == 2);
    CHECK(std::get<0>(result[0]) == "led1");
    CHECK(std::get<1>(result[0]) == true);   // 0.8 > 0.5
    CHECK(std::get<2>(result[0]) == doctest::Approx(0.8f));
    CHECK(std::get<0>(result[1]) == "led2");
    CHECK(std::get<1>(result[1]) == false);  // 0.2 <= 0.5
    CHECK(std::get<2>(result[1]) == doctest::Approx(0.2f));
}

TEST_CASE("ParseChangedBrightnessList: Empty and false return empty") {
    CHECK(ParseChangedBrightnessList("").empty());
    CHECK(ParseChangedBrightnessList("false").empty());
}

// ===========================================================================
// ParseHardwareRulesList tests
// ===========================================================================

TEST_CASE("ParseHardwareRulesList: 3-tuples parsed correctly") {
    auto result = ParseHardwareRulesList(R"([["sw1", "coil1", true], ["sw2", "coil2", false]])");
    REQUIRE(result.size() == 2);
    CHECK(std::get<0>(result[0]) == "sw1");
    CHECK(std::get<1>(result[0]) == "coil1");
    CHECK(std::get<2>(result[0]) == true);
    CHECK(std::get<0>(result[1]) == "sw2");
    CHECK(std::get<1>(result[1]) == "coil2");
    CHECK(std::get<2>(result[1]) == false);
}

TEST_CASE("ParseHardwareRulesList: Numeric IDs coerced") {
    auto result = ParseHardwareRulesList(R"([[10, 20, true]])");
    REQUIRE(result.size() == 1);
    CHECK(std::get<0>(result[0]) == "10");
    CHECK(std::get<1>(result[0]) == "20");
    CHECK(std::get<2>(result[0]) == true);
}

TEST_CASE("ParseHardwareRulesList: Empty and false return empty") {
    CHECK(ParseHardwareRulesList("").empty());
    CHECK(ParseHardwareRulesList("false").empty());
}
