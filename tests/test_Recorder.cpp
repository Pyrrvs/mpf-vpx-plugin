#include "doctest.h"
#include "Recorder.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <regex>

using namespace MPF;

// ---------------------------------------------------------------------------
// FormatEvent pure function tests
// ---------------------------------------------------------------------------

TEST_CASE("FormatEvent: basic event without wall clock") {
    RecordEvent event;
    event.timestamp = 1.234567;
    event.category = "input";
    event.direction = "vpx_to_mpf";
    event.command = "switch_active";
    event.params = "";
    event.result = "";

    std::string json = Recorder::FormatEvent(event, false, "");

    CHECK(json.find("\"ts\":1.234567") != std::string::npos);
    CHECK(json.find("\"cat\":\"input\"") != std::string::npos);
    CHECK(json.find("\"dir\":\"vpx_to_mpf\"") != std::string::npos);
    CHECK(json.find("\"cmd\":\"switch_active\"") != std::string::npos);
    CHECK(json.find("\"wall\"") == std::string::npos);
    CHECK(json.find("\"result\"") == std::string::npos);
}

TEST_CASE("FormatEvent: first event includes wall clock") {
    RecordEvent event;
    event.timestamp = 0.0;
    event.category = "state";
    event.direction = "mpf_to_vpx";
    event.command = "session_start";
    event.params = "";
    event.result = "";

    std::string anchor = "2026-04-11T12:00:00.000000Z";
    std::string json = Recorder::FormatEvent(event, true, anchor);

    CHECK(json.find("\"wall\":\"2026-04-11T12:00:00.000000Z\"") != std::string::npos);
    CHECK(json.find("\"cat\":\"state\"") != std::string::npos);
}

TEST_CASE("FormatEvent: event with result only") {
    RecordEvent event;
    event.timestamp = 2.5;
    event.category = "query";
    event.direction = "mpf_to_vpx";
    event.command = "get_switch";
    event.params = "";
    event.result = "{\"value\":1}";

    std::string json = Recorder::FormatEvent(event, false, "");

    CHECK(json.find("\"result\":{\"value\":1}") != std::string::npos);
    CHECK(json.find("\"params\"") == std::string::npos);
}

TEST_CASE("FormatEvent: event with both params and result") {
    RecordEvent event;
    event.timestamp = 3.0;
    event.category = "query";
    event.direction = "vpx_to_mpf";
    event.command = "set_light";
    event.params = "{\"name\":\"l_shoot\"}";
    event.result = "{\"ok\":true}";

    std::string json = Recorder::FormatEvent(event, false, "");

    CHECK(json.find("\"params\":{\"name\":\"l_shoot\"}") != std::string::npos);
    CHECK(json.find("\"result\":{\"ok\":true}") != std::string::npos);
}

TEST_CASE("FormatEvent: command with quotes is escaped") {
    RecordEvent event;
    event.timestamp = 0.0;
    event.category = "input";
    event.direction = "vpx_to_mpf";
    event.command = "say \"hello\"";
    event.params = "";
    event.result = "";

    std::string json = Recorder::FormatEvent(event, false, "");

    CHECK(json.find("\"cmd\":\"say \\\"hello\\\"\"") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Full session tests (write to temp directory)
// ---------------------------------------------------------------------------

TEST_CASE("Recorder: disabled recorder does not create files") {
    std::string tmpDir = (std::filesystem::temp_directory_path() / "mpf_test_disabled").string();
    // Clean up first in case of previous failed run
    std::filesystem::remove_all(tmpDir);

    Recorder recorder;
    recorder.SetEnabled(false);
    recorder.SetOutputDirectory(tmpDir);

    recorder.StartSession();

    RecordEvent event;
    event.timestamp = 0.0;
    event.category = "input";
    event.direction = "vpx_to_mpf";
    event.command = "test";
    recorder.Record(event);

    recorder.StopSession();

    CHECK_FALSE(std::filesystem::exists(tmpDir));

    // Clean up
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("Recorder: enabled recorder creates JSONL file") {
    std::string tmpDir = (std::filesystem::temp_directory_path() / "mpf_test_enabled").string();
    std::filesystem::remove_all(tmpDir);

    Recorder recorder;
    recorder.SetEnabled(true);
    recorder.SetOutputDirectory(tmpDir);

    recorder.StartSession();

    RecordEvent event1;
    event1.timestamp = recorder.Now();
    event1.category = "input";
    event1.direction = "vpx_to_mpf";
    event1.command = "switch_1";
    event1.params = "{\"id\":1}";
    recorder.Record(event1);

    // Small delay to let writer process
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    RecordEvent event2;
    event2.timestamp = recorder.Now();
    event2.category = "state";
    event2.direction = "mpf_to_vpx";
    event2.command = "light_on";
    event2.result = "{\"ok\":true}";
    recorder.Record(event2);

    recorder.StopSession();

    // Find the JSONL file
    CHECK(std::filesystem::exists(tmpDir));

    std::string foundFile;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".jsonl") {
            foundFile = entry.path().string();
            break;
        }
    }
    REQUIRE_FALSE(foundFile.empty());

    // Read lines (in a scope so the ifstream closes before remove_all on Windows)
    std::vector<std::string> lines;
    {
        std::ifstream file(foundFile);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) lines.push_back(line);
        }
    }

    CHECK(lines.size() == 2);

    // First line should have "wall" field
    CHECK(lines[0].find("\"wall\"") != std::string::npos);

    // Second line should NOT have "wall" field
    CHECK(lines[1].find("\"wall\"") == std::string::npos);

    // Clean up
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("Recorder: Now() returns elapsed time") {
    Recorder recorder;
    recorder.SetEnabled(true);
    recorder.SetOutputDirectory(
        (std::filesystem::temp_directory_path() / "mpf_test_now").string());

    recorder.StartSession();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    double elapsed = recorder.Now();

    CHECK(elapsed > 0.04);
    CHECK(elapsed < 0.5);

    recorder.StopSession();

    // Clean up
    std::filesystem::remove_all(
        (std::filesystem::temp_directory_path() / "mpf_test_now").string());
}

TEST_CASE("Recorder: file name starts with date") {
    std::string tmpDir = (std::filesystem::temp_directory_path() / "mpf_test_filename").string();
    std::filesystem::remove_all(tmpDir);

    Recorder recorder;
    recorder.SetEnabled(true);
    recorder.SetOutputDirectory(tmpDir);

    recorder.StartSession();
    recorder.StopSession();

    // Find the JSONL file and check its name matches the expected pattern
    std::string foundName;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".jsonl") {
            foundName = entry.path().filename().string();
            break;
        }
    }
    REQUIRE_FALSE(foundName.empty());

    // Pattern: YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl
    std::regex pattern(R"(\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_mpf_recording\.jsonl)");
    CHECK(std::regex_match(foundName, pattern));

    // Clean up
    std::filesystem::remove_all(tmpDir);
}
