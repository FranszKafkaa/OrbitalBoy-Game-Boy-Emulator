#pragma once

#include "runlab_client.h"

namespace orbitalboy::mcp {

Json resourcesList();
Json readResource(const RunLabClient& client, const std::string& uri, bool* invalidParams = nullptr);

} // namespace orbitalboy::mcp
