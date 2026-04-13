#include "BCPClient.h"

#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>

#include "json.hpp"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socket_t = SOCKET;
    #define INVALID_SOCK INVALID_SOCKET
    #define CLOSE_SOCKET closesocket
    using ssize_t = int;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <poll.h>
    #include <errno.h>
    using socket_t = int;
    #define INVALID_SOCK (-1)
    #define CLOSE_SOCKET ::close
#endif

namespace MPF {

// ---------------------------------------------------------------------------
// URL encoding / decoding
// ---------------------------------------------------------------------------

std::string BCPClient::UrlEncode(const std::string& value) {
    std::ostringstream out;
    out.fill('0');
    out << std::hex << std::uppercase;
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else {
            out << '%' << std::setw(2) << static_cast<int>(c);
        }
    }
    return out.str();
}

std::string BCPClient::UrlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            std::string hex = value.substr(i + 1, 2);
            char ch = static_cast<char>(std::stoi(hex, nullptr, 16));
            out += ch;
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// BCP wire format encode / decode
// ---------------------------------------------------------------------------

std::string BCPClient::EncodeCommand(const std::string& command,
                                     const std::map<std::string, std::string>& params) {
    if (params.empty()) return command;
    std::string result = command + "?";
    bool first = true;
    for (const auto& [key, val] : params) {
        if (!first) result += '&';
        result += key + "=" + UrlEncode(val);
        first = false;
    }
    return result;
}

BCPResponse BCPClient::DecodeLine(const std::string& line) {
    BCPResponse resp;
    size_t qpos = line.find('?');
    if (qpos == std::string::npos) {
        resp.command = line;
        return resp;
    }
    resp.command = line.substr(0, qpos);
    std::string query = line.substr(qpos + 1);

    // Split on '&'
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eqpos = pair.find('=');
        if (eqpos == std::string::npos) {
            resp.params[pair] = "";
        } else {
            std::string key = pair.substr(0, eqpos);
            std::string val = pair.substr(eqpos + 1);
            resp.params[key] = UrlDecode(val);
        }
    }

    // If there's a "json" key, parse the JSON and flatten inner keys
    auto jsonIt = resp.params.find("json");
    if (jsonIt != resp.params.end()) {
        try {
            auto j = nlohmann::json::parse(jsonIt->second);
            if (j.is_object()) {
                for (auto& [key, value] : j.items()) {
                    resp.params[key] = value.dump();
                }
            }
        } catch (...) {
            // If JSON parsing fails, leave the raw "json" key as-is
        }
        resp.params.erase("json");
    }

    return resp;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

BCPClient::BCPClient()
    : m_socket(static_cast<decltype(m_socket)>(INVALID_SOCK))
    , m_connected(false)
    , m_timeoutMs(5000)
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

BCPClient::~BCPClient() {
    Disconnect();
#ifdef _WIN32
    WSACleanup();
#endif
}

// ---------------------------------------------------------------------------
// Public connection API
// ---------------------------------------------------------------------------

bool BCPClient::Connect(const std::string& host, int port) {
    fprintf(stderr, "[MPF] BCPClient::Connect(%s, %d)\n", host.c_str(), port);
    if (m_connected) Disconnect();
    if (!SocketConnect(host, port)) {
        fprintf(stderr, "[MPF] BCPClient::Connect FAILED - socket connect failed\n");
        return false;
    }
    m_connected = true;
    fprintf(stderr, "[MPF] BCPClient::Connect SUCCESS\n");
    return true;
}

void BCPClient::Disconnect() {
    if (m_connected) {
        SocketClose();
        m_connected = false;
    }
    m_readBuffer.clear();
}

bool BCPClient::IsConnected() const {
    return m_connected;
}

// ---------------------------------------------------------------------------
// Send / SendAndWait
// ---------------------------------------------------------------------------

void BCPClient::Send(const std::string& command,
                     const std::map<std::string, std::string>& params) {
    if (!m_connected) return;
    std::string line = EncodeCommand(command, params);
    SocketSendLine(line);
}

