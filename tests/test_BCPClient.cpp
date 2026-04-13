#include "doctest.h"
#include "BCPClient.h"
#include "MockBCPServer.h"

#include <thread>
#include <chrono>

using namespace MPF;

// ---------------------------------------------------------------------------
// Pure function tests (no sockets)
// ---------------------------------------------------------------------------

TEST_CASE("UrlEncode: alphanumeric passthrough") {
    CHECK(BCPClient::UrlEncode("hello123") == "hello123");
    CHECK(BCPClient::UrlEncode("ABC") == "ABC");
    CHECK(BCPClient::UrlEncode("test-value_here.v2~ok") == "test-value_here.v2~ok");
}

TEST_CASE("UrlEncode: special characters") {
    CHECK(BCPClient::UrlEncode("a b") == "a%20b");
    CHECK(BCPClient::UrlEncode("key:value") == "key%3Avalue");
    CHECK(BCPClient::UrlEncode("a&b") == "a%26b");
    CHECK(BCPClient::UrlEncode("100%") == "100%25");
}

TEST_CASE("UrlDecode: roundtrip") {
    std::string original = "hello world:foo&bar=baz";
    std::string encoded = BCPClient::UrlEncode(original);
    CHECK(BCPClient::UrlDecode(encoded) == original);
}

TEST_CASE("UrlDecode: percent-encoded colons") {
    CHECK(BCPClient::UrlDecode("int%3A5") == "int:5");
    CHECK(BCPClient::UrlDecode("no%20spaces%26stuff") == "no spaces&stuff");
}

TEST_CASE("EncodeCommand: no params") {
    std::map<std::string, std::string> empty;
    CHECK(BCPClient::EncodeCommand("hello", empty) == "hello");
}

TEST_CASE("EncodeCommand: simple params") {
    std::map<std::string, std::string> params = {{"subcommand", "start"}};
    CHECK(BCPClient::EncodeCommand("vpcom_bridge", params) == "vpcom_bridge?subcommand=start");
}

TEST_CASE("EncodeCommand: params with special chars") {
    std::map<std::string, std::string> params = {{"type", "int:5"}, {"name", "a b"}};
    std::string encoded = BCPClient::EncodeCommand("set", params);
    // Should contain percent-encoded values
    CHECK(encoded.find("int%3A5") != std::string::npos);
    CHECK(encoded.find("a%20b") != std::string::npos);
    // Should have command prefix
    CHECK(encoded.substr(0, 4) == "set?");
}

TEST_CASE("DecodeLine: command only") {
    BCPResponse resp = BCPClient::DecodeLine("hello");
    CHECK(resp.command == "hello");
    CHECK(resp.params.empty());
}

TEST_CASE("DecodeLine: command with params") {
    BCPResponse resp = BCPClient::DecodeLine("vpcom_bridge?subcommand=start&mode=fast");
    CHECK(resp.command == "vpcom_bridge");
    CHECK(resp.params.at("subcommand") == "start");
    CHECK(resp.params.at("mode") == "fast");
}

TEST_CASE("DecodeLine: params with percent-encoded values") {
    BCPResponse resp = BCPClient::DecodeLine("set?type=int%3A5&name=a%20b");
    CHECK(resp.command == "set");
    CHECK(resp.params.at("type") == "int:5");
    CHECK(resp.params.at("name") == "a b");
}

TEST_CASE("DecodeLine: roundtrip encode-decode") {
    std::map<std::string, std::string> params = {
        {"host", "localhost:5050"},
        {"path", "/a&b=c"},
        {"msg", "hello world"}
    };
    std::string encoded = BCPClient::EncodeCommand("test_cmd", params);
    BCPResponse resp = BCPClient::DecodeLine(encoded);
    CHECK(resp.command == "test_cmd");
    CHECK(resp.params.at("host") == "localhost:5050");
    CHECK(resp.params.at("path") == "/a&b=c");
    CHECK(resp.params.at("msg") == "hello world");
}

// ---------------------------------------------------------------------------
// Socket tests (with MockBCPServer)
// ---------------------------------------------------------------------------

TEST_CASE("Connect and disconnect") {
    MockBCPServer server;
    int port = server.Start([](const std::string&) { return std::string(); });
    REQUIRE(port > 0);

    BCPClient client;
    CHECK(client.Connect("127.0.0.1", port));
    CHECK(client.IsConnected());

    client.Disconnect();
    CHECK_FALSE(client.IsConnected());

    server.Stop();
}

TEST_CASE("Connect to invalid port fails") {
    BCPClient client;
    CHECK_FALSE(client.Connect("127.0.0.1", 1));
    CHECK_FALSE(client.IsConnected());
}

TEST_CASE("SendAndWait: receives matching response") {
    MockBCPServer server;
    int port = server.Start([](const std::string& line) -> std::string {
        // Echo back as a _response command
        BCPResponse req = BCPClient::DecodeLine(line);
        if (req.command == "vpcom_bridge") {
            return "vpcom_bridge_response?status=ok";
        }
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(3000);
    REQUIRE(client.Connect("127.0.0.1", port));

    std::map<std::string, std::string> params = {{"subcommand", "start"}};
    BCPResponse resp = client.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params.at("status") == "ok");

    client.Disconnect();
    server.Stop();
}

TEST_CASE("SendAndWait: skips non-matching lines") {
    MockBCPServer server;
    int port = server.Start([](const std::string& line) -> std::string {
        BCPResponse req = BCPClient::DecodeLine(line);
        if (req.command == "vpcom_bridge") {
            // Send noise first, then the real response, as one batch
            return "noise_event?foo=bar\nvpcom_bridge_response?result=success";
        }
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(3000);
    REQUIRE(client.Connect("127.0.0.1", port));

    std::map<std::string, std::string> params = {{"subcommand", "start"}};
    BCPResponse resp = client.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params.at("result") == "success");

    client.Disconnect();
    server.Stop();
}

TEST_CASE("SendAndWait: timeout returns empty response") {
    MockBCPServer server;
    int port = server.Start([](const std::string&) -> std::string {
        // Never respond
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(500); // Short timeout for test speed
    REQUIRE(client.Connect("127.0.0.1", port));

    std::map<std::string, std::string> params;
    BCPResponse resp = client.SendAndWait("hello", params, "hello_response");
    CHECK(resp.command.empty());
    CHECK(resp.params.empty());

    server.Stop();
}

TEST_CASE("Send: fire and forget does not block") {
    MockBCPServer server;
    int port = server.Start([](const std::string&) -> std::string {
        // Never respond
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    REQUIRE(client.Connect("127.0.0.1", port));

    auto start = std::chrono::steady_clock::now();
    std::map<std::string, std::string> params = {{"key", "value"}};
    client.Send("fire_and_forget", params);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Send should return almost immediately (well under 1 second)
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 1000);

    client.Disconnect();
    server.Stop();
}

TEST_CASE("DecodeLine: json= wrapper extracts inner keys") {
    std::string line = "vpcom_bridge_response?json=%7B%22result%22%3A%20%5B%5B%22l-1%22%2C%20true%5D%5D%7D";
    BCPResponse resp = BCPClient::DecodeLine(line);
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params.count("result") == 1);
    CHECK(resp.params["result"].find("l-1") != std::string::npos);
    CHECK(resp.params["result"].find("true") != std::string::npos);
}

TEST_CASE("DecodeLine: json= wrapper with multiple keys") {
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
