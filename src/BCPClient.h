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

    BCPResponse SendAndWait(const std::string& command,
                            const std::map<std::string, std::string>& params,
                            const std::string& waitForCommand);

    void Send(const std::string& command,
              const std::map<std::string, std::string>& params);

    void SetTimeout(int timeoutMs) { m_timeoutMs = timeoutMs; }

    // Exposed for testing — pure functions.
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
