#pragma once

#include "runlab_state.h"

namespace orbitalboy::mcp {

class RunLabClient {
public:
    explicit RunLabClient(ServerConfig config);
    [[nodiscard]] const ServerConfig& config() const;
    [[nodiscard]] RunLabState loadState() const;

private:
    ServerConfig config_;
};

} // namespace orbitalboy::mcp
