#include "doctest.h"
#include "MPFController.h"
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
            return "vpcom_bridge_response?result=%5B%5B%220%22%2Ctrue%5D%5D";
        if (sub == "changed_lamps" || sub == "changed_gi_strings" ||
            sub == "changed_leds" || sub == "changed_brightness_leds" ||
            sub == "changed_flashers" || sub == "get_hardwarerules")
            return "vpcom_bridge_response?result=false";
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

TEST_CASE("MPFController: ChangedSolenoids returns result string") {
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    std::string result = ctrl.GetChangedSolenoids();
    CHECK_FALSE(result.empty());
    // The mock returns URL-decoded: [["0",true]]
    CHECK(result.find("[") != std::string::npos);

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
