# mpf-vpx-plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a cross-platform VPX plugin that bridges Visual Pinball X to MPF via BCP, with game session recording.

**Architecture:** Four components with clean boundaries: BCPClient (TCP socket + BCP wire protocol), Recorder (lock-free queue + background JSONL writer), MPFController (scriptable VBScript surface), MPFPlugin (entry point + wiring). The plugin registers as `MPF.Controller` via VPX's `SetCOMObjectOverride` so existing table scripts work unchanged.

**Tech Stack:** C++20, CMake 3.20+, VPX plugin SDK (fetched via FetchContent), raw platform sockets (POSIX/Winsock), doctest (header-only test framework)

**Spec:** `docs/superpowers/specs/2026-04-12-mpf-vpx-plugin-design.md`

**BCP wire format reference:** `command?key=value&key=value\n` over TCP. Values are type-prefixed (`int:5`, `bool:True`, plain strings) and URL-percent-encoded (colons become `%3A`). Example: `vpcom_bridge?subcommand=set_switch&number=int%3A5&value=bool%3ATrue\n`. Responses are the same format — read lines until one starts with the expected response command name.

**Test approach:** TDD with doctest. Tests are written before implementation. A `MockBCPServer` (small TCP listener thread) is used for socket-level and integration tests. Run tests via `ctest --test-dir build` or `build/mpf-vpx-tests`.

---

### Task 1: Repository scaffold + test infrastructure

