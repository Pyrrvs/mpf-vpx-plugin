# mpf-vpx-plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a cross-platform VPX plugin that bridges Visual Pinball X to MPF via BCP, with game session recording.

**Architecture:** Four components with clean boundaries: BCPClient (TCP socket + BCP wire protocol), Recorder (lock-free queue + background JSONL writer), MPFController (scriptable VBScript surface), MPFPlugin (entry point + wiring). The plugin registers as `MPF.Controller` via VPX's `SetCOMObjectOverride` so existing table scripts work unchanged.

**Tech Stack:** C++20, CMake 3.20+, VPX plugin SDK (fetched via FetchContent), raw platform sockets (POSIX/Winsock)

**Spec:** `docs/superpowers/specs/2026-04-12-mpf-vpx-plugin-design.md`

**BCP wire format reference:** `command?key=value&key=value\n` over TCP. Values are type-prefixed (`int:5`, `bool:True`, plain strings) and URL-percent-encoded (colons become `%3A`). Example: `vpcom_bridge?subcommand=set_switch&number=int%3A5&value=bool%3ATrue\n`. Responses are the same format — read lines until one starts with the expected response command name.

---

### Task 1: Repository scaffold

**Files:**
- Create: `.gitignore`
- Create: `plugin.cfg`
- Create: `CMakeLists.txt`
- Create: `src/MPFPlugin.cpp` (minimal stub that compiles)

This task sets up the build system so that `cmake --build` succeeds and produces a plugin binary. The stub plugin exports the required Load/Unload symbols but does nothing.

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

# Plugin shared library
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
```

- [ ] **Step 4: Create minimal stub src/MPFPlugin.cpp**

This file must compile and export the two required plugin symbols. It does nothing yet — just proves the build works.

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

- [ ] **Step 5: Build to verify scaffold works**

Run:
```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake -B build
cmake --build build
```

Expected: Build succeeds. `build/dist/mpf/plugin.cfg` and `build/dist/mpf/plugin-mpf.dylib` exist.

- [ ] **Step 6: Commit**

```bash
git add .gitignore plugin.cfg CMakeLists.txt src/MPFPlugin.cpp
git commit -m "Scaffold: CMake build with VPX SDK FetchContent and stub plugin"
```

---

### Task 2: BCPClient — platform socket abstraction

**Files:**
- Create: `src/BCPClient.h`
- Create: `src/BCPClient.cpp`
- Modify: `CMakeLists.txt` (add source file)

This task implements the TCP socket wrapper and BCP wire protocol (encode, send, receive, decode). It is fully self-contained — no dependency on other plugin components.

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
    // Returns the parsed response. On error, response.command is empty.
    BCPResponse SendAndWait(const std::string& command,
                            const std::map<std::string, std::string>& params,
                            const std::string& waitForCommand);

    // Fire-and-forget send (for stop/disconnect).
    void Send(const std::string& command,
              const std::map<std::string, std::string>& params);

    void SetTimeout(int timeoutMs) { m_timeoutMs = timeoutMs; }

private:
    // BCP wire format encoding/decoding
    static std::string EncodeCommand(const std::string& command,
                                     const std::map<std::string, std::string>& params);
    static BCPResponse DecodeLine(const std::string& line);
    static std::string UrlEncode(const std::string& value);
    static std::string UrlDecode(const std::string& value);

    // Platform socket operations
    bool SocketConnect(const std::string& host, int port);
    void SocketClose();
    bool SocketSendLine(const std::string& line);
    bool SocketReadLine(std::string& out);

#ifdef _WIN32
    unsigned long long m_socket; // SOCKET is UINT_PTR on Windows
#else
    int m_socket;
#endif
    bool m_connected;
    int m_timeoutMs;
    std::string m_readBuffer;
};

} // namespace MPF
```

