#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#ifdef __unix__
#include <sys/socket.h>
#endif

namespace gb::frontend {

struct LinkEndpoint {
    std::string host{};
    std::uint16_t port = 0;
};

std::optional<LinkEndpoint> parseLinkEndpoint(const std::string& text);

class UdpLinkTransport {
public:
    UdpLinkTransport();
    ~UdpLinkTransport();

    bool openHost(std::uint16_t port);
    bool openClient(const std::string& host, std::uint16_t port, std::uint16_t localPort = 0);
    void close();
    [[nodiscard]] bool isOpen() const;

    bool exchangeSerialByte(std::uint8_t outData, std::uint8_t& inData);

    // Lockstep base: envia input local do frame e tenta receber input remoto.
    // Quando nao chega a tempo, retorna ultimo input remoto e marca predicted=true.
    bool exchangeNetplayInput(std::uint64_t frame, std::uint8_t localInput, std::uint8_t& remoteInput, bool& predicted);

private:
    bool openSocket(std::uint16_t localPort);
    void drainIncoming();
    bool sendPacket(std::uint8_t type, std::uint64_t frame, std::uint8_t value);

    int socketFd_ = -1;
    bool remoteKnown_ = false;

    std::uint8_t lastRemoteInput_ = 0;
    std::uint8_t lastRemoteSerial_ = 0xFF;
    std::unordered_map<std::uint64_t, std::uint8_t> netplayInputs_{};

#ifdef __unix__
    struct sockaddr_storage remoteAddr_{};
    socklen_t remoteAddrLen_ = 0;
#endif
};

} // namespace gb::frontend
