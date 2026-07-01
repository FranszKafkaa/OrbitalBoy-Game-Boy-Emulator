#include "mcp_server.h"

#include "mcp_prompts.h"
#include "mcp_resources.h"
#include "mcp_tools.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

void writeHeartbeat(const RunLabClient& client) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().controlQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return;
    }
    std::string writeError;
    (void)appendTextFile(queuePath.value(), "{\"command\":\"heartbeat\",\"frames\":1}\n", &writeError);
}

void writeFeedback(const RunLabClient& client, const std::string& type, const std::string& message, int promptId = 0) {
    std::string pathError;
    const auto feedbackPath = safeConfiguredPath(client.config().feedbackQueuePath, &pathError);
    if (!feedbackPath.has_value()) {
        return;
    }
    Json::Object obj;
    obj["type"] = type;
    obj["message"] = message;
    if (promptId > 0) {
        obj["prompt_id"] = promptId;
    }
    std::string writeError;
    (void)appendTextFile(feedbackPath.value(), Json(obj).dump() + "\n", &writeError);
}

std::string lowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

std::string promptText(const Json& prompt) {
    const Json* value = prompt.find("prompt");
    return value && value->isString() ? value->asString() : "";
}

bool appendControlCommands(const RunLabClient& client, const std::vector<std::string>& lines) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().controlQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return false;
    }
    std::string text;
    for (const auto& line : lines) {
        text += line;
        text += '\n';
    }
    std::string writeError;
    return appendTextFile(queuePath.value(), text, &writeError);
}

bool markPromptHandled(const RunLabClient& client, int promptId, const std::string& note) {
    std::string pathError;
    const auto promptPath = safeConfiguredPath(client.config().promptQueuePath, &pathError);
    if (!promptPath.has_value()) {
        return false;
    }
    Json::Object obj;
    obj["id"] = promptId;
    obj["status"] = "handled";
    obj["note"] = note;
    std::string writeError;
    return appendTextFile(promptPath.value(), Json(obj).dump() + "\n", &writeError);
}

bool autoRunPrompt(const RunLabClient& client, const Json& prompt, int promptId) {
    if (!client.config().autoRunPrompts) {
        return false;
    }
    const std::string text = lowerAscii(promptText(prompt));
    if (text.empty()) {
        return false;
    }

    std::vector<std::string> commands;
    std::string note;
    if (text.find("direita") != std::string::npos || text.find("right") != std::string::npos) {
        commands = {
            R"json({"command":"hold","buttons":["right"],"frames":90})json",
            R"json({"command":"release_all","buttons":[],"frames":1})json"
        };
        note = "auto-run: hold right";
    }
    if (text.find("pule") != std::string::npos
        || text.find("pular") != std::string::npos
        || text.find("jump") != std::string::npos) {
        commands = {
            R"json({"command":"tap","buttons":["a"],"frames":8})json",
            R"json({"command":"hold","buttons":["right"],"frames":45})json",
            R"json({"command":"release_all","buttons":[],"frames":1})json"
        };
        note = "auto-run: jump/right";
    }
    if (text.find("passe de fase") != std::string::npos
        || text.find("passa de fase") != std::string::npos
        || text.find("complete") != std::string::npos
        || text.find("finish") != std::string::npos) {
        commands = {
            R"json({"command":"hold","buttons":["right"],"frames":90})json",
            R"json({"command":"tap","buttons":["a"],"frames":8})json",
            R"json({"command":"hold","buttons":["right"],"frames":90})json",
            R"json({"command":"tap","buttons":["a"],"frames":8})json",
            R"json({"command":"hold","buttons":["right"],"frames":90})json",
            R"json({"command":"release_all","buttons":[],"frames":1})json"
        };
        note = "auto-run: try progress/right+jump";
    }

    if (commands.empty()) {
        return false;
    }
    if (!appendControlCommands(client, commands)) {
        writeFeedback(client, "prompt_error", "MCP could not queue controls for prompt #" + std::to_string(promptId), promptId);
        return true;
    }
    (void)markPromptHandled(client, promptId, note);
    writeFeedback(client, "prompt_autorun", "MCP auto-ran prompt #" + std::to_string(promptId) + ": " + note, promptId);
    return true;
}

std::string promptKey(const Json& prompt, int id) {
    const Json* frameValue = prompt.find("frame");
    const int frame = frameValue && frameValue->isNumber() ? frameValue->asInt(0) : 0;
    return std::to_string(id) + ":" + std::to_string(frame) + ":" + promptText(prompt);
}

void checkPromptQueueForFeedback(const RunLabClient& client, std::set<std::string>& announcedPrompts) {
    std::string pathError;
    const auto promptPath = safeConfiguredPath(client.config().promptQueuePath, &pathError);
    if (!promptPath.has_value()) {
        return;
    }
    std::string readError;
    const std::string text = readTextFile(promptPath.value(), &readError);
    if (!readError.empty()) {
        return;
    }
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto parsed = Json::parse(line);
        if (!parsed.has_value() || !parsed->isObject()) continue;
        const Json* idValue = parsed->find("id");
        const Json* statusValue = parsed->find("status");
        if (!idValue || !idValue->isNumber() || !statusValue || !statusValue->isString()) continue;
        const int id = idValue->asInt(0);
        if (id <= 0 || statusValue->asString() != "pending") continue;
        if (announcedPrompts.insert(promptKey(*parsed, id)).second) {
            writeFeedback(client, "prompt_seen", "MCP saw prompt #" + std::to_string(id), id);
            if (!autoRunPrompt(client, *parsed, id)) {
                writeFeedback(client, "prompt_waiting", "MCP needs AI client for prompt #" + std::to_string(id), id);
            }
        }
    }
}

} // namespace

McpServer::McpServer(RunLabClient client) : client_(std::move(client)) {}

int McpServer::run(std::istream& in, std::ostream& out, std::ostream& err) {
    writeHeartbeat(client_);
    std::atomic<bool> promptWatcherRunning{true};
    std::thread promptWatcher([&]() {
        std::set<std::string> announcedPrompts;
        while (promptWatcherRunning.load(std::memory_order_relaxed)) {
            checkPromptQueueForFeedback(client_, announcedPrompts);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    });
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        writeHeartbeat(client_);
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
    promptWatcherRunning.store(false, std::memory_order_relaxed);
    if (promptWatcher.joinable()) {
        promptWatcher.join();
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