- [ ] **Step 2: Create src/BCPClient.cpp**

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
    #include <fcntl.h>
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
    // Check buffer first for a complete line
    while (true) {
        auto nlpos = m_readBuffer.find('\n');
        if (nlpos != std::string::npos) {
            out = m_readBuffer.substr(0, nlpos);
            m_readBuffer.erase(0, nlpos + 1);
            return true;
        }

        // Poll with timeout
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
        if (pollResult <= 0) return false; // timeout or error

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

    // Read lines until we see one matching waitForCommand
    std::string line;
    while (SocketReadLine(line)) {
        BCPResponse resp = DecodeLine(line);
        if (resp.command == waitForCommand)
            return resp;
        // Discard non-matching lines (other BCP traffic)
    }

    // Timeout or socket error
    Disconnect();
    return empty;
}

} // namespace MPF
```

- [ ] **Step 3: Add BCPClient.cpp to CMakeLists.txt**

In `CMakeLists.txt`, change the `add_library` call:

```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
)
```

- [ ] **Step 4: Build to verify BCPClient compiles**

Run:
```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/BCPClient.h src/BCPClient.cpp CMakeLists.txt
git commit -m "Add BCPClient: synchronous TCP client with BCP wire protocol"
```

---

### Task 3: Recorder — SPSC queue and background JSONL writer

**Files:**
- Create: `src/Recorder.h`
- Create: `src/Recorder.cpp`
- Modify: `CMakeLists.txt` (add source file)

This task implements the non-blocking event recorder. The SPSC ring buffer and background writer thread are all in these two files. No dependency on BCPClient or the plugin SDK.

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

private:
    void WriterThread();
    std::string FormatEvent(const RecordEvent& event, bool includeWallClock) const;
    std::string GenerateFilename() const;

    bool m_enabled = false;
    std::string m_outputDir;
    bool m_sessionActive = false;
    std::chrono::steady_clock::time_point m_sessionStart;
    std::string m_wallClockAnchor; // ISO 8601 wall clock at session start

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

- [ ] **Step 2: Create src/Recorder.cpp**

```cpp
#include "Recorder.h"

#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>
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

    // Create output directory if needed
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

    // Start writer thread
    m_writerRunning.store(true, std::memory_order_release);
    m_writerThread = std::thread(&Recorder::WriterThread, this);
}

void Recorder::StopSession()
{
    if (!m_sessionActive) return;
    m_sessionActive = false;

    // Signal writer thread to stop and drain
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

    if (next == rp) return; // buffer full, drop event

    m_ring[wp] = std::move(event);
    m_writePos.store(next, std::memory_order_release);
    m_cv.notify_one();
}

// Escape a string for JSON output. Handles quotes, backslashes, and control chars.
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

std::string Recorder::FormatEvent(const RecordEvent& event, bool includeWallClock) const
{
    std::string out;
    out.reserve(256);

    char tsBuf[32];
    std::snprintf(tsBuf, sizeof(tsBuf), "%.6f", event.timestamp);

    out += "{\"ts\":";
    out += tsBuf;

    if (includeWallClock) {
        out += ",\"wall\":";
        JsonEscapeAppend(out, m_wallClockAnchor);
    }

    out += ",\"cat\":\"";
    out += event.category;
    out += "\",\"dir\":\"";
    out += event.direction;
    out += "\",\"cmd\":";
    JsonEscapeAppend(out, event.command);

    if (!event.params.empty()) {
        out += ",\"params\":";
        out += event.params; // already JSON
    }

    if (!event.result.empty()) {
        out += ",\"result\":";
        out += event.result; // already JSON
    }

    out += '}';
    return out;
}