BCPResponse BCPClient::SendAndWait(const std::string& command,
                                   const std::map<std::string, std::string>& params,
                                   const std::string& waitForCommand) {
    BCPResponse empty;
    if (!m_connected) return empty;

    std::string line = EncodeCommand(command, params);
    fprintf(stderr, "[MPF] SendAndWait: sending '%s', waiting for '%s'\n", line.c_str(), waitForCommand.c_str());
    if (!SocketSendLine(line)) {
        fprintf(stderr, "[MPF] SendAndWait: SocketSendLine FAILED\n");
        Disconnect();
        return empty;
    }

    // Loop reading lines until we find the one we're waiting for
    std::string incoming;
    while (SocketReadLine(incoming)) {
        fprintf(stderr, "[MPF] SendAndWait: received '%s'\n", incoming.c_str());
        BCPResponse resp = DecodeLine(incoming);
        if (resp.command == waitForCommand) {
            fprintf(stderr, "[MPF] SendAndWait: matched response\n");
            return resp;
        }
        fprintf(stderr, "[MPF] SendAndWait: skipping non-matching line\n");
    }

    // Timeout or error
    fprintf(stderr, "[MPF] SendAndWait: TIMEOUT or socket error\n");
    Disconnect();
    return empty;
}

// ---------------------------------------------------------------------------
// Socket internals
// ---------------------------------------------------------------------------

bool BCPClient::SocketConnect(const std::string& host, int port) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    struct addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        return false;
    }

    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        socket_t s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == INVALID_SOCK) continue;

        if (connect(s, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
            m_socket = static_cast<decltype(m_socket)>(s);
            freeaddrinfo(result);
            return true;
        }
        CLOSE_SOCKET(s);
    }

    freeaddrinfo(result);
    return false;
}

void BCPClient::SocketClose() {
    if (m_socket != static_cast<decltype(m_socket)>(INVALID_SOCK)) {
        CLOSE_SOCKET(static_cast<socket_t>(m_socket));
        m_socket = static_cast<decltype(m_socket)>(INVALID_SOCK);
    }
}

bool BCPClient::SocketSendLine(const std::string& line) {
    std::string data = line + '\n';
    const char* ptr = data.c_str();
    size_t remaining = data.size();

    while (remaining > 0) {
        auto sent = send(static_cast<socket_t>(m_socket), ptr, static_cast<int>(remaining), 0);
        if (sent <= 0) return false;
        ptr += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

bool BCPClient::SocketReadLine(std::string& out) {
    // Check if we already have a complete line in the buffer
    size_t nlpos = m_readBuffer.find('\n');
    if (nlpos != std::string::npos) {
        out = m_readBuffer.substr(0, nlpos);
        m_readBuffer.erase(0, nlpos + 1);
        return true;
    }

    // Poll and read until we get a newline or timeout
#ifdef _WIN32
    WSAPOLLFD pfd{};
    pfd.fd = static_cast<SOCKET>(m_socket);
    pfd.events = POLLIN;
#else
    struct pollfd pfd{};
    pfd.fd = static_cast<int>(m_socket);
    pfd.events = POLLIN;
#endif

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        int remainingMs = m_timeoutMs - static_cast<int>(elapsed);
        if (remainingMs <= 0) return false;

#ifdef _WIN32
        int pollResult = WSAPoll(&pfd, 1, remainingMs);
#else
        int pollResult = poll(&pfd, 1, remainingMs);
#endif

        if (pollResult <= 0) return false; // timeout or error

        char buf[4096];
        auto n = recv(static_cast<socket_t>(m_socket), buf, sizeof(buf), 0);
        if (n <= 0) return false;

        m_readBuffer.append(buf, static_cast<size_t>(n));

        nlpos = m_readBuffer.find('\n');
        if (nlpos != std::string::npos) {
            out = m_readBuffer.substr(0, nlpos);
            m_readBuffer.erase(0, nlpos + 1);
            return true;
        }
    }
}

} // namespace MPF
