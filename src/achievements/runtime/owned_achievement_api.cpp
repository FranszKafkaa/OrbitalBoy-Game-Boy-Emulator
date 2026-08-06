#include "gb/achievements/runtime/owned_achievement_api.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include "gb/achievements/protocol/json_parser.hpp"

namespace gb::achievements {
namespace {

const protocol::JsonValue* member(const protocol::JsonValue& object, std::string_view name) {
    const auto lookup = object.findUniqueMember(name);
    return lookup.kind == protocol::JsonMemberLookupKind::Unique ? lookup.value : nullptr;
}

std::string stringField(const protocol::JsonValue& object, std::string_view name) {
    const auto* value = member(object, name);
    return value != nullptr && value->string() != nullptr ? *value->string() : std::string{};
}

std::uint32_t uintField(const protocol::JsonValue& object, std::string_view name) {
    const auto* value = member(object, name);
    std::uint64_t parsed = 0U;
    if (value == nullptr || !value->toUint64(parsed)) return 0U;
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(parsed, 0xFFFFFFFFULL));
}

} // namespace

OwnedAchievementApi::OwnedAchievementApi(protocol::HttpTransport& transport, std::string endpointBase)
    : transport_(transport), endpointBase_(std::move(endpointBase)) {}

OwnedApiResult OwnedAchievementApi::loginToken(std::string username, SecretString token) {
    return request("/login", std::move(username), std::move(token), true);
}

OwnedApiResult OwnedAchievementApi::loginPassword(std::string username, SecretString password) {
    return request("/login", std::move(username), std::move(password), false);
}

OwnedApiResult OwnedAchievementApi::request(std::string endpoint, std::string username, SecretString secret, bool token) {
    OwnedApiResult result;
    if (endpointBase_.empty() || username.empty() || secret.empty()) {
        result.error = "credentials missing";
        return result;
    }
    protocol::HttpRequest request;
    request.channel = protocol::HttpChannel::Api;
    request.url = endpointBase_ + std::move(endpoint);
    request.postData = "username=" + username + (token ? "&token=" : "&password=") + std::string(secret.view());
    if (!transport_.submit(std::move(request))) {
        secret.clear();
        result.error = "request rejected";
        return result;
    }
    secret.clear();
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto responses = transport_.takeCompleted(protocol::HttpChannel::Api);
        if (responses.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto& response = responses.front();
        if (response.statusCode < 200L || response.statusCode >= 300L || !response.error.empty()) {
            result.error = response.error.empty() ? "http error" : response.error;
            return result;
        }
        const auto parsed = protocol::JsonParser::parse(response.body.data(), response.body.size());
        if (!parsed.ok() || parsed.value().type() != protocol::JsonValueType::Object) {
            result.error = "malformed response";
            return result;
        }
        const auto& object = parsed.value();
        result.user.username = stringField(object, "username");
        result.user.displayName = stringField(object, "displayName");
        if (result.user.displayName.empty()) result.user.displayName = stringField(object, "display_name");
        result.user.scoreHardcore = uintField(object, "scoreHardcore");
        result.user.scoreCasual = uintField(object, "scoreCasual");
        result.user.unreadMessages = uintField(object, "unreadMessages");
        result.game.gameId = uintField(object, "gameId");
        result.game.title = stringField(object, "gameTitle");
        if (result.user.username.empty()) result.user.username = username;
        result.ok = true;
        return result;
    }
    result.error = "request timeout";
    return result;
}

} // namespace gb::achievements