void Recorder::WriterThread()
{
    bool firstEvent = true;

    while (true) {
        // Wait for events or stop signal
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                return !m_writerRunning.load(std::memory_order_acquire)
                    || m_readPos.load(std::memory_order_acquire)
                       != m_writePos.load(std::memory_order_acquire);
            });
        }

        // Drain all available events
        size_t rp = m_readPos.load(std::memory_order_relaxed);
        size_t wp = m_writePos.load(std::memory_order_acquire);

        while (rp != wp) {
            const RecordEvent& ev = m_ring[rp];
            std::string line = FormatEvent(ev, firstEvent);
            firstEvent = false;
            m_file << line << '\n';
            rp = (rp + 1) % kRingSize;
        }
        m_readPos.store(rp, std::memory_order_release);

        if (rp != wp)
            m_file.flush();

        // Exit after draining if stop was requested
        if (!m_writerRunning.load(std::memory_order_acquire)) {
            m_file.flush();
            break;
        }
    }
}

} // namespace MPF
```

- [ ] **Step 3: Add Recorder.cpp to CMakeLists.txt**

In `CMakeLists.txt`, update the `add_library` call:

```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
)
```

- [ ] **Step 4: Build to verify Recorder compiles**

Run:
```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/Recorder.h src/Recorder.cpp CMakeLists.txt
git commit -m "Add Recorder: SPSC ring buffer with background JSONL writer"
```

---

### Task 4: MPFController — scriptable Controller class

**Files:**
- Create: `src/MPFController.h`
- Create: `src/MPFController.cpp`
- Modify: `CMakeLists.txt` (add source file)

This task implements the Controller object that VBScript interacts with. It uses BCPClient for BCP communication and Recorder for event capture. Each method is a thin wrapper: marshal args, dispatch to BCP, record, return result.

**Reference:** The Python bridge's `Controller._dispatch_to_mpf()` at `mpf_vpcom_bridge/main.py:190-203` — this C++ class does the same thing.

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
    int ReadMech(int number);   // BCP subcommand "mech" — used by Mech(n) property read
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
    // Dispatch a BCP command to MPF and return the "result" param from the response.
    // Records the event if recording is active.
    std::string DispatchToMPF(const char* category,
                              const std::string& subcommand,
                              const std::map<std::string, std::string>& extraParams = {});

    // JSON helpers for pre-serializing params/results for the recorder
    static std::string ParamsToJson(const std::map<std::string, std::string>& params);

    BCPClient m_bcp;
    Recorder m_recorder;

    // Stub state
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

#include <stdexcept>

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

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// BCP dispatch
// ---------------------------------------------------------------------------

std::string MPFController::DispatchToMPF(const char* category,
                                         const std::string& subcommand,
                                         const std::map<std::string, std::string>& extraParams)
{
    std::map<std::string, std::string> params = extraParams;
    params["subcommand"] = subcommand;

    // Record outbound event
    if (m_recorder.IsEnabled()) {
        m_recorder.Record({
            m_recorder.Now(),
            category,
            "vpx_to_mpf",
            subcommand,
            ParamsToJson(extraParams),
            ""
        });
    }

    BCPResponse resp = m_bcp.SendAndWait("vpcom_bridge", params, "vpcom_bridge_response");

    if (resp.command.empty()) return ""; // socket error

    // Record inbound event
    std::string resultVal;
    auto it = resp.params.find("result");
    if (it != resp.params.end()) resultVal = it->second;

    if (m_recorder.IsEnabled()) {
        m_recorder.Record({
            m_recorder.Now(),
            category,
            "mpf_to_vpx",
            subcommand,
            "",
            resultVal.empty() ? "" : resultVal
        });
    }

    auto errIt = resp.params.find("error");
    if (errIt != resp.params.end()) return "";

    return resultVal;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void MPFController::Run()
{
    Run("localhost", 5051);
}

void MPFController::Run(const std::string& addr)
{
    Run(addr, 5051);
}

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
        std::map<std::string, std::string> params;
        params["subcommand"] = "stop";
        m_bcp.Send("vpcom_bridge", params);
        m_bcp.Disconnect();
    }
}

// ---------------------------------------------------------------------------
// Switches — int overloads
// ---------------------------------------------------------------------------

bool MPFController::GetSwitch(int number)
{
    std::string r = DispatchToMPF("query", "switch",
        {{"number", std::to_string(number)}});
    return r == "True" || r == "true" || r == "1";
}

void MPFController::SetSwitch(int number, bool value)
{
    DispatchToMPF("input", "set_switch",
        {{"number", std::to_string(number)},
         {"value", value ? "bool:True" : "bool:False"}});
}

void MPFController::PulseSW(int number)
{
    DispatchToMPF("input", "pulsesw",
        {{"number", std::to_string(number)}});
}

// ---------------------------------------------------------------------------
// Switches — string overloads
// ---------------------------------------------------------------------------

bool MPFController::GetSwitch(const std::string& number)
{
    std::string r = DispatchToMPF("query", "switch", {{"number", number}});
    return r == "True" || r == "true" || r == "1";
}

void MPFController::SetSwitch(const std::string& number, bool value)
{
    DispatchToMPF("input", "set_switch",
        {{"number", number},
         {"value", value ? "bool:True" : "bool:False"}});
}

void MPFController::PulseSW(const std::string& number)
{
    DispatchToMPF("input", "pulsesw", {{"number", number}});
}

// ---------------------------------------------------------------------------
// Mechs
// ---------------------------------------------------------------------------

int MPFController::ReadMech(int number)
{
    std::string r = DispatchToMPF("query", "mech",
        {{"number", std::to_string(number)}});
    try { return std::stoi(r); } catch (...) { return 0; }
}

void MPFController::SetMech(int number, int value)
{
    DispatchToMPF("input", "set_mech",
        {{"number", std::to_string(number)},
         {"value", std::to_string(value)}});
}

int MPFController::GetMech(int number)
{
    std::string r = DispatchToMPF("query", "get_mech",
        {{"number", std::to_string(number)}});
    try { return std::stoi(r); } catch (...) { return 0; }
}

// ---------------------------------------------------------------------------
// Polled state
// ---------------------------------------------------------------------------

std::string MPFController::GetChangedSolenoids()
{
    return DispatchToMPF("state", "changed_solenoids");
}

std::string MPFController::GetChangedLamps()
{
    return DispatchToMPF("state", "changed_lamps");
}

std::string MPFController::GetChangedGIStrings()
{
    return DispatchToMPF("state", "changed_gi_strings");
}

std::string MPFController::GetChangedLEDs()
{
    return DispatchToMPF("state", "changed_leds");
}

std::string MPFController::GetChangedBrightnessLEDs()
{
    return DispatchToMPF("state", "changed_brightness_leds");
}

std::string MPFController::GetChangedFlashers()
{
    return DispatchToMPF("state", "changed_flashers");
}

std::string MPFController::GetHardwareRules()
{
    return DispatchToMPF("state", "get_hardwarerules");
}

bool MPFController::IsCoilActive(int number)
{
    std::string r = DispatchToMPF("state", "get_coilactive",
        {{"number", std::to_string(number)}});
    return r == "True" || r == "true" || r == "1";
}

bool MPFController::IsCoilActive(const std::string& number)
{
    std::string r = DispatchToMPF("state", "get_coilactive",
        {{"number", number}});
    return r == "True" || r == "true" || r == "1";
}

} // namespace MPF
```

