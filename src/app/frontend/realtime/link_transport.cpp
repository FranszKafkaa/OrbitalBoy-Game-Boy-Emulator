#include "gb/app/frontend/realtime/link_transport.hpp"

#include <array>
#include <cstring>

#ifdef __unix__
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gb::frontend {

namespace {

constexpr std::uint32_t kMagic = 0x4B4E494C; // LINK
constexpr std::uint8_t kTypeSerial = 1;
constexpr std::uint8_t kTypeNetplay = 2;

#pragma pack(push, 1)
struct LinkPacket {
    std::uint32_t magic = kMagic;
    std::uint8_t type = 0;
    std::uint64_t frame = 0;
    std::uint8_t value = 0;
};
#pragma pack(pop)

} // namespace

std::optional<LinkEndpoint> parseLinkEndpoint(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) {
        return std::nullopt;
    }
    LinkEndpoint out{};
    out.host = text.substr(0, colon);
    try {
        const int port = std::stoi(text.substr(colon + 1));
        if (port < 1 || port > 65535) {
            return std::nullopt;
        }
        out.port = static_cast<std::uint16_t>(port);
    } catch (...) {
        return std::nullopt;
    }
    return out;
}

UdpLinkTransport::UdpLinkTransport() = default;

UdpLinkTransport::~UdpLinkTransport() {
    close();
}

bool UdpLinkTransport::openHost(std::uint16_t port) {
#ifdef __unix__
    remoteKnown_ = false;
    return openSocket(port);
#else
    (void)port;
    return false;
#endif
}

bool UdpLinkTransport::openClient(const std::string& host, std::uint16_t port, std::uint16_t localPort) {
#ifdef __unix__
    if (!openSocket(localPort)) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close();
        return false;
    }

    std::memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    std::memcpy(&remoteAddr_, &addr, sizeof(addr));
    remoteAddrLen_ = sizeof(addr);
    remoteKnown_ = true;
    return true;
#else
    (void)host;
    (void)port;
    (void)localPort;
    return false;
#endif
}

void UdpLinkTransport::close() {
#ifdef __unix__
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
#endif
    remoteKnown_ = false;
    netplayInputs_.clear();
    lastRemoteInput_ = 0;
    lastRemoteSerial_ = 0xFF;
}

bool UdpLinkTransport::isOpen() const {
    return socketFd_ >= 0;
}

bool UdpLinkTransport::exchangeSerialByte(std::uint8_t outData, std::uint8_t& inData) {
    if (!isOpen()) {
        inData = 0xFF;
        return false;
    }
    sendPacket(kTypeSerial, 0, outData);
    drainIncoming();
    inData = lastRemoteSerial_;
    return true;
}

bool UdpLinkTransport::exchangeNetplayInput(
    std::uint64_t frame,
    std::uint8_t localInput,
    std::uint8_t& remoteInput,
    bool& predicted
) {
    if (!isOpen()) {
        remoteInput = 0;
        predicted = false;
        return false;
    }

    sendPacket(kTypeNetplay, frame, localInput);
    drainIncoming();

    const auto it = netplayInputs_.find(frame);
    if (it != netplayInputs_.end()) {
        remoteInput = it->second;
        lastRemoteInput_ = remoteInput;
        netplayInputs_.erase(it);
        predicted = false;
        return true;
    }

    remoteInput = lastRemoteInput_;
    predicted = true;
    return true;
}

bool UdpLinkTransport::openSocket(std::uint16_t localPort) {
#ifdef __unix__
    close();

    socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd_ < 0) {
        return false;
    }

    const int opt = 1;
    setsockopt(socketFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bindAddr.sin_port = htons(localPort);
    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) != 0) {
        close();
        return false;
    }

    const int flags = fcntl(socketFd_, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);
    }

    return true;
#else
    (void)localPort;
    return false;
#endif
}

void UdpLinkTransport::drainIncoming() {
#ifdef __unix__
    if (socketFd_ < 0) {
        return;
    }

    while (true) {
        LinkPacket packet{};
        sockaddr_storage from{};
        socklen_t fromLen = sizeof(from);
        const ssize_t received = recvfrom(
            socketFd_,
            &packet,
            sizeof(packet),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromLen
        );
        if (received < static_cast<ssize_t>(sizeof(packet))) {
            break;
        }

        if (packet.magic != kMagic) {
            continue;
        }

        if (!remoteKnown_) {
            std::memcpy(&remoteAddr_, &from, sizeof(from));
            remoteAddrLen_ = fromLen;
            remoteKnown_ = true;
        }

        if (packet.type == kTypeSerial) {
            lastRemoteSerial_ = packet.value;
        } else if (packet.type == kTypeNetplay) {
            netplayInputs_[packet.frame] = packet.value;
        }
    }
#endif
}

bool UdpLinkTransport::sendPacket(std::uint8_t type, std::uint64_t frame, std::uint8_t value) {
#ifdef __unix__
    if (socketFd_ < 0 || !remoteKnown_) {
        return false;
    }
    LinkPacket packet{};
    packet.type = type;
    packet.frame = frame;
    packet.value = value;

    const ssize_t sent = sendto(
        socketFd_,
        &packet,
        sizeof(packet),
        0,
        reinterpret_cast<const sockaddr*>(&remoteAddr_),
        remoteAddrLen_
    );
    return sent == static_cast<ssize_t>(sizeof(packet));
#else
    (void)type;
    (void)frame;
    (void)value;
    return false;
#endif
}

} // namespace gb::frontend
