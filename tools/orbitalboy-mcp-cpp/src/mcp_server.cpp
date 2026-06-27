#include "mcp_server.h"

#include "mcp_prompts.h"
#include "mcp_resources.h"
#include "mcp_tools.h"

#include <iostream>
#include <string>
#include <utility>

namespace orbitalboy::mcp {

namespace {

Json nullId() { return Json(nullptr); }

Json requestId(const Json& request) {
    const Json* id = request.find("id");
    return id ? *id : nullId();
}

std::string stringField(const Json& obj, const std::string& key) {
    const Json* value = obj.find(key);
    return value && value->isString() ? value->asString() : "";
}

Json objectField(const Json& obj, const std::string& key) {
    const Json* value = obj.find(key);
    return value && value->isObject() ? *value : Json::Object{};
}

Json initializeResult() {
    Json::Object serverInfo;
    serverInfo["name"] = "orbitalboy-runlab";
    serverInfo["version"] = "0.1.0";

    Json::Object capabilities;
    capabilities["tools"] = Json::Object{};
    capabilities["resources"] = Json::Object{};
    capabilities["prompts"] = Json::Object{};

    Json::Object result;
    result["protocolVersion"] = "2025-06-18";
    result["capabilities"] = Json(capabilities);
    result["serverInfo"] = Json(serverInfo);
    return Json(result);
}

} // namespace

McpServer::McpServer(RunLabClient client) : client_(std::move(client)) {}

int McpServer::run(std::istream& in, std::ostream& out, std::ostream& err) {
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::string parseError;
        auto request = Json::parse(line, &parseError);
        if (!request.has_value()) {
            out << jsonRpcError(nullId(), kParseError, "Parse error: " + parseError).dump() << '\n';
            out.flush();
            continue;
        }
        bool shouldRespond = true;
        Json response = handleRequest(*request, &shouldRespond);
        if (shouldRespond) {
            out << response.dump() << '\n';
            out.flush();
        }
    }
    err << "orbitalboy-mcp: stdin closed\n";
    return 0;
}

Json McpServer::handleRequest(const Json& request, bool* shouldRespond) {
    if (shouldRespond) *shouldRespond = true;
    if (!request.isObject()) {
        return jsonRpcError(nullId(), kInvalidRequest, "Invalid JSON-RPC request");
    }

    const Json id = requestId(request);
    const bool notification = request.find("id") == nullptr;
    if (shouldRespond) *shouldRespond = !notification;

    if (stringField(request, "jsonrpc") != "2.0" || stringField(request, "method").empty()) {
        if (notification) return Json();
        return jsonRpcError(id, kInvalidRequest, "Invalid JSON-RPC request");
    }

    const std::string method = stringField(request, "method");
    const Json params = objectField(request, "params");

    if (method == "notifications/initialized") {
        if (shouldRespond) *shouldRespond = false;
        return Json();
    }
    if (method == "initialize") return jsonRpcResult(id, initializeResult());
    if (method == "ping") return jsonRpcResult(id, Json::Object{});
    if (method == "tools/list") return jsonRpcResult(id, toolsList());
    if (method == "resources/list") return jsonRpcResult(id, resourcesList());
    if (method == "prompts/list") return jsonRpcResult(id, promptsList());

    if (method == "tools/call") {
        const std::string name = stringField(params, "name");
        const Json arguments = objectField(params, "arguments");
        if (name.empty()) return jsonRpcError(id, kInvalidParams, "tools/call requires params.name");
        bool invalidParams = false;
        Json result = callTool(client_, name, arguments, &invalidParams);
        return invalidParams ? jsonRpcError(id, kInvalidParams, "Unknown tool or invalid arguments") : jsonRpcResult(id, result);
    }

    if (method == "resources/read") {
        const std::string uri = stringField(params, "uri");
        if (uri.empty()) return jsonRpcError(id, kInvalidParams, "resources/read requires params.uri");
        bool invalidParams = false;
        Json result = readResource(client_, uri, &invalidParams);
        return invalidParams ? jsonRpcError(id, kInvalidParams, "Unknown resource URI") : jsonRpcResult(id, result);
    }

    if (method == "prompts/get") {
        const std::string name = stringField(params, "name");
        const Json arguments = objectField(params, "arguments");
        if (name.empty()) return jsonRpcError(id, kInvalidParams, "prompts/get requires params.name");
        bool invalidParams = false;
        Json result = getPrompt(name, arguments, &invalidParams);
        return invalidParams ? jsonRpcError(id, kInvalidParams, "Unknown prompt") : jsonRpcResult(id, result);
    }

    if (notification) return Json();
    return jsonRpcError(id, kMethodNotFound, "Method not found");
}

} // namespace orbitalboy::mcp
