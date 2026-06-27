#pragma once

#include "runlab_client.h"

#include <istream>
#include <ostream>

namespace orbitalboy::mcp {

class McpServer {
public:
    explicit McpServer(RunLabClient client);

    int run(std::istream& in, std::ostream& out, std::ostream& err);
    Json handleRequest(const Json& request, bool* shouldRespond);

private:
    RunLabClient client_;
};

} // namespace orbitalboy::mcp
