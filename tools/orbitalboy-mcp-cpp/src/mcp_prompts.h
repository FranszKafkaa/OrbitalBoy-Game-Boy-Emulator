#pragma once

#include "json_rpc.h"

namespace orbitalboy::mcp {

Json promptsList();
Json getPrompt(const std::string& name, const Json& arguments, bool* invalidParams = nullptr);

} // namespace orbitalboy::mcp
