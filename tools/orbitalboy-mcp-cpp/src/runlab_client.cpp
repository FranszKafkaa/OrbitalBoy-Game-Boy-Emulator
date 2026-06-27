#include "runlab_client.h"

#include <utility>

namespace orbitalboy::mcp {

RunLabClient::RunLabClient(ServerConfig config) : config_(std::move(config)) {}

const ServerConfig& RunLabClient::config() const {
    return config_;
}

RunLabState RunLabClient::loadState() const {
    return RunLabState::load(config_);
}

} // namespace orbitalboy::mcp