- [ ] **Step 3: Add MPFController.cpp to CMakeLists.txt**

```cmake
add_library(mpf-vpx-plugin MODULE
    src/MPFPlugin.cpp
    src/BCPClient.cpp
    src/Recorder.cpp
    src/MPFController.cpp
)
```

- [ ] **Step 4: Build to verify MPFController compiles**

Run:
```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add src/MPFController.h src/MPFController.cpp CMakeLists.txt
git commit -m "Add MPFController: scriptable Controller with BCP dispatch and recording"
```

---

### Task 5: MPFPlugin — entry point, settings, COM override

**Files:**
- Modify: `src/MPFPlugin.cpp` (replace stub with full implementation)

This task replaces the stub plugin entry point with the real implementation: it fetches the ScriptablePluginAPI, registers the MPF_Controller script class with all properties/methods using PSC macros, registers plugin settings, and wires `SetCOMObjectOverride("MPF.Controller", ...)`.

**Reference:** Follow the exact pattern from `vpinball/plugins/pinmame/PinMAMEPlugin.cpp` lines 395-496 and `vpinball/plugins/helloscript/helloscript.cpp` lines 38-57.

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
// Scriptable class definition using PSC macros
// ---------------------------------------------------------------------------

// The PSC macros expect the bound class to have Get*/Set* methods.
// Switch and Mech need array-style access (indexed by int or string).
// VBScript uses both int and string switch numbers. We register the int
// variant via PSC_PROP_RW_ARRAY1 and add a manual string overload.

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
    // macro prepends "Get" to the name, which causes naming collisions.
    // Mech(number) reads via BCP subcommand "mech"
    // GetMech(number) reads via BCP subcommand "get_mech"
    members.push_back( { { "Mech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<_BindedClass*>(me)->ReadMech(pArgs[0].vInt);
        } });
    members.push_back( { { "GetMech" }, { "int" }, 1, { { "int" } },
        [](void* me, int, ScriptVariant* pArgs, ScriptVariant* pRet) {
            pRet->vInt = static_cast<_BindedClass*>(me)->GetMech(pArgs[0].vInt);
        } });

    // Polled state — these return raw strings (JSON from MPF)
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