**Files:**
- Create: `.gitignore`
- Create: `plugin.cfg`
- Create: `CMakeLists.txt`
- Create: `src/MPFPlugin.cpp` (minimal stub)
- Create: `tests/doctest.h` (vendored from https://github.com/doctest/doctest)
- Create: `tests/test_main.cpp`

This task sets up the build system so that both the plugin and test binary compile. The stub plugin exports the required Load/Unload symbols but does nothing. The test binary runs zero tests but proves the test framework works.

- [ ] **Step 1: Create .gitignore**

```gitignore
# Build
build/
dist/
CMakeCache.txt
CMakeFiles/

# Binaries
*.dylib
*.so
*.dll
*.exe
*.o
*.obj

# IDE
.vscode/
.idea/
*.swp
*.swo
*~
.cache/

# OS
.DS_Store
Thumbs.db

# FetchContent downloads
_deps/
```

- [ ] **Step 2: Create plugin.cfg**

```ini
[configuration]
id = "MPF"
name = "MPF Bridge"
description = "Mission Pinball Framework controller bridge over BCP"
author = "Pyrrvs"
version = "0.1.0"
link = "https://github.com/Pyrrvs/mpf-vpx-plugin"

[libraries]
windows.x86 = "plugin-mpf.dll"
windows.x64 = "plugin-mpf64.dll"
linux.x64 = "plugin-mpf.so"
linux.aarch64 = "plugin-mpf.so"
macos.x64 = "plugin-mpf.dylib"
macos.arm64 = "plugin-mpf.dylib"
```

- [ ] **Step 3: Create CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20)
project(mpf-vpx-plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VPX SDK headers via FetchContent
set(VPX_TAG "v10.8.0-2051-28dd6c3" CACHE STRING "VPX git tag to fetch SDK headers from")

include(FetchContent)
FetchContent_Declare(vpx-sdk
    GIT_REPOSITORY https://github.com/vpinball/vpinball.git
    GIT_TAG ${VPX_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vpx-sdk)

# --- Plugin shared library ---
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
)

target_include_directories(mpf-vpx-plugin PRIVATE
    ${vpx-sdk_SOURCE_DIR}/plugins
    ${CMAKE_SOURCE_DIR}/src
)

# Platform-specific output naming and linking
if(WIN32)
    target_link_libraries(mpf-vpx-plugin PRIVATE ws2_32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set_target_properties(mpf-vpx-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "plugin-mpf64"
        )
    else()
        set_target_properties(mpf-vpx-plugin PROPERTIES
            PREFIX ""
            OUTPUT_NAME "plugin-mpf"
        )
    endif()
elseif(APPLE)
    set_target_properties(mpf-vpx-plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "plugin-mpf"
        SUFFIX ".dylib"
    )
else()
    set_target_properties(mpf-vpx-plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "plugin-mpf"
    )
endif()

# Install target: copies plugin.cfg + binary into dist/mpf/
set(DIST_DIR "${CMAKE_BINARY_DIR}/dist/mpf")
add_custom_command(TARGET mpf-vpx-plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DIST_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/plugin.cfg" "${DIST_DIR}/plugin.cfg"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:mpf-vpx-plugin>" "${DIST_DIR}"
)

# --- Test executable ---
enable_testing()

add_executable(mpf-vpx-tests
    tests/test_main.cpp
)

target_include_directories(mpf-vpx-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/tests
    ${CMAKE_SOURCE_DIR}/src
    ${vpx-sdk_SOURCE_DIR}/plugins
)

if(WIN32)
    target_link_libraries(mpf-vpx-tests PRIVATE ws2_32)
endif()

add_test(NAME mpf-vpx-tests COMMAND mpf-vpx-tests)
```

- [ ] **Step 4: Download doctest.h into tests/**

```bash
curl -sL https://raw.githubusercontent.com/doctest/doctest/v2.4.11/doctest/doctest.h -o tests/doctest.h
```

Verify the file is ~6000+ lines (it's a single-header framework).

- [ ] **Step 5: Create tests/test_main.cpp**

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

// Test source files are added via CMakeLists.txt.
// This file only provides the main() entry point for doctest.
```

- [ ] **Step 6: Create minimal stub src/MPFPlugin.cpp**

```cpp
#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"

MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    (void)sessionId;
    (void)api;
}

MSGPI_EXPORT void MSGPIAPI MPFPluginUnload()
{
}
```

- [ ] **Step 7: Build and run tests to verify scaffold**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Plugin builds. Test binary builds and runs with `0 test cases` (doctest reports `[doctest] Status: SUCCESS!`).

- [ ] **Step 8: Commit**

```bash
git add .gitignore plugin.cfg CMakeLists.txt src/MPFPlugin.cpp tests/doctest.h tests/test_main.cpp
git commit -m "Scaffold: CMake build, VPX SDK FetchContent, doctest test infrastructure"
```

---

### Task 2: BCPClient — tests first

**Files:**
- Create: `src/BCPClient.h`
- Create: `tests/test_BCPClient.cpp`
- Create: `tests/MockBCPServer.h`
- Modify: `CMakeLists.txt` (add test source)

Write tests for BCPClient before the implementation. This task creates the header (interface), a mock BCP server, and all test cases. The tests will fail to link until Task 3 provides the implementation.

- [ ] **Step 1: Create src/BCPClient.h**

```cpp
#pragma once

#include <string>
#include <map>
#include <cstdint>

namespace MPF {

struct BCPResponse {
    std::string command;
    std::map<std::string, std::string> params;
};

class BCPClient {
public:
    BCPClient();
    ~BCPClient();

    BCPClient(const BCPClient&) = delete;
    BCPClient& operator=(const BCPClient&) = delete;

    bool Connect(const std::string& host, int port);
    void Disconnect();
    bool IsConnected() const;

    // Send a command and block until waitForCommand arrives in response.
    BCPResponse SendAndWait(const std::string& command,
                            const std::map<std::string, std::string>& params,
                            const std::string& waitForCommand);

    // Fire-and-forget send.
    void Send(const std::string& command,
              const std::map<std::string, std::string>& params);

    void SetTimeout(int timeoutMs) { m_timeoutMs = timeoutMs; }

    // Exposed for testing — these are pure functions.
    static std::string EncodeCommand(const std::string& command,
                                     const std::map<std::string, std::string>& params);
    static BCPResponse DecodeLine(const std::string& line);
    static std::string UrlEncode(const std::string& value);
    static std::string UrlDecode(const std::string& value);

private:
    bool SocketConnect(const std::string& host, int port);
    void SocketClose();
    bool SocketSendLine(const std::string& line);
    bool SocketReadLine(std::string& out);

#ifdef _WIN32
    unsigned long long m_socket;
#else
    int m_socket;
#endif
    bool m_connected;
    int m_timeoutMs;
    std::string m_readBuffer;
};

} // namespace MPF
```

- [ ] **Step 2: Create tests/MockBCPServer.h**

A minimal TCP server that listens on a random port, accepts one connection, reads BCP lines, and responds with canned responses. Used by BCPClient and MPFController tests.

```cpp
#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <atomic>
#include <cstring>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using mock_socket_t = SOCKET;
    #define MOCK_INVALID_SOCK INVALID_SOCKET
    #define MOCK_CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    using mock_socket_t = int;
    #define MOCK_INVALID_SOCK (-1)
    #define MOCK_CLOSE_SOCKET ::close
#endif

// Callback: receives the raw BCP line from client, returns the raw BCP line to send back.
// Return empty string to send nothing.
using MockHandler = std::function<std::string(const std::string& line)>;

class MockBCPServer {
public:
    MockBCPServer()
        : m_listenSock(MOCK_INVALID_SOCK)
        , m_clientSock(MOCK_INVALID_SOCK)
        , m_port(0)
        , m_running(false)
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~MockBCPServer() { Stop(); }

    // Start listening. Returns the port number.
    int Start(MockHandler handler)
    {
        m_handler = handler;

        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == MOCK_INVALID_SOCK) return 0;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // OS picks a free port

        if (bind(m_listenSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            MOCK_CLOSE_SOCKET(m_listenSock);
            m_listenSock = MOCK_INVALID_SOCK;
            return 0;
        }

        socklen_t addrLen = sizeof(addr);
        getsockname(m_listenSock, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
        m_port = ntohs(addr.sin_port);

        listen(m_listenSock, 1);

        m_running.store(true);
        m_thread = std::thread(&MockBCPServer::ServerLoop, this);
        return m_port;
    }

    void Stop()
    {
        m_running.store(false);
        if (m_listenSock != MOCK_INVALID_SOCK) {
            MOCK_CLOSE_SOCKET(m_listenSock);
            m_listenSock = MOCK_INVALID_SOCK;
        }
        if (m_clientSock != MOCK_INVALID_SOCK) {
            MOCK_CLOSE_SOCKET(m_clientSock);
            m_clientSock = MOCK_INVALID_SOCK;
        }
        if (m_thread.joinable())
            m_thread.join();
    }

    int Port() const { return m_port; }

private:
    void ServerLoop()
    {
        m_clientSock = accept(m_listenSock, nullptr, nullptr);
        if (m_clientSock == MOCK_INVALID_SOCK) return;

        std::string buffer;
        char buf[4096];

        while (m_running.load()) {
            auto n = recv(m_clientSock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            buffer.append(buf, static_cast<size_t>(n));

            // Process complete lines
            size_t nlpos;
            while ((nlpos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nlpos);
                buffer.erase(0, nlpos + 1);

                std::string response = m_handler(line);
                if (!response.empty()) {
                    response += '\n';
                    send(m_clientSock, response.c_str(),
                         static_cast<int>(response.size()), 0);
                }
            }
        }
    }

    mock_socket_t m_listenSock;
    mock_socket_t m_clientSock;
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;
    MockHandler m_handler;
};
```

- [ ] **Step 3: Create tests/test_BCPClient.cpp**

```cpp
#include "doctest.h"
#include "BCPClient.h"
#include "MockBCPServer.h"

using namespace MPF;

// -----------------------------------------------------------------------
// Pure function tests — no sockets needed
// -----------------------------------------------------------------------

TEST_CASE("UrlEncode: alphanumeric passthrough")
{
    CHECK(BCPClient::UrlEncode("hello123") == "hello123");
}

TEST_CASE("UrlEncode: special characters")
{
    CHECK(BCPClient::UrlEncode("a b") == "a%20b");
    CHECK(BCPClient::UrlEncode("key:value") == "key%3Avalue");
    CHECK(BCPClient::UrlEncode("a&b=c") == "a%26b%3Dc");
}

TEST_CASE("UrlDecode: roundtrip")
{
    std::string original = "int:5 bool:True hello world&foo=bar";
    CHECK(BCPClient::UrlDecode(BCPClient::UrlEncode(original)) == original);
}

TEST_CASE("UrlDecode: percent-encoded colons")
{
    CHECK(BCPClient::UrlDecode("int%3A5") == "int:5");
    CHECK(BCPClient::UrlDecode("bool%3ATrue") == "bool:True");
}

TEST_CASE("EncodeCommand: no params")
{
    CHECK(BCPClient::EncodeCommand("hello", {}) == "hello");
}

TEST_CASE("EncodeCommand: simple params")
{
    std::map<std::string, std::string> params = {{"subcommand", "start"}};
    CHECK(BCPClient::EncodeCommand("vpcom_bridge", params) == "vpcom_bridge?subcommand=start");
}

TEST_CASE("EncodeCommand: params with special chars")
{
    std::map<std::string, std::string> params = {
        {"subcommand", "set_switch"},
        {"number", "int:5"},
        {"value", "bool:True"}
    };
    std::string encoded = BCPClient::EncodeCommand("vpcom_bridge", params);
    // Params are ordered by map (alphabetical), so: number, subcommand, value
    CHECK(encoded == "vpcom_bridge?number=int%3A5&subcommand=set_switch&value=bool%3ATrue");
}

TEST_CASE("DecodeLine: command only")
{
    BCPResponse resp = BCPClient::DecodeLine("hello");
    CHECK(resp.command == "hello");
    CHECK(resp.params.empty());
}

TEST_CASE("DecodeLine: command with params")
{
    BCPResponse resp = BCPClient::DecodeLine("vpcom_bridge_response?result=true&subcommand=switch");
    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params["result"] == "true");
    CHECK(resp.params["subcommand"] == "switch");
}

TEST_CASE("DecodeLine: params with percent-encoded values")
{
    BCPResponse resp = BCPClient::DecodeLine("cmd?value=int%3A42");
    CHECK(resp.params["value"] == "int:42");
}

TEST_CASE("DecodeLine: roundtrip encode-decode")
{
    std::map<std::string, std::string> params = {
        {"subcommand", "set_switch"},
        {"number", "int:5"},
        {"value", "bool:True"}
    };
    std::string encoded = BCPClient::EncodeCommand("vpcom_bridge", params);
    BCPResponse decoded = BCPClient::DecodeLine(encoded);
    CHECK(decoded.command == "vpcom_bridge");
    CHECK(decoded.params["subcommand"] == "set_switch");
    CHECK(decoded.params["number"] == "int:5");
    CHECK(decoded.params["value"] == "bool:True");
}

// -----------------------------------------------------------------------
// Socket-level tests with MockBCPServer
// -----------------------------------------------------------------------

TEST_CASE("Connect and disconnect")
{
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

TEST_CASE("Connect to invalid port fails")
{
    BCPClient client;
    CHECK_FALSE(client.Connect("127.0.0.1", 1)); // port 1 should not be listening
    CHECK_FALSE(client.IsConnected());
}

TEST_CASE("SendAndWait: receives matching response")
{
    MockBCPServer server;
    int port = server.Start([](const std::string& line) -> std::string {
        // Echo back as vpcom_bridge_response with result=ok
        if (line.find("vpcom_bridge") != std::string::npos)
            return "vpcom_bridge_response?result=ok";
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(2000);
    REQUIRE(client.Connect("127.0.0.1", port));

    BCPResponse resp = client.SendAndWait(
        "vpcom_bridge",
        {{"subcommand", "start"}},
        "vpcom_bridge_response");

    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params["result"] == "ok");

    client.Disconnect();
    server.Stop();
}

TEST_CASE("SendAndWait: skips non-matching lines")
{
    MockBCPServer server;
    int port = server.Start([](const std::string& line) -> std::string {
        if (line.find("vpcom_bridge") != std::string::npos)
            return "noise?foo=bar\nvpcom_bridge_response?result=found";
        return "";
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(2000);
    REQUIRE(client.Connect("127.0.0.1", port));

    BCPResponse resp = client.SendAndWait(
        "vpcom_bridge",
        {{"subcommand", "test"}},
        "vpcom_bridge_response");

    CHECK(resp.command == "vpcom_bridge_response");
    CHECK(resp.params["result"] == "found");

    client.Disconnect();
    server.Stop();
}

TEST_CASE("SendAndWait: timeout returns empty response")
{
    MockBCPServer server;
    int port = server.Start([](const std::string&) -> std::string {
        return ""; // never respond
    });
    REQUIRE(port > 0);

    BCPClient client;
    client.SetTimeout(500); // short timeout
    REQUIRE(client.Connect("127.0.0.1", port));

    BCPResponse resp = client.SendAndWait(
        "vpcom_bridge",
        {{"subcommand", "start"}},
        "vpcom_bridge_response");

    CHECK(resp.command.empty()); // timeout
    server.Stop();
}

TEST_CASE("Send: fire and forget does not block")
{
    MockBCPServer server;
    int port = server.Start([](const std::string&) -> std::string {
        return ""; // never respond
    });
    REQUIRE(port > 0);

    BCPClient client;
    REQUIRE(client.Connect("127.0.0.1", port));

    // Should return immediately without blocking
    client.Send("vpcom_bridge", {{"subcommand", "stop"}});
    CHECK(client.IsConnected());

    client.Disconnect();
    server.Stop();
}
```

- [ ] **Step 4: Add test_BCPClient.cpp to CMakeLists.txt**

Update the test executable in `CMakeLists.txt`:

```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
)
```

- [ ] **Step 5: Build — tests should fail to link (no BCPClient.cpp yet)**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Linker errors — `undefined reference to MPF::BCPClient::*`. This confirms the tests reference the correct symbols.

- [ ] **Step 6: Commit**

```bash
git add src/BCPClient.h tests/MockBCPServer.h tests/test_BCPClient.cpp CMakeLists.txt
git commit -m "Add BCPClient tests and MockBCPServer (red: tests fail to link)"
```

---

### Task 3: BCPClient — implementation (make tests green)

**Files:**
- Create: `src/BCPClient.cpp`
- Modify: `CMakeLists.txt` (add source to both plugin and test targets)

- [ ] **Step 1: Create src/BCPClient.cpp**

```cpp
#include "BCPClient.h"

#include <cstring>
#include <sstream>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
    using socket_t = SOCKET;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <poll.h>
    #include <cerrno>
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCKET ::close
    using socket_t = int;
#endif

namespace MPF {

// ---------------------------------------------------------------------------
// BCP wire format helpers
// ---------------------------------------------------------------------------

std::string BCPClient::UrlEncode(const std::string& value)
{
    std::ostringstream out;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out << buf;
        }
    }
    return out.str();
}

std::string BCPClient::UrlDecode(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '%' && i + 2 < value.size()) {
            int hi = 0, lo = 0;
            if (std::sscanf(value.c_str() + i + 1, "%1x%1x", &hi, &lo) == 2) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += value[i];
    }
    return out;
}

std::string BCPClient::EncodeCommand(const std::string& command,
                                     const std::map<std::string, std::string>& params)
{
    std::string line = command;
    if (!params.empty()) {
        line += '?';
        bool first = true;
        for (const auto& [key, val] : params) {
            if (!first) line += '&';
            first = false;
            line += UrlEncode(key);
            line += '=';
            line += UrlEncode(val);
        }
    }
    return line;
}

BCPResponse BCPClient::DecodeLine(const std::string& line)
{
    BCPResponse resp;
    auto qpos = line.find('?');
    if (qpos == std::string::npos) {
        resp.command = line;
        return resp;
    }
    resp.command = line.substr(0, qpos);
    std::string query = line.substr(qpos + 1);

    size_t start = 0;
    while (start < query.size()) {
        auto amp = query.find('&', start);
        std::string pair = (amp == std::string::npos)
            ? query.substr(start)
            : query.substr(start, amp - start);
        start = (amp == std::string::npos) ? query.size() : amp + 1;

        auto eq = pair.find('=');
        if (eq == std::string::npos) continue;
        std::string key = UrlDecode(pair.substr(0, eq));
        std::string val = UrlDecode(pair.substr(eq + 1));
        resp.params[key] = val;
    }
    return resp;
}

// ---------------------------------------------------------------------------
// Platform socket
// ---------------------------------------------------------------------------

BCPClient::BCPClient()
    : m_socket(INVALID_SOCK)
    , m_connected(false)
    , m_timeoutMs(5000)
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

BCPClient::~BCPClient()
{
    Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool BCPClient::SocketConnect(const std::string& host, int port)
{
    struct addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0)
        return false;

    for (auto* rp = result; rp != nullptr; rp = rp->ai_next) {
        m_socket = static_cast<socket_t>(
            socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol));
        if (m_socket == INVALID_SOCK) continue;
        if (connect(m_socket, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            freeaddrinfo(result);
            return true;
        }
        CLOSE_SOCKET(m_socket);
        m_socket = INVALID_SOCK;
    }
    freeaddrinfo(result);
    return false;
}

void BCPClient::SocketClose()
{
    if (m_socket != INVALID_SOCK) {
        CLOSE_SOCKET(m_socket);
        m_socket = INVALID_SOCK;
    }
}

bool BCPClient::SocketSendLine(const std::string& line)
{
    std::string data = line + '\n';
    const char* ptr = data.c_str();
    size_t remaining = data.size();
    while (remaining > 0) {
        auto sent = ::send(static_cast<socket_t>(m_socket), ptr,
                           static_cast<int>(remaining), 0);
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

bool BCPClient::SocketReadLine(std::string& out)
{
    while (true) {
        auto nlpos = m_readBuffer.find('\n');
        if (nlpos != std::string::npos) {
            out = m_readBuffer.substr(0, nlpos);
            m_readBuffer.erase(0, nlpos + 1);
            return true;
        }

#ifdef _WIN32
        WSAPOLLFD pfd;
        pfd.fd = static_cast<SOCKET>(m_socket);
        pfd.events = POLLIN;
        int pollResult = WSAPoll(&pfd, 1, m_timeoutMs);
#else
        struct pollfd pfd;
        pfd.fd = m_socket;
        pfd.events = POLLIN;
        int pollResult = poll(&pfd, 1, m_timeoutMs);
#endif
        if (pollResult <= 0) return false;

        char buf[4096];
        auto n = ::recv(static_cast<socket_t>(m_socket), buf, sizeof(buf), 0);
        if (n <= 0) return false;
        m_readBuffer.append(buf, static_cast<size_t>(n));
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool BCPClient::Connect(const std::string& host, int port)
{
    if (m_connected) Disconnect();
    m_readBuffer.clear();
    if (!SocketConnect(host, port)) return false;
    m_connected = true;
    return true;
}

void BCPClient::Disconnect()
{
    if (!m_connected) return;
    SocketClose();
    m_connected = false;
    m_readBuffer.clear();
}

bool BCPClient::IsConnected() const
{
    return m_connected;
}

void BCPClient::Send(const std::string& command,
                     const std::map<std::string, std::string>& params)
{
    if (!m_connected) return;
    SocketSendLine(EncodeCommand(command, params));
}

BCPResponse BCPClient::SendAndWait(const std::string& command,
                                   const std::map<std::string, std::string>& params,
                                   const std::string& waitForCommand)
{
    BCPResponse empty;
    if (!m_connected) return empty;
    if (!SocketSendLine(EncodeCommand(command, params))) {
        Disconnect();
        return empty;
    }

    std::string line;
    while (SocketReadLine(line)) {
        BCPResponse resp = DecodeLine(line);
        if (resp.command == waitForCommand)
            return resp;
    }

    Disconnect();
    return empty;
}

} // namespace MPF
```

- [ ] **Step 2: Add BCPClient.cpp to both targets in CMakeLists.txt**

Plugin target:
```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
)
```

Test target:
```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
    src/BCPClient.cpp
)
```

- [ ] **Step 3: Build and run tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All BCPClient tests pass (green).

- [ ] **Step 4: Commit**

```bash
git add src/BCPClient.cpp CMakeLists.txt
git commit -m "Implement BCPClient: all tests green"
```

---

### Task 4: Recorder — tests first

**Files:**
- Create: `src/Recorder.h`
- Create: `tests/test_Recorder.cpp`
- Modify: `CMakeLists.txt` (add test source)

- [ ] **Step 1: Create src/Recorder.h**

```cpp
#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <chrono>
#include <array>

namespace MPF {

struct RecordEvent {
    double timestamp;       // seconds relative to session start (steady_clock)
    const char* category;   // "input", "state", "query" — static string, not owned
    const char* direction;  // "vpx_to_mpf", "mpf_to_vpx" — static string, not owned
    std::string command;
    std::string params;     // pre-serialized JSON
    std::string result;     // pre-serialized JSON
};

class Recorder {
public:
    Recorder();
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }
    void SetOutputDirectory(const std::string& path) { m_outputDir = path; }

    void StartSession();
    void StopSession();

    // Non-blocking. Pushes event to the ring buffer. Drops if buffer is full.
    void Record(RecordEvent event);

    // Returns the current session elapsed time in seconds.
    double Now() const;

    // Exposed for testing.
    static std::string FormatEvent(const RecordEvent& event,
                                   bool includeWallClock,
                                   const std::string& wallClockAnchor);

private:
    void WriterThread();
    std::string GenerateFilename() const;

    bool m_enabled = false;
    std::string m_outputDir;
    bool m_sessionActive = false;
    std::chrono::steady_clock::time_point m_sessionStart;
    std::string m_wallClockAnchor;

    // SPSC ring buffer
    static constexpr size_t kRingSize = 8192;
    std::array<RecordEvent, kRingSize> m_ring;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};

    // Writer thread
    std::thread m_writerThread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_writerRunning{false};
    std::ofstream m_file;
};

} // namespace MPF
```

- [ ] **Step 2: Create tests/test_Recorder.cpp**

```cpp
#include "doctest.h"
#include "Recorder.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <chrono>

using namespace MPF;

// -----------------------------------------------------------------------
// FormatEvent — pure function tests
// -----------------------------------------------------------------------

TEST_CASE("FormatEvent: basic event without wall clock")
{
    RecordEvent ev{0.001, "input", "vpx_to_mpf", "set_switch",
                   R"({"number":"2","value":"true"})", ""};
    std::string line = Recorder::FormatEvent(ev, false, "");

    CHECK(line.find("\"ts\":0.001") != std::string::npos);
    CHECK(line.find("\"cat\":\"input\"") != std::string::npos);
    CHECK(line.find("\"dir\":\"vpx_to_mpf\"") != std::string::npos);
    CHECK(line.find("\"cmd\":\"set_switch\"") != std::string::npos);
    CHECK(line.find("\"params\":{\"number\":\"2\",\"value\":\"true\"}") != std::string::npos);
    CHECK(line.find("\"wall\"") == std::string::npos);
    CHECK(line.find("\"result\"") == std::string::npos);
}

TEST_CASE("FormatEvent: first event includes wall clock")
{
    RecordEvent ev{0.0, "state", "mpf_to_vpx", "changed_solenoids",
                   "", R"([[\"5\",true]])"};
    std::string wall = "2026-04-12T14:30:00.123456Z";
    std::string line = Recorder::FormatEvent(ev, true, wall);

    CHECK(line.find("\"wall\":\"2026-04-12T14:30:00.123456Z\"") != std::string::npos);
}

TEST_CASE("FormatEvent: event with result only")
{
    RecordEvent ev{1.5, "state", "mpf_to_vpx", "changed_lamps", "", R"([["3",true]])"};
    std::string line = Recorder::FormatEvent(ev, false, "");

    CHECK(line.find("\"result\":") != std::string::npos);
    CHECK(line.find("\"params\"") == std::string::npos);
}

TEST_CASE("FormatEvent: event with both params and result")
{
    RecordEvent ev{0.5, "query", "vpx_to_mpf", "switch",
                   R"({"number":"2"})", "true"};
    std::string line = Recorder::FormatEvent(ev, false, "");

    CHECK(line.find("\"params\":{\"number\":\"2\"}") != std::string::npos);
    CHECK(line.find("\"result\":true") != std::string::npos);
}

TEST_CASE("FormatEvent: command with quotes is escaped")
{
    RecordEvent ev{0.0, "query", "vpx_to_mpf", "say\"hello", "", ""};
    std::string line = Recorder::FormatEvent(ev, false, "");

    CHECK(line.find("say\\\"hello") != std::string::npos);
}

// -----------------------------------------------------------------------
// Full session tests — writes to a temp directory
// -----------------------------------------------------------------------

TEST_CASE("Recorder: disabled recorder does not create files")
{
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/mpf_test_rec_disabled";
    std::filesystem::remove_all(tmpDir);

    Recorder rec;
    rec.SetEnabled(false);
    rec.SetOutputDirectory(tmpDir);
    rec.StartSession();
    rec.Record({0.0, "input", "vpx_to_mpf", "set_switch", "{}", ""});
    rec.StopSession();

    CHECK_FALSE(std::filesystem::exists(tmpDir));
}

TEST_CASE("Recorder: enabled recorder creates JSONL file")
{
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/mpf_test_rec_enabled";
    std::filesystem::remove_all(tmpDir);

    Recorder rec;
    rec.SetEnabled(true);
    rec.SetOutputDirectory(tmpDir);
    rec.StartSession();

    rec.Record({rec.Now(), "input", "vpx_to_mpf", "set_switch",
                R"({"number":"1","value":"true"})", ""});
    rec.Record({rec.Now(), "state", "mpf_to_vpx", "changed_solenoids",
                "", R"([["0",true]])"});

    // Small sleep to let writer thread drain
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    rec.StopSession();

    // Find the .jsonl file
    bool foundFile = false;
    std::string fileContent;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".jsonl") {
            foundFile = true;
            std::ifstream f(entry.path());
            std::ostringstream ss;
            ss << f.rdbuf();
            fileContent = ss.str();
            break;
        }
    }

    REQUIRE(foundFile);

    // First line should have "wall" field
    auto firstNewline = fileContent.find('\n');
    REQUIRE(firstNewline != std::string::npos);
    std::string firstLine = fileContent.substr(0, firstNewline);
    CHECK(firstLine.find("\"wall\":") != std::string::npos);
    CHECK(firstLine.find("\"cat\":\"input\"") != std::string::npos);

    // Second line should NOT have "wall" field
    std::string rest = fileContent.substr(firstNewline + 1);
    auto secondNewline = rest.find('\n');
    REQUIRE(secondNewline != std::string::npos);
    std::string secondLine = rest.substr(0, secondNewline);
    CHECK(secondLine.find("\"wall\":") == std::string::npos);
    CHECK(secondLine.find("\"cat\":\"state\"") != std::string::npos);

    // Cleanup
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("Recorder: Now() returns elapsed time")
{
    Recorder rec;
    rec.SetEnabled(true);
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/mpf_test_rec_now";
    std::filesystem::remove_all(tmpDir);
    rec.SetOutputDirectory(tmpDir);

    CHECK(rec.Now() == 0.0); // not started yet

    rec.StartSession();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    double elapsed = rec.Now();
    CHECK(elapsed > 0.04);
    CHECK(elapsed < 0.5);

    rec.StopSession();
    std::filesystem::remove_all(tmpDir);
}

TEST_CASE("Recorder: file name starts with date")
{
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/mpf_test_rec_fname";
    std::filesystem::remove_all(tmpDir);

    Recorder rec;
    rec.SetEnabled(true);
    rec.SetOutputDirectory(tmpDir);
    rec.StartSession();
    rec.Record({0.0, "input", "vpx_to_mpf", "test", "{}", ""});
    rec.StopSession();

    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        std::string name = entry.path().filename().string();
        // Should match YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl
        CHECK(name.size() > 30);
        CHECK(name.substr(4, 1) == "-");  // YYYY-
        CHECK(name.find("_mpf_recording.jsonl") != std::string::npos);
        found = true;
    }
    CHECK(found);

    std::filesystem::remove_all(tmpDir);
}
```

- [ ] **Step 3: Add test_Recorder.cpp to CMakeLists.txt test target**

```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
    tests/test_Recorder.cpp
    src/BCPClient.cpp
)
```

- [ ] **Step 4: Build — tests should fail to link (no Recorder.cpp yet)**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Linker errors for `MPF::Recorder::*` symbols. BCPClient tests would still pass if built separately.

- [ ] **Step 5: Commit**

```bash
git add src/Recorder.h tests/test_Recorder.cpp CMakeLists.txt
git commit -m "Add Recorder tests (red: tests fail to link)"
```

---

### Task 5: Recorder — implementation (make tests green)

**Files:**
- Create: `src/Recorder.cpp`
- Modify: `CMakeLists.txt` (add source to both targets)

- [ ] **Step 1: Create src/Recorder.cpp**

```cpp
#include "Recorder.h"

#include <cstdio>
#include <ctime>
#include <filesystem>

namespace MPF {

Recorder::Recorder() = default;

Recorder::~Recorder()
{
    if (m_sessionActive)
        StopSession();
}

double Recorder::Now() const
{
    if (!m_sessionActive) return 0.0;
    auto elapsed = std::chrono::steady_clock::now() - m_sessionStart;
    return std::chrono::duration<double>(elapsed).count();
}

std::string Recorder::GenerateFilename() const
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H-%M-%S", &tm_buf);
    return std::string(buf) + "_mpf_recording.jsonl";
}

void Recorder::StartSession()
{
    if (!m_enabled || m_sessionActive) return;

    std::string dir = m_outputDir.empty() ? "recordings" : m_outputDir;
    std::filesystem::create_directories(dir);

    std::string filepath = dir + "/" + GenerateFilename();
    m_file.open(filepath, std::ios::out | std::ios::trunc);
    if (!m_file.is_open()) return;

    m_sessionStart = std::chrono::steady_clock::now();

    // Compute wall-clock anchor (ISO 8601 with microseconds)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()) % 1000000;
        std::tm tm_buf{};
#ifdef _WIN32
        gmtime_s(&tm_buf, &time_t_now);
#else
        gmtime_r(&time_t_now, &tm_buf);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);
        char full[80];
        std::snprintf(full, sizeof(full), "%s.%06dZ", buf, static_cast<int>(us.count()));
        m_wallClockAnchor = full;
    }

    m_writePos.store(0, std::memory_order_relaxed);
    m_readPos.store(0, std::memory_order_relaxed);
    m_sessionActive = true;

    m_writerRunning.store(true, std::memory_order_release);
    m_writerThread = std::thread(&Recorder::WriterThread, this);
}

void Recorder::StopSession()
{
    if (!m_sessionActive) return;
    m_sessionActive = false;

    m_writerRunning.store(false, std::memory_order_release);
    m_cv.notify_one();
    if (m_writerThread.joinable())
        m_writerThread.join();

    m_file.close();
}

void Recorder::Record(RecordEvent event)
{
    if (!m_sessionActive) return;

    size_t wp = m_writePos.load(std::memory_order_relaxed);
    size_t next = (wp + 1) % kRingSize;
    size_t rp = m_readPos.load(std::memory_order_acquire);

    if (next == rp) return; // buffer full, drop

    m_ring[wp] = std::move(event);
    m_writePos.store(next, std::memory_order_release);
    m_cv.notify_one();
}

// Escape a string for JSON output.
static void JsonEscapeAppend(std::string& out, const std::string& s)
{
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

std::string Recorder::FormatEvent(const RecordEvent& event,
                                  bool includeWallClock,
                                  const std::string& wallClockAnchor)
{
    std::string out;
    out.reserve(256);

    char tsBuf[32];
    std::snprintf(tsBuf, sizeof(tsBuf), "%.6f", event.timestamp);

    out += "{\"ts\":";
    out += tsBuf;

    if (includeWallClock && !wallClockAnchor.empty()) {
        out += ",\"wall\":";
        JsonEscapeAppend(out, wallClockAnchor);
    }

    out += ",\"cat\":\"";
    out += event.category;
    out += "\",\"dir\":\"";
    out += event.direction;
    out += "\",\"cmd\":";
    JsonEscapeAppend(out, event.command);

    if (!event.params.empty()) {
        out += ",\"params\":";
        out += event.params;
    }

    if (!event.result.empty()) {
        out += ",\"result\":";
        out += event.result;
    }

    out += '}';
    return out;
}

void Recorder::WriterThread()
{
    bool firstEvent = true;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !m_writerRunning.load(std::memory_order_acquire)
                    || m_readPos.load(std::memory_order_acquire)
                       != m_writePos.load(std::memory_order_acquire);
            });
        }

        size_t rp = m_readPos.load(std::memory_order_relaxed);
        size_t wp = m_writePos.load(std::memory_order_acquire);
        bool wrote = false;

        while (rp != wp) {
            const RecordEvent& ev = m_ring[rp];
            std::string line = FormatEvent(ev, firstEvent, m_wallClockAnchor);
            firstEvent = false;
            m_file << line << '\n';
            rp = (rp + 1) % kRingSize;
            wrote = true;
        }
        m_readPos.store(rp, std::memory_order_release);

        if (wrote)
            m_file.flush();

        if (!m_writerRunning.load(std::memory_order_acquire)) {
            m_file.flush();
            break;
        }
    }
}

} // namespace MPF
```

- [ ] **Step 2: Add Recorder.cpp to both targets in CMakeLists.txt**

Plugin:
```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
)
```

Tests:
```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
    tests/test_Recorder.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
)
```

- [ ] **Step 3: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All BCPClient and Recorder tests pass (green).

- [ ] **Step 4: Commit**

```bash
git add src/Recorder.cpp CMakeLists.txt
git commit -m "Implement Recorder: all tests green"
```

---

### Task 6: MPFController — tests first

**Files:**
- Create: `src/MPFController.h`
- Create: `src/MPFController.cpp`
- Create: `tests/test_MPFController.cpp`
- Modify: `CMakeLists.txt`

This task tests MPFController end-to-end with MockBCPServer acting as MPF. The mock server receives `vpcom_bridge` commands and returns canned `vpcom_bridge_response` messages. Since MPFController is a thin layer over BCPClient, these are integration-level tests that verify the full marshal-dispatch-unmarshal cycle.

Because MPFController's methods need both the header and implementation to be testable (unlike BCPClient's pure static functions), we create both header and implementation together, then write the tests.

- [ ] **Step 1: Create src/MPFController.h**

```cpp
#pragma once

#include "BCPClient.h"
#include "Recorder.h"
#include "plugins/ScriptablePlugin.h"

#include <string>
#include <map>

namespace MPF {

class MPFController {
    PSC_IMPLEMENT_REFCOUNT()

public:
    MPFController(bool recordingEnabled, const std::string& recordingPath);
    ~MPFController();

    // --- Lifecycle ---
    void Run();
    void Run(const std::string& addr);
    void Run(const std::string& addr, int port);
    void Stop();

    // --- Switch access ---
    bool GetSwitch(int number);
    bool GetSwitch(const std::string& number);
    void SetSwitch(int number, bool value);
    void SetSwitch(const std::string& number, bool value);
    void PulseSW(int number);
    void PulseSW(const std::string& number);

    // --- Mech access ---
    int ReadMech(int number);   // BCP subcommand "mech" — Mech(n) property read
    void SetMech(int number, int value);
    int GetMech(int number);    // BCP subcommand "get_mech"

    // --- Polled state ---
    std::string GetChangedSolenoids();
    std::string GetChangedLamps();
    std::string GetChangedGIStrings();
    std::string GetChangedLEDs();
    std::string GetChangedBrightnessLEDs();
    std::string GetChangedFlashers();
    std::string GetHardwareRules();
    bool IsCoilActive(int number);
    bool IsCoilActive(const std::string& number);

    // --- Stub properties ---
    std::string GetVersion() const { return "1.0.0"; }
    std::string GetGameName() const { return m_gameName; }
    void SetGameName(const std::string& name) { m_gameName = name; }
    bool GetShowTitle() const { return m_showTitle; }
    void SetShowTitle(bool v) { m_showTitle = v; }
    bool GetShowFrame() const { return false; }
    void SetShowFrame(bool) {}
    bool GetShowDMDOnly() const { return false; }
    void SetShowDMDOnly(bool) {}
    bool GetHandleMechanics() const { return m_handleMechanics; }
    void SetHandleMechanics(bool v) { m_handleMechanics = v; }
    bool GetHandleKeyboard() const { return false; }
    void SetHandleKeyboard(bool) {}
    bool GetDIP() const { return false; }
    void SetDIP(bool) {}
    bool GetPause() const { return m_pause; }
    void SetPause(bool v) { m_pause = v; }
    std::string GetSplashInfoLine() const { return m_splashInfoLine; }
    void SetSplashInfoLine(const std::string& v) { m_splashInfoLine = v; }

private:
    std::string DispatchToMPF(const char* category,
                              const std::string& subcommand,
                              const std::map<std::string, std::string>& extraParams = {});
    static std::string ParamsToJson(const std::map<std::string, std::string>& params);

    BCPClient m_bcp;
    Recorder m_recorder;

    std::string m_gameName = "Game";
    bool m_showTitle = false;
    bool m_handleMechanics = true;
    bool m_pause = false;
    std::string m_splashInfoLine;
};

} // namespace MPF
```

- [ ] **Step 2: Create src/MPFController.cpp**

```cpp
#include "MPFController.h"

namespace MPF {

MPFController::MPFController(bool recordingEnabled, const std::string& recordingPath)
{
    m_recorder.SetEnabled(recordingEnabled);
    if (!recordingPath.empty())
        m_recorder.SetOutputDirectory(recordingPath);
}

MPFController::~MPFController()
{
    if (m_bcp.IsConnected()) {
        m_recorder.StopSession();
        m_bcp.Disconnect();
    }
}

std::string MPFController::ParamsToJson(const std::map<std::string, std::string>& params)
{
    if (params.empty()) return "{}";
    std::string out = "{";
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) out += ',';
        first = false;
        out += "\"" + k + "\":\"" + v + "\"";
    }
    out += '}';
    return out;
}

std::string MPFController::DispatchToMPF(const char* category,
                                         const std::string& subcommand,
                                         const std::map<std::string, std::string>& extraParams)
{
    std::map<std::string, std::string> params = extraParams;
    params["subcommand"] = subcommand;

    if (m_recorder.IsEnabled()) {
        m_recorder.Record({
            m_recorder.Now(), category, "vpx_to_mpf",
            subcommand, ParamsToJson(extraParams), ""
        });
    }

    BCPResponse resp = m_bcp.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");

    std::string resultVal;
    if (!resp.command.empty()) {
        auto it = resp.params.find("result");
        if (it != resp.params.end()) resultVal = it->second;
    }

    if (m_recorder.IsEnabled()) {
        m_recorder.Record({
            m_recorder.Now(), category, "mpf_to_vpx",
            subcommand, "", resultVal.empty() ? "" : resultVal
        });
    }

    auto errIt = resp.params.find("error");
    if (errIt != resp.params.end()) return "";

    return resultVal;
}

// --- Lifecycle ---

void MPFController::Run() { Run("localhost", 5051); }
void MPFController::Run(const std::string& addr) { Run(addr, 5051); }
void MPFController::Run(const std::string& addr, int port)
{
    if (!m_bcp.Connect(addr, port)) return;
    std::map<std::string, std::string> params;
    params["subcommand"] = "start";
    m_bcp.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");
    m_recorder.StartSession();
}

void MPFController::Stop()
{
    m_recorder.StopSession();
    if (m_bcp.IsConnected()) {
        m_bcp.Send("vpcom_bridge", {{"subcommand", "stop"}});
        m_bcp.Disconnect();
    }
}

// --- Switches (int) ---
bool MPFController::GetSwitch(int n)
{
    std::string r = DispatchToMPF("query", "switch", {{"number", std::to_string(n)}});
    return r == "True" || r == "true" || r == "1";
}
void MPFController::SetSwitch(int n, bool v)
{
    DispatchToMPF("input", "set_switch",
        {{"number", std::to_string(n)}, {"value", v ? "bool:True" : "bool:False"}});
}
void MPFController::PulseSW(int n)
{
    DispatchToMPF("input", "pulsesw", {{"number", std::to_string(n)}});
}

// --- Switches (string) ---
bool MPFController::GetSwitch(const std::string& n)
{
    std::string r = DispatchToMPF("query", "switch", {{"number", n}});
    return r == "True" || r == "true" || r == "1";
}
void MPFController::SetSwitch(const std::string& n, bool v)
{
    DispatchToMPF("input", "set_switch",
        {{"number", n}, {"value", v ? "bool:True" : "bool:False"}});
}
void MPFController::PulseSW(const std::string& n)
{
    DispatchToMPF("input", "pulsesw", {{"number", n}});
}

// --- Mechs ---
int MPFController::ReadMech(int n)
{
    std::string r = DispatchToMPF("query", "mech", {{"number", std::to_string(n)}});
    try { return std::stoi(r); } catch (...) { return 0; }
}
void MPFController::SetMech(int n, int v)
{
    DispatchToMPF("input", "set_mech",
        {{"number", std::to_string(n)}, {"value", std::to_string(v)}});
}
int MPFController::GetMech(int n)
{
    std::string r = DispatchToMPF("query", "get_mech", {{"number", std::to_string(n)}});
    try { return std::stoi(r); } catch (...) { return 0; }
}

// --- Polled state ---
std::string MPFController::GetChangedSolenoids() { return DispatchToMPF("state", "changed_solenoids"); }
std::string MPFController::GetChangedLamps() { return DispatchToMPF("state", "changed_lamps"); }
std::string MPFController::GetChangedGIStrings() { return DispatchToMPF("state", "changed_gi_strings"); }
std::string MPFController::GetChangedLEDs() { return DispatchToMPF("state", "changed_leds"); }
std::string MPFController::GetChangedBrightnessLEDs() { return DispatchToMPF("state", "changed_brightness_leds"); }
std::string MPFController::GetChangedFlashers() { return DispatchToMPF("state", "changed_flashers"); }
std::string MPFController::GetHardwareRules() { return DispatchToMPF("state", "get_hardwarerules"); }

bool MPFController::IsCoilActive(int n)
{
    std::string r = DispatchToMPF("state", "get_coilactive", {{"number", std::to_string(n)}});
    return r == "True" || r == "true" || r == "1";
}
bool MPFController::IsCoilActive(const std::string& n)
{
    std::string r = DispatchToMPF("state", "get_coilactive", {{"number", n}});
    return r == "True" || r == "true" || r == "1";
}

} // namespace MPF
```

- [ ] **Step 3: Create tests/test_MPFController.cpp**

```cpp
#include "doctest.h"
#include "MPFController.h"
#include "MockBCPServer.h"
#include "BCPClient.h"

#include <string>
#include <filesystem>

using namespace MPF;

// Helper: create a mock server that handles vpcom_bridge commands.
// Parses the subcommand from the request and returns a matching response.
static MockHandler MakeMPFHandler()
{
    return [](const std::string& line) -> std::string {
        BCPResponse req = BCPClient::DecodeLine(line);
        if (req.command != "vpcom_bridge") return "";

        auto subcmdIt = req.params.find("subcommand");
        if (subcmdIt == req.params.end()) return "";
        const std::string& subcmd = subcmdIt->second;

        if (subcmd == "start" || subcmd == "stop")
            return "vpcom_bridge_response?result=ok";
        if (subcmd == "switch" || subcmd == "get_switch")
            return "vpcom_bridge_response?result=True";
        if (subcmd == "set_switch" || subcmd == "pulsesw")
            return "vpcom_bridge_response?result=ok";
        if (subcmd == "mech" || subcmd == "get_mech")
            return "vpcom_bridge_response?result=42";
        if (subcmd == "set_mech")
            return "vpcom_bridge_response?result=ok";
        if (subcmd == "changed_solenoids")
            return "vpcom_bridge_response?result=%5B%5B%220%22%2Ctrue%5D%5D"; // [["0",true]]
        if (subcmd == "changed_lamps")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "changed_gi_strings")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "changed_leds")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "changed_brightness_leds")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "changed_flashers")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "get_hardwarerules")
            return "vpcom_bridge_response?result=false";
        if (subcmd == "get_coilactive")
            return "vpcom_bridge_response?result=True";

        return "vpcom_bridge_response?error=unknown_subcommand";
    };
}

TEST_CASE("MPFController: Run and Stop lifecycle")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);
    // If Run succeeds, the controller sent "start" and got a response
    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: GetSwitch returns true")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.GetSwitch(1) == true);
    CHECK(ctrl.GetSwitch("swa") == true);

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: SetSwitch and PulseSW do not crash")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    ctrl.SetSwitch(2, true);
    ctrl.SetSwitch("swa", false);
    ctrl.PulseSW(3);
    ctrl.PulseSW("swb");

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: Mech access")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.ReadMech(1) == 42);
    CHECK(ctrl.GetMech(1) == 42);
    ctrl.SetMech(1, 10); // should not crash

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: ChangedSolenoids returns result string")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    std::string result = ctrl.GetChangedSolenoids();
    CHECK_FALSE(result.empty());

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: IsCoilActive")
{
    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    MPFController ctrl(false, "");
    ctrl.Run("127.0.0.1", port);

    CHECK(ctrl.IsCoilActive(0) == true);
    CHECK(ctrl.IsCoilActive("coil1") == true);

    ctrl.Stop();
    server.Stop();
}

TEST_CASE("MPFController: stub properties")
{
    MPFController ctrl(false, "");
    CHECK(ctrl.GetVersion() == "1.0.0");

    ctrl.SetGameName("TestGame");
    CHECK(ctrl.GetGameName() == "TestGame");

    ctrl.SetHandleMechanics(false);
    CHECK(ctrl.GetHandleMechanics() == false);

    ctrl.SetPause(true);
    CHECK(ctrl.GetPause() == true);
}

TEST_CASE("MPFController: recording creates file during session")
{
    std::string tmpDir = std::filesystem::temp_directory_path().string() + "/mpf_test_ctrl_rec";
    std::filesystem::remove_all(tmpDir);

    MockBCPServer server;
    int port = server.Start(MakeMPFHandler());
    REQUIRE(port > 0);

    {
        MPFController ctrl(true, tmpDir);
        ctrl.Run("127.0.0.1", port);
        ctrl.SetSwitch(1, true);
        ctrl.GetSwitch(1);
        ctrl.Stop();
    }

    // Check a recording file was created
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(tmpDir)) {
        if (entry.path().extension() == ".jsonl") {
            found = true;
            break;
        }
    }
    CHECK(found);

    server.Stop();
    std::filesystem::remove_all(tmpDir);
}
```

- [ ] **Step 4: Update CMakeLists.txt — add MPFController to both targets**

Plugin:
```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
    src/MPFController.cpp
)
```

Tests:
```cmake
add_executable(mpf-vpx-tests
    tests/test_main.cpp
    tests/test_BCPClient.cpp
    tests/test_Recorder.cpp
    tests/test_MPFController.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
    src/MPFController.cpp
)
```

- [ ] **Step 5: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: All tests pass (green).

- [ ] **Step 6: Commit**

```bash
git add src/MPFController.h src/MPFController.cpp tests/test_MPFController.cpp CMakeLists.txt
git commit -m "Add MPFController with tests: dispatch, switches, mechs, polling, recording"
```

---

### Task 7: MPFPlugin — wire entry point, settings, COM override

**Files:**
- Modify: `src/MPFPlugin.cpp` (replace stub)

This task cannot be unit tested without the VPX plugin host — it registers with VPX's ScriptablePluginAPI. The compile + link is the verification.

- [ ] **Step 1: Replace src/MPFPlugin.cpp with full implementation**

```cpp
#include "MPFController.h"

#include "plugins/MsgPlugin.h"
#include "plugins/ScriptablePlugin.h"
#include "plugins/LoggingPlugin.h"

#include <cassert>
#include <string>

namespace MPF {

// ---------------------------------------------------------------------------
// Scriptable class definition
// ---------------------------------------------------------------------------

PSC_CLASS_START(MPF_Controller, MPFController)
    // Lifecycle
    PSC_FUNCTION0(void, Run)
    PSC_FUNCTION1(void, Run, string)
    PSC_FUNCTION2(void, Run, string, int)
    PSC_FUNCTION0(void, Stop)

    // Switch access (int-indexed)
    PSC_PROP_RW_ARRAY1(bool, Switch, int)
    PSC_FUNCTION1(void, PulseSW, int)

    // Switch access (string-indexed)
    PSC_PROP_RW_ARRAY1(bool, Switch, string)
    PSC_FUNCTION1(void, PulseSW, string)

    // Mech write
    PSC_PROP_W_ARRAY1(int, Mech, int)

    // Mech read and GetMech read — registered manually because the PSC_PROP_R_ARRAY1
    // macro prepends "Get" to the name, causing naming collisions.
    members.push_back( { { "Mech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<_BindedClass*>(me)->ReadMech(pArgs[0].vInt);
        } });
    members.push_back( { { "GetMech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<_BindedClass*>(me)->GetMech(pArgs[0].vInt);
        } });

    // Polled state
    PSC_PROP_R(string, ChangedSolenoids)
    PSC_PROP_R(string, ChangedLamps)
    PSC_PROP_R(string, ChangedGIStrings)
    PSC_PROP_R(string, ChangedLEDs)
    PSC_PROP_R(string, ChangedBrightnessLEDs)
    PSC_PROP_R(string, ChangedFlashers)
    PSC_PROP_R(string, HardwareRules)
    PSC_FUNCTION1(bool, IsCoilActive, int)
    PSC_FUNCTION1(bool, IsCoilActive, string)

    // Stub properties
    PSC_PROP_R(string, Version)
    PSC_PROP_RW(string, GameName)
    PSC_PROP_RW(bool, ShowTitle)
    PSC_PROP_RW(bool, ShowFrame)
    PSC_PROP_RW(bool, ShowDMDOnly)
    PSC_PROP_RW(bool, HandleMechanics)
    PSC_PROP_RW(bool, HandleKeyboard)
    PSC_PROP_RW(bool, DIP)
    PSC_PROP_RW(bool, Pause)
    PSC_PROP_RW(string, SplashInfoLine)
PSC_CLASS_END()

// ---------------------------------------------------------------------------
// Plugin state
// ---------------------------------------------------------------------------

static const MsgPluginAPI* msgApi = nullptr;
static ScriptablePluginAPI* scriptApi = nullptr;
static unsigned int getScriptApiMsgId = 0;
static uint32_t endpointId = 0;
static MPFController* controller = nullptr;

PSC_ERROR_IMPLEMENT(scriptApi);

MSGPI_BOOL_VAL_SETTING(enableRecordingProp, "EnableRecording", "Enable Recording",
    "Record BCP events during game sessions", true, false);
MSGPI_STRING_VAL_SETTING(recordingPathProp, "RecordingPath", "Recording Path",
    "Directory for recording files (empty = recordings/ next to plugin)", true, "", 1024);

} // namespace MPF

using namespace MPF;

// ---------------------------------------------------------------------------
// Plugin Load / Unload
// ---------------------------------------------------------------------------

MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    msgApi = api;
    endpointId = sessionId;

    msgApi->RegisterSetting(endpointId, &enableRecordingProp);
    msgApi->RegisterSetting(endpointId, &recordingPathProp);

    getScriptApiMsgId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
    msgApi->BroadcastMsg(endpointId, getScriptApiMsgId, &scriptApi);

    auto regLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    RegisterMPF_Controller(regLambda);

    MPF_Controller_SCD->CreateObject = []() -> void*
    {
        assert(controller == nullptr);
        controller = new MPFController(
            enableRecordingProp_Val != 0,
            std::string(recordingPathProp_Val));
        return static_cast<void*>(controller);
    };

    scriptApi->SubmitTypeLibrary(endpointId);
    scriptApi->SetCOMObjectOverride("MPF.Controller", MPF_Controller_SCD);
}

MSGPI_EXPORT void MSGPIAPI MPFPluginUnload()
{
    if (controller) {
        while (controller) {
            controller->Release();
        }
    }

    scriptApi->SetCOMObjectOverride("MPF.Controller", nullptr);

    auto regLambda = [](ScriptClassDef* scd) { scriptApi->UnregisterScriptClass(scd); };
    UnregisterMPF_Controller(regLambda);

    msgApi->ReleaseMsgID(getScriptApiMsgId);
    scriptApi = nullptr;
    msgApi = nullptr;
}
```

- [ ] **Step 2: Build and run all tests**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: Plugin builds (verifies PSC macros compile correctly). All tests still pass.

- [ ] **Step 3: Commit**

```bash
git add src/MPFPlugin.cpp
git commit -m "Wire MPFPlugin: settings, script class registration, COM override"
```

---

### Task 8: CI/CD — GitHub Actions

**Files:**
- Create: `.github/workflows/build.yml`
- Create: `.github/workflows/release.yml`

- [ ] **Step 1: Create .github/workflows/build.yml**

```yaml
name: Build

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [macos-latest, ubuntu-latest, windows-latest]
        vpx_tag: ["v10.8.0-2051-28dd6c3", "main"]

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release -DVPX_TAG=${{ matrix.vpx_tag }}

      - name: Build
        run: cmake --build build --config Release

      - name: Run tests
        run: ctest --test-dir build --output-on-failure -C Release

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: mpf-plugin-${{ matrix.os }}-${{ matrix.vpx_tag }}
          path: build/dist/mpf/
```

- [ ] **Step 2: Create .github/workflows/release.yml**

```yaml
name: Release

on:
  push:
    tags: ["v*"]

permissions:
  contents: write

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        include:
          - os: macos-latest
            artifact: mpf-plugin-macos
          - os: ubuntu-latest
            artifact: mpf-plugin-linux
          - os: windows-latest
            artifact: mpf-plugin-windows

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build --config Release

      - name: Run tests
        run: ctest --test-dir build --output-on-failure -C Release

      - name: Package
        run: |
          cd build/dist
          tar czf ${{ matrix.artifact }}.tar.gz mpf/

      - name: Upload artifact
        uses: actions/upload-artifact@v4
        with:
          name: ${{ matrix.artifact }}
          path: build/dist/${{ matrix.artifact }}.tar.gz

  release:
    needs: build
    runs-on: ubuntu-latest
    steps:
      - uses: actions/download-artifact@v4
        with:
          path: artifacts

      - name: Create GitHub Release
        uses: softprops/action-gh-release@v2
        with:
          files: artifacts/**/*.tar.gz
          generate_release_notes: true
```

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/build.yml .github/workflows/release.yml
git commit -m "Add CI/CD: build matrix with tests, release workflow"
```

---

### Task 9: README.md

**Files:**
- Create: `README.md`

- [ ] **Step 1: Create README.md**

```markdown
# mpf-vpx-plugin

Cross-platform VPX plugin that bridges Visual Pinball X to the Mission Pinball Framework (MPF) via BCP, enabling MPF-driven game logic on macOS, Linux, and Windows.

This plugin replaces the Windows-only [mpf-vpcom-bridge](https://github.com/missionpinball/mpf-vpcom-bridge) by using VPX's native plugin SDK. Existing table scripts using `CreateObject("MPF.Controller")` work unchanged.

## Installation

1. Download the latest release for your platform from the [Releases](https://github.com/Pyrrvs/mpf-vpx-plugin/releases) page.
2. Extract the `mpf/` folder into your VPX plugins directory:
   - **macOS:** `VPinballX_BGFX.app/Contents/Resources/plugins/`
   - **Linux:** `<vpx-install>/plugins/`
   - **Windows:** `<vpx-install>/plugins/`
3. Enable the plugin in VPX settings.

## MPF Configuration

In your MPF machine config:

```yaml
hardware:
    platform: virtual_pinball
```

## Usage

1. Start MPF (`mpf both`), wait until the display has been initialized.
2. Start VPX and load your table.
3. The table script's `CreateObject("MPF.Controller")` will be handled by this plugin.
4. `Controller.Run` connects to MPF's BCP server (default: `localhost:5051`).

To specify a custom address/port in your table script:

```vbscript
Controller.Run "192.168.1.100", 5051
```

## Recording

The plugin can record BCP events during game sessions for debugging and test generation.

Enable recording in VPX's plugin settings:
- **Enable Recording:** toggle on
- **Recording Path:** directory for output files (default: `recordings/` next to the plugin)

Recordings are saved as JSONL files (one JSON object per line), named `YYYY-MM-DD_HH-MM-SS_mpf_recording.jsonl`.

Each event has a category for easy filtering:
- `input` — state changes from VPX to MPF (SetSwitch, PulseSW, SetMech)
- `state` — polled state from MPF (ChangedSolenoids, ChangedLamps, etc.)
- `query` — read-only queries (Switch, GetSwitch, GetMech)

## Building from source

Requirements: CMake 3.20+, C++20 compiler

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The built plugin is placed in `build/dist/mpf/`.

To build against a specific VPX version:

```bash
cmake -B build -DVPX_TAG=v10.8.0-2051-28dd6c3
```

## License

GPLv3 — see [LICENSE](LICENSE).
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "Add README with installation, usage, recording, and build instructions"
```

---

### Task 10: End-to-end verification

**Files:** None (verification only)

- [ ] **Step 1: Clean build from scratch**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Expected: Build succeeds.

- [ ] **Step 2: Run full test suite**

```bash
ctest --test-dir build --output-on-failure -C Release
```

Expected: All tests pass.

- [ ] **Step 3: Verify plugin binary exports correct symbols**

```bash
nm -gU build/dist/mpf/plugin-mpf.dylib | grep -i "Plugin"
```

Expected: `MPFPluginLoad` and `MPFPluginUnload` symbols are visible.

- [ ] **Step 4: Verify dist folder is complete**

```bash
ls -la build/dist/mpf/
```

Expected: `plugin.cfg` and `plugin-mpf.dylib` (or `.so`/`.dll` on other platforms).

- [ ] **Step 5: Push to remote**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
git push origin main
```
