#include "mcp_server.h"
#include "runlab_client.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

void runSafePathTests();
void runToolTests();

namespace {

std::filesystem::path testDir() {
    const auto dir = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests";
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path writeState(const std::string& name, const std::string& text) {
    const auto path = testDir() / name;
    std::ofstream out(path, std::ios::trunc);
    out << text;
    return path;
}

std::string sampleState() {
    return R"json({
  "status": { "rom_name": "test.gb", "frame": 42, "running": true, "paused": false },
  "entities": [
    { "label": "player", "type": "Player", "oam_indices": [0], "last_bbox": { "x": 10, "y": 20, "w": 8, "h": 8 } },
    { "label": "enemy_1", "type": "Enemy", "oam_indices": [1], "last_bbox": { "x": 40, "y": 20, "w": 8, "h": 8 } }
  ],
  "memory_labels": [
    { "label": "player.x", "address": "0xC001", "type": "u8", "entity": "player", "field": "x", "current_value": 10 },
    { "label": "level_id", "address": "0xC100", "type": "u8", "current_value": 2 }
  ],
  "events": [
    { "frame": 1, "type": "value_increased", "label": "player.x", "previous": 9, "current": 10 },
    { "frame": 2, "type": "split_candidate", "label": "level_id", "previous": 1, "current": 2 }
  ],
  "goal": { "active": true, "name": "finish_current_level" },
  "splits": [{ "name": "level_1", "triggered": true, "triggered_frame": 2 }],
  "profile": { "game": { "name": "test.gb" }, "memory_labels": [{ "label": "player.x", "address": "0xC001", "type": "u8" }] }
})json";
}

void runStateTests() {
    orbitalboy::mcp::ServerConfig cfg;
    cfg.statePath = writeState("state.json", sampleState());
    const auto state = orbitalboy::mcp::RunLabState::load(cfg);
    assert(state.ok());
    assert(state.status().find("rom_name")->asString() == "test.gb");
    assert(state.entities("Player").asArray().size() == 1);
    assert(state.memoryLabels("player").asArray().size() == 1);
    assert(state.events(1).asArray().size() == 1);
    assert(state.events(20, "split_candidate").asArray().size() == 1);
    assert(state.activeGoal().find("name")->asString() == "finish_current_level");
    assert(state.splits().asArray().size() == 1);

    const auto summary = state.analysisSummary();
    assert(summary.find("has_player_entity")->asBool(false));
    assert(summary.find("has_player_x")->asBool(false));
    assert(!summary.find("has_player_y")->asBool(true));

    const auto profile = state.profileJson(cfg, false);
    assert(profile.has_value());
    assert(profile->find("\"player.x\"") != std::string::npos);

    orbitalboy::mcp::ServerConfig missing;
    missing.statePath = testDir() / "missing.json";
    const auto missingState = orbitalboy::mcp::RunLabState::load(missing);
    assert(!missingState.ok());
}

void runServerTests() {
    orbitalboy::mcp::ServerConfig cfg;
    cfg.statePath = writeState("server-state.json", sampleState());
    orbitalboy::mcp::McpServer server{orbitalboy::mcp::RunLabClient(cfg)};

    bool respond = false;
    const auto initRequest = *orbitalboy::mcp::Json::parse(R"json({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})json");
    const auto init = server.handleRequest(initRequest, &respond);
    assert(respond);
    assert(init.find("result")->find("protocolVersion")->asString() == "2025-06-18");

    const auto badMethod = *orbitalboy::mcp::Json::parse(R"json({"jsonrpc":"2.0","id":"x","method":"not_real","params":{}})json");
    const auto bad = server.handleRequest(badMethod, &respond);
    assert(bad.find("error")->find("code")->asInt() == orbitalboy::mcp::kMethodNotFound);

    const auto badTool = *orbitalboy::mcp::Json::parse(R"json({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{}})json");
    const auto badToolResponse = server.handleRequest(badTool, &respond);
    assert(badToolResponse.find("error")->find("code")->asInt() == orbitalboy::mcp::kInvalidParams);
}

} // namespace

int main() {
    runStateTests();
    runServerTests();
    runSafePathTests();
    runToolTests();
    std::cout << "orbitalboy_mcp_tests passed\n";
    return 0;
}
