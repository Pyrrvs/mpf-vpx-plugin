#include "doctest.h"
#include "MPFController.h"
#include "ChangedItems.h"
#include "MockBCPServer.h"

#include <thread>
#include <chrono>
#include <filesystem>
#include <string>

using namespace MPF;

// ---------------------------------------------------------------------------
// Helper: mock handler that returns canned responses per subcommand
// ---------------------------------------------------------------------------

static MockHandler MakeMPFHandler() {
    return [](const std::string& line) -> std::string {
        BCPResponse req = BCPClient::DecodeLine(line);
        if (req.command != "vpcom_bridge") return "";

        auto it = req.params.find("subcommand");
        if (it == req.params.end()) {
            return "vpcom_bridge_response?error=missing_subcommand";
        }
        const std::string& sub = it->second;

        if (sub == "start" || sub == "stop")
            return "vpcom_bridge_response?result=ok";
        if (sub == "switch" || sub == "get_switch")
            return "vpcom_bridge_response?result=True";
        if (sub == "set_switch" || sub == "pulsesw")
            return "vpcom_bridge_response?result=ok";
        if (sub == "mech" || sub == "get_mech")
            return "vpcom_bridge_response?result=42";
        if (sub == "set_mech")
            return "vpcom_bridge_response?result=ok";
        if (sub == "changed_solenoids")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["coil1", true], ["coil2", false]]})");
        if (sub == "changed_lamps")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["l-1", true], ["l-2", false]]})");
        if (sub == "changed_gi_strings")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["gi1", true]]})");
        if (sub == "changed_leds")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["led1", true]]})");
        if (sub == "changed_brightness_leds")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["led1", 0.8], ["led2", 0.2]]})");
        if (sub == "changed_flashers")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["f1", true]]})");
        if (sub == "get_hardwarerules")
            return "vpcom_bridge_response?json=" + BCPClient::UrlEncode(R"({"result": [["sw1", "coil1", true]]})");

        if (sub == "get_coilactive")
            return "vpcom_bridge_response?result=True";

        return "vpcom_bridge_response?error=unknown_subcommand";
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("MPFController: Run and Stop lifecycle") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);
    ctrl.Stop();

    server.Stop();
    // If we get here without crash, the test passes.
    CHECK(true);
}

TEST_CASE("MPFController: GetSwitch returns true") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.GetSwitch(1) == true);
    CHECK(ctrl.GetSwitch(std::string("s_trough1")) == true);

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: SetSwitch and PulseSW do not crash") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    ctrl.SetSwitch(5, true);
    ctrl.SetSwitch(std::string("s_start"), false);
    ctrl.PulseSW(10);
    ctrl.PulseSW(std::string("s_bumper"));

    ctrl.Stop();
    server.Stop();
    CHECK(true);
}

TEST_CASE("MPFController: Mech access") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.ReadMech(1) == 42);
    CHECK(ctrl.GetMech(2) == 42);
    ctrl.SetMech(1, 100); // should not crash

    ctrl.Stop();
    server.Stop();
}

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
    CHECK(result->GetState(0) == true);
    CHECK(result->GetBrightness(1) == doctest::Approx(0.2f));
    CHECK(result->GetState(1) == false);
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

TEST_CASE("MPFController: IsCoilActive") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.IsCoilActive(0) == true);
    CHECK(ctrl.IsCoilActive(std::string("c_flipper")) == true);

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: stub properties") {
    MPFController ctrl(false, "");

    CHECK(ctrl.GetVersion() == "1.0.0");

    CHECK(ctrl.GetGameName() == "Game");
    ctrl.SetGameName("TestGame");
    CHECK(ctrl.GetGameName() == "TestGame");

    CHECK(ctrl.GetHandleMechanics() == true);
    ctrl.SetHandleMechanics(false);
    CHECK(ctrl.GetHandleMechanics() == false);

    CHECK(ctrl.GetPause() == false);
    ctrl.SetPause(true);
    CHECK(ctrl.GetPause() == true);

    CHECK(ctrl.GetShowTitle() == false);
    ctrl.SetShowTitle(true);
    CHECK(ctrl.GetShowTitle() == true);

    CHECK(ctrl.GetSplashInfoLine().empty());
    ctrl.SetSplashInfoLine("Hello");
    CHECK(ctrl.GetSplashInfoLine() == "Hello");

    // Read-only stubs
    CHECK(ctrl.GetShowFrame() == false);
    CHECK(ctrl.GetShowDMDOnly() == false);
    CHECK(ctrl.GetHandleKeyboard() == false);
    CHECK(ctrl.GetDIP() == false);
}

TEST_CASE("MPFController: recording creates file during session") {
    std::string tmpDir = (std::filesystem::temp_directory_path() / "mpf_test_controller_rec").string();
    std::filesystem::remove_all(tmpDir);

    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    {
        MPFController ctrl(true, tmpDir);
        ctrl.Run("127.0.0.1", port);

        ctrl.SetSwitch(1, true);
        ctrl.GetSwitch(1);

        // Small delay to let recorder process events
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        ctrl.Stop();
    }

    server.Stop();

    // Verify recording file exists
    CHECK(std::filesystem::exists(tmpDir));

    bool foundJsonl = false;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".jsonl") {
            foundJsonl = true;
            break;
        }
    }
    CHECK(foundJsonl);

    // Clean up
    std::filesystem::remove_all(tmpDir);
}
