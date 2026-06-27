#include "mcp_server.h"
#include "safe_paths.h"

#include <iostream>

int main(int argc, char** argv) {
    const orbitalboy::mcp::ServerConfig config = orbitalboy::mcp::parseConfig(argc, argv);
    orbitalboy::mcp::McpServer server{orbitalboy::mcp::RunLabClient(config)};
    return server.run(std::cin, std::cout, std::cerr);
}
