#pragma once

#include "runlab_client.h"

namespace orbitalboy::mcp {

Json toolsList();
Json callTool(const RunLabClient& client, const std::string& name, const Json& arguments, bool* invalidParams = nullptr);

} // namespace orbitalboy::mcp
