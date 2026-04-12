#pragma once

#include <string>
#include <functional>
#include <thread>
#include <atomic>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
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

using MockHandler = std::function<std::string(const std::string& line)>;

class MockBCPServer {
public:
    MockBCPServer() : m_listenSock(MOCK_INVALID_SOCK), m_clientSock(MOCK_INVALID_SOCK), m_port(0), m_running(false) {
#ifdef _WIN32
        WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }
    ~MockBCPServer() { Stop(); }

    int Start(MockHandler handler) {
        m_handler = handler;
        m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_listenSock == MOCK_INVALID_SOCK) return 0;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        if (bind(m_listenSock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
            MOCK_CLOSE_SOCKET(m_listenSock); m_listenSock = MOCK_INVALID_SOCK; return 0;
        }

        socklen_t addrLen = sizeof(addr);
        getsockname(m_listenSock, reinterpret_cast<struct sockaddr*>(&addr), &addrLen);
        m_port = ntohs(addr.sin_port);
        listen(m_listenSock, 1);

        m_running.store(true);
        m_thread = std::thread(&MockBCPServer::ServerLoop, this);
        return m_port;
    }

    void Stop() {
        m_running.store(false);
        if (m_listenSock != MOCK_INVALID_SOCK) { MOCK_CLOSE_SOCKET(m_listenSock); m_listenSock = MOCK_INVALID_SOCK; }
        if (m_clientSock != MOCK_INVALID_SOCK) { MOCK_CLOSE_SOCKET(m_clientSock); m_clientSock = MOCK_INVALID_SOCK; }
        if (m_thread.joinable()) m_thread.join();
    }

    int Port() const { return m_port; }

private:
    void ServerLoop() {
        m_clientSock = accept(m_listenSock, nullptr, nullptr);
        if (m_clientSock == MOCK_INVALID_SOCK) return;
        std::string buffer;
        char buf[4096];
        while (m_running.load()) {
            auto n = recv(m_clientSock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            buffer.append(buf, static_cast<size_t>(n));
            size_t nlpos;
            while ((nlpos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nlpos);
                buffer.erase(0, nlpos + 1);
                std::string response = m_handler(line);
                if (!response.empty()) {
                    response += '\n';
                    send(m_clientSock, response.c_str(), static_cast<int>(response.size()), 0);
                }
            }
        }
    }

    mock_socket_t m_listenSock, m_clientSock;
    int m_port;
    std::atomic<bool> m_running;
    std::thread m_thread;
    MockHandler m_handler;
};
