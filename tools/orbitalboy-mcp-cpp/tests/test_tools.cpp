#include "mcp_prompts.h"
#include "mcp_resources.h"
#include "mcp_tools.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path writeToolState() {
    const auto dir = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "tool-state.json";
    std::ofstream out(path, std::ios::trunc);
    out << R"json({
  "status": { "rom_name": "tool.gb", "frame": 7, "running": true, "paused": false },
  "entities": [{ "label": "player", "type": "Player" }],
  "memory_labels": [{ "label": "player.x", "address": "0xC001", "type": "u8", "entity": "player", "field": "x" }],
  "events": [{ "frame": 7, "type": "value_increased", "label": "player.x" }],
  "goal": { "active": false },
  "splits": []
})json";
    return path;
}

std::string toolText(const orbitalboy::mcp::Json& result) {
    const auto& content = result.find("content")->asArray();
    return content.front().find("text")->asString();
}

} // namespace

void runToolTests() {
    orbitalboy::mcp::ServerConfig cfg;
    cfg.statePath = writeToolState();
    orbitalboy::mcp::RunLabClient client(cfg);

    assert(orbitalboy::mcp::toolsList().find("tools")->asArray().size() == 8);
    assert(orbitalboy::mcp::resourcesList().find("resources")->asArray().size() == 8);
    assert(orbitalboy::mcp::promptsList().find("prompts")->asArray().size() == 5);

    bool invalid = true;
    const auto status = orbitalboy::mcp::callTool(client, "runlab_get_status", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(toolText(status).find("tool.gb") != std::string::npos);

    const auto entities = orbitalboy::mcp::callTool(
        client,
        "runlab_list_entities",
        orbitalboy::mcp::Json::Object{{"typeFilter", "Player"}},
        &invalid
    );
    assert(!invalid);
    assert(toolText(entities).find("player") != std::string::npos);

    const auto resource = orbitalboy::mcp::readResource(client, "runlab://events/recent", &invalid);
    assert(!invalid);
    assert(resource.find("contents")->asArray().front().find("text")->asString().find("player.x") != std::string::npos);

    const auto prompt = orbitalboy::mcp::getPrompt("suggest_missing_memory_labels", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(prompt.find("messages")->asArray().front().find("content")->find("text")->asString().find("player.y") != std::string::npos);

    orbitalboy::mcp::callTool(client, "nope", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(invalid);
}
