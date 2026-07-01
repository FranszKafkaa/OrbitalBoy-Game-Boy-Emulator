#include "mcp_prompts.h"
#include "mcp_resources.h"
#include "mcp_tools.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {

std::filesystem::path writeToolState() {
    const auto dir = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "tool-state.json";
    std::ofstream out(path, std::ios::trunc);
    out << R"json({
  "status": { "rom_name": "tool.gb", "frame": 7, "running": true, "paused": false, "screenshot_path": "/tmp/orbitalboy_mcp_tests/current-screen.ppm" },
  "entities": [{ "label": "player", "type": "Player" }],
  "memory_labels": [{ "label": "player.x", "address": "0xC001", "type": "u8", "entity": "player", "field": "x" }],
  "events": [{ "frame": 7, "type": "value_increased", "label": "player.x" }],
  "control": { "enabled": true, "client_recently_seen": true, "pending_count": 0, "current_command": "" },
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

    assert(orbitalboy::mcp::toolsList().find("tools")->asArray().size() == 22);
    assert(orbitalboy::mcp::resourcesList().find("resources")->asArray().size() == 10);
    assert(orbitalboy::mcp::promptsList().find("prompts")->asArray().size() == 8);

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

    const auto visualPrompt = orbitalboy::mcp::getPrompt("identify_screen_objects", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(visualPrompt.find("messages")->asArray().front().find("content")->find("text")->asString().find("runlab_visual_annotate") != std::string::npos);

    const auto boxPrompt = orbitalboy::mcp::getPrompt("handle_prompt_box", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(boxPrompt.find("messages")->asArray().front().find("content")->find("text")->asString().find("runlab_get_pending_prompt") != std::string::npos);

    const auto controlContext = orbitalboy::mcp::callTool(client, "runlab_get_control_context", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(toolText(controlContext).find("\"client_recently_seen\": true") != std::string::npos);

    const auto visualContext = orbitalboy::mcp::callTool(client, "runlab_get_visual_context", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(toolText(visualContext).find("current-screen.ppm") != std::string::npos);
    assert(toolText(visualContext).find("runlab_visual_annotate") != std::string::npos);

    orbitalboy::mcp::callTool(client, "nope", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(invalid);

    cfg.controlQueuePath = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests" / "commands.jsonl";
    cfg.promptQueuePath = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests" / "prompts.jsonl";
    cfg.feedbackQueuePath = std::filesystem::temp_directory_path() / "orbitalboy_mcp_tests" / "feedback.jsonl";
    {
        std::ofstream clear(cfg.controlQueuePath, std::ios::trunc);
    }
    {
        std::ofstream prompts(cfg.promptQueuePath, std::ios::trunc);
        prompts << "{\"id\":7,\"frame\":12,\"status\":\"pending\",\"prompt\":\"jump over enemy\"}\n";
    }
    {
        std::ofstream feedback(cfg.feedbackQueuePath, std::ios::trunc);
    }
    orbitalboy::mcp::RunLabClient controlClient(cfg);
    const auto pendingPrompt = orbitalboy::mcp::callTool(controlClient, "runlab_get_pending_prompt", orbitalboy::mcp::Json::Object{}, &invalid);
    assert(!invalid);
    assert(toolText(pendingPrompt).find("jump over enemy") != std::string::npos);

    const auto control = orbitalboy::mcp::callTool(
        controlClient,
        "runlab_control_hold",
        orbitalboy::mcp::Json::Object{
            {"buttons", orbitalboy::mcp::Json::Array{"right", "a"}},
            {"frames", 12}
        },
        &invalid
    );
    assert(!invalid);
    assert(toolText(control).find("commands.jsonl") != std::string::npos);

    std::ifstream queue(cfg.controlQueuePath);
    std::string line;
    std::getline(queue, line);
    assert(line.find("\"command\":\"hold\"") != std::string::npos);
    assert(line.find("\"right\"") != std::string::npos);
    assert(line.find("\"a\"") != std::string::npos);

    const auto step = orbitalboy::mcp::callTool(
        controlClient,
        "runlab_control_step_frame",
        orbitalboy::mcp::Json::Object{{"frames", 3}},
        &invalid
    );
    assert(!invalid);
    assert(toolText(step).find("\"command\": \"step_frame\"") != std::string::npos);

    const auto annotation = orbitalboy::mcp::callTool(
        controlClient,
        "runlab_visual_annotate",
        orbitalboy::mcp::Json::Object{
            {"label", "PLAYER"},
            {"type", "player"},
            {"x", 20},
            {"y", 30},
            {"w", 16},
            {"h", 24},
            {"frames", 120}
        },
        &invalid
    );
    assert(!invalid);
    assert(toolText(annotation).find("\"command\": \"annotation\"") != std::string::npos);

    const auto ackPrompt = orbitalboy::mcp::callTool(
        controlClient,
        "runlab_ack_prompt",
        orbitalboy::mcp::Json::Object{{"id", 7}, {"note", "tested"}},
        &invalid
    );
    assert(!invalid);
    assert(toolText(ackPrompt).find("\"handled\": true") != std::string::npos);

    const auto macro = orbitalboy::mcp::callTool(
        controlClient,
        "runlab_control_macro",
        orbitalboy::mcp::Json::Object{{
            "commands",
            orbitalboy::mcp::Json::Array{
                orbitalboy::mcp::Json::Object{{"command", "pause"}},
                orbitalboy::mcp::Json::Object{{"command", "tap"}, {"buttons", orbitalboy::mcp::Json::Array{"a"}}, {"frames", 4}},
                orbitalboy::mcp::Json::Object{{"command", "step_frame"}, {"frames", 2}}
            }
        }},
        &invalid
    );
    assert(!invalid);
    assert(toolText(macro).find("\"command\": \"tap\"") != std::string::npos);

    std::ifstream finalQueue(cfg.controlQueuePath);
    std::string queueText((std::istreambuf_iterator<char>(finalQueue)), std::istreambuf_iterator<char>());
    assert(queueText.find("\"command\":\"annotation\"") != std::string::npos);
    assert(queueText.find("\"label\":\"PLAYER\"") != std::string::npos);

    std::ifstream promptQueue(cfg.promptQueuePath);
    std::string promptText((std::istreambuf_iterator<char>(promptQueue)), std::istreambuf_iterator<char>());
    assert(promptText.find("\"status\":\"handled\"") != std::string::npos);

    std::ifstream feedbackQueue(cfg.feedbackQueuePath);
    std::string feedbackText((std::istreambuf_iterator<char>(feedbackQueue)), std::istreambuf_iterator<char>());
    assert(feedbackText.find("MCP read prompt #7") != std::string::npos);
    assert(feedbackText.find("MCP done: tested") != std::string::npos);
}
