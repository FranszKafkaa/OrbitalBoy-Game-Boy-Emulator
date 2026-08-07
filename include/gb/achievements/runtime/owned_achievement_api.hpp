#pragma once

#include <cstdint>
#include <string>

#include "gb/achievements/protocol/http_transport.hpp"
#include "gb/achievements/runtime/models.hpp"
#include "gb/achievements/security/secret_string.hpp"

namespace gb::achievements {

struct OwnedApiResult {
    bool ok = false;
    UserSummary user{};
    GameSummary game{};
    std::string error{};
};

class OwnedAchievementApi {
public:
    OwnedAchievementApi(protocol::HttpTransport& transport, std::string endpointBase);

    [[nodiscard]] OwnedApiResult loginToken(std::string username, SecretString token);
    [[nodiscard]] OwnedApiResult loginPassword(std::string username, SecretString password);

private:
    OwnedApiResult request(std::string endpoint, std::string username, SecretString secret, bool token);
    protocol::HttpTransport& transport_;
    std::string endpointBase_;
};

} // namespace gb::achievements