// Plugin settings
MSGPI_BOOL_VAL_SETTING(enableRecordingProp, "EnableRecording", "Enable Recording",
    "Record BCP events during game sessions", true, false);
MSGPI_STRING_VAL_SETTING(recordingPathProp, "RecordingPath", "Recording Path",
    "Directory for recording files (empty = recordings/ next to plugin)", true, "", 1024);

} // namespace MPF

using namespace MPF;

// ---------------------------------------------------------------------------
// Plugin Load / Unload (exported C functions)
// ---------------------------------------------------------------------------

MSGPI_EXPORT void MSGPIAPI MPFPluginLoad(const uint32_t sessionId, const MsgPluginAPI* api)
{
    msgApi = api;
    endpointId = sessionId;

    // Register settings
    msgApi->RegisterSetting(endpointId, &enableRecordingProp);
    msgApi->RegisterSetting(endpointId, &recordingPathProp);

    // Get Scriptable API
    getScriptApiMsgId = msgApi->GetMsgID(SCRIPTPI_NAMESPACE, SCRIPTPI_MSG_GET_API);
    msgApi->BroadcastMsg(endpointId, getScriptApiMsgId, &scriptApi);

    // Register our script class
    auto regLambda = [](ScriptClassDef* scd) { scriptApi->RegisterScriptClass(scd); };
    RegisterMPF_Controller(regLambda);

    // Set up factory
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
    // Clean up controller if VBScript didn't release it
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

- [ ] **Step 2: Build to verify the full plugin compiles**

Run:
```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
cmake --build build
```

Expected: Build succeeds. `build/dist/mpf/plugin-mpf.dylib` is produced.

- [ ] **Step 3: Commit**

```bash
git add src/MPFPlugin.cpp
git commit -m "Wire MPFPlugin: settings, script class registration, COM override"
```

---

### Task 6: CI/CD — GitHub Actions

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
git commit -m "Add CI/CD: build matrix and release workflows"
```

---

### Task 7: README.md

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
git commit -m "Add README with installation, usage, and build instructions"
```

---

### Task 8: Verify end-to-end build

**Files:** None (verification only)

- [ ] **Step 1: Clean build from scratch**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Expected: Build succeeds. `build/dist/mpf/` contains `plugin.cfg` and `plugin-mpf.dylib`.

- [ ] **Step 2: Verify the dylib exports the required symbols**

```bash
nm -gU build/dist/mpf/plugin-mpf.dylib | grep -i "Plugin"
```

Expected: `MPFPluginLoad` and `MPFPluginUnload` symbols are visible.

- [ ] **Step 3: Push to remote**

```bash
cd /Users/gla/Workspace/Bump/mpf/mpf-vpx-plugin
git push origin main
```
