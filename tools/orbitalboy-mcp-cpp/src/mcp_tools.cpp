#include "mcp_tools.h"

#include <algorithm>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace orbitalboy::mcp {

namespace {

Json schema(Json::Object properties = {}) {
    Json::Object obj;
    obj["type"] = "object";
    obj["properties"] = Json(std::move(properties));
    return Json(obj);
}

Json tool(const char* name, const char* title, const char* description, Json inputSchema) {
    Json::Object obj;
    obj["name"] = name;
    obj["title"] = title;
    obj["description"] = description;
    obj["inputSchema"] = inputSchema;
    return Json(obj);
}

Json stringArrayProperty(const char* description) {
    Json::Object items;
    items["type"] = "string";
    Json::Object prop;
    prop["type"] = "array";
    prop["items"] = Json(items);
    prop["description"] = description;
    return Json(prop);
}

Json loadOrError(const RunLabClient& client, RunLabState& state) {
    state = client.loadState();
    if (!state.ok()) {
        return textToolResult("RunLab state unavailable: " + state.error());
    }
    return Json();
}

std::string argString(const Json& args, const std::string& key) {
    const Json* value = args.find(key);
    return value && value->isString() ? value->asString() : "";
}

int argLimit(const Json& args) {
    const Json* value = args.find("limit");
    if (!value || !value->isNumber()) return 20;
    const int raw = value->asInt(20);
    return raw < 1 ? 1 : (raw > 100 ? 100 : raw);
}

int argId(const Json& args) {
    const Json* value = args.find("id");
    if (!value || !value->isNumber()) return 0;
    return std::max(0, value->asInt(0));
}

int argFrames(const Json& args, int fallback) {
    const Json* value = args.find("frames");
    if (!value || !value->isNumber()) return fallback;
    return std::clamp(value->asInt(fallback), 1, 600);
}

int argClampedInt(const Json& args, const std::string& key, int fallback, int minValue, int maxValue) {
    const Json* value = args.find(key);
    if (!value || !value->isNumber()) return fallback;
    return std::clamp(value->asInt(fallback), minValue, maxValue);
}

std::vector<std::string> argButtons(const Json& args) {
    std::vector<std::string> out;
    const Json* buttons = args.find("buttons");
    if (!buttons || !buttons->isArray()) return out;
    for (const auto& button : buttons->asArray()) {
        if (button.isString()) out.push_back(button.asString());
    }
    return out;
}

std::vector<std::string> buttonsFromValue(const Json* buttons) {
    std::vector<std::string> out;
    if (!buttons || !buttons->isArray()) return out;
    for (const auto& button : buttons->asArray()) {
        if (button.isString()) out.push_back(button.asString());
    }
    return out;
}

int framesFromValue(const Json* value, int fallback) {
    if (!value || !value->isNumber()) return fallback;
    return std::clamp(value->asInt(fallback), 1, 600);
}

std::string canonicalControlCommand(const std::string& raw) {
    if (raw == "release") return "release_all";
    if (raw == "advance") return "advance_frames";
    if (raw == "step" || raw == "step_frames") return "step_frame";
    return raw;
}

bool commandNeedsButtons(const std::string& command) {
    return command == "tap" || command == "hold";
}

bool commandAllowsNoButtons(const std::string& command) {
    return command == "release_all"
        || command == "advance_frames"
        || command == "pause"
        || command == "resume"
        || command == "step_frame";
}

int defaultFramesForCommand(const std::string& command) {
    if (command == "tap") return 6;
    if (command == "hold") return 30;
    return 1;
}

std::string commandJson(const std::string& command, const std::vector<std::string>& buttons, int frames) {
    Json::Array buttonValues;
    for (const auto& button : buttons) {
        buttonValues.push_back(button);
    }
    Json::Object obj;
    obj["command"] = command;
    obj["buttons"] = Json(buttonValues);
    obj["frames"] = frames;
    return Json(obj).dump();
}

std::string annotationJson(const std::string& label, const std::string& type, int x, int y, int w, int h, int frames) {
    Json::Object obj;
    obj["command"] = "annotation";
    obj["label"] = label.empty() ? "OBJECT" : label;
    obj["type"] = type.empty() ? "object" : type;
    obj["x"] = std::clamp(x, 0, 159);
    obj["y"] = std::clamp(y, 0, 143);
    obj["w"] = std::clamp(w, 1, 160);
    obj["h"] = std::clamp(h, 1, 144);
    obj["frames"] = std::clamp(frames, 1, 600);
    return Json(obj).dump();
}

void appendFeedback(const RunLabClient& client, const std::string& type, const std::string& message, int promptId = 0) {
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

Json enqueueControlLines(const RunLabClient& client, const Json::Array& commands) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().controlQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return textToolResult("RunLab control queue unavailable: " + pathError);
    }

    std::string text;
    Json::Array queued;
    int count = 0;
    for (const auto& item : commands) {
        if (!item.isObject()) {
            return textToolResult("Invalid macro command: each item must be an object.");
        }
        if (++count > 32) {
            return textToolResult("Invalid macro command: max 32 commands per macro.");
        }
        const Json* commandValue = item.find("command");
        if (!commandValue || !commandValue->isString()) {
            return textToolResult("Invalid macro command: missing command.");
        }
        const std::string command = canonicalControlCommand(commandValue->asString());
        if (!commandNeedsButtons(command) && !commandAllowsNoButtons(command)) {
            return textToolResult("Invalid macro command: unknown command " + command + ".");
        }
        const auto buttons = buttonsFromValue(item.find("buttons"));
        if (commandNeedsButtons(command) && buttons.empty()) {
            return textToolResult("Invalid macro command: " + command + " requires buttons.");
        }
        const int frames = framesFromValue(item.find("frames"), defaultFramesForCommand(command));
        text += commandJson(command, buttons, frames);
        text += '\n';

        Json::Object out;
        out["command"] = command;
        out["frames"] = frames;
        Json::Array buttonValues;
        for (const auto& button : buttons) buttonValues.push_back(button);
        out["buttons"] = Json(buttonValues);
        queued.push_back(Json(out));
    }

    std::string writeError;
    if (!appendTextFile(queuePath.value(), text, &writeError)) {
        return textToolResult("RunLab control macro failed: " + writeError);
    }
    Json::Object result;
    result["queued"] = Json(queued);
    result["queue_path"] = queuePath->string();
    result["note"] = "Macro queued. Emulator must have RunLab MCP bridge enabled.";
    return textToolResult(Json(result).dump(2));
}

Json enqueueControlCommand(const RunLabClient& client, const std::string& command, const std::vector<std::string>& buttons, int frames) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().controlQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return textToolResult("RunLab control queue unavailable: " + pathError);
    }
    std::string writeError;
    const std::string line = commandJson(command, buttons, frames) + "\n";
    if (!appendTextFile(queuePath.value(), line, &writeError)) {
        return textToolResult("RunLab control command failed: " + writeError);
    }
    Json::Object out;
    out["queued"] = true;
    out["queue_path"] = queuePath->string();
    out["command"] = command;
    out["frames"] = frames;
    out["buttons"] = Json::Array{};
    if (!buttons.empty()) {
        Json::Array arr;
        for (const auto& button : buttons) arr.push_back(button);
        out["buttons"] = Json(arr);
    }
    out["note"] = "Emulator must be running with --runlab-control to consume this queue.";
    return textToolResult(Json(out).dump(2));
}

Json enqueueVisualAnnotation(const RunLabClient& client, const Json& arguments) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().controlQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return textToolResult("RunLab control queue unavailable: " + pathError);
    }
    const std::string label = argString(arguments, "label").empty() ? "OBJECT" : argString(arguments, "label");
    const std::string type = argString(arguments, "type").empty() ? "object" : argString(arguments, "type");
    const int x = argClampedInt(arguments, "x", 0, 0, 159);
    const int y = argClampedInt(arguments, "y", 0, 0, 143);
    const int w = argClampedInt(arguments, "w", 8, 1, 160);
    const int h = argClampedInt(arguments, "h", 8, 1, 144);
    const int frames = argFrames(arguments, 180);

    std::string writeError;
    const std::string line = annotationJson(label, type, x, y, w, h, frames) + "\n";
    if (!appendTextFile(queuePath.value(), line, &writeError)) {
        return textToolResult("RunLab visual annotation failed: " + writeError);
    }
    Json::Object out;
    out["queued"] = true;
    out["queue_path"] = queuePath->string();
    out["command"] = "annotation";
    out["label"] = label;
    out["type"] = type;
    out["x"] = x;
    out["y"] = y;
    out["w"] = w;
    out["h"] = h;
    out["frames"] = frames;
    out["note"] = "Annotation queued. Emulator must have RunLab MCP bridge enabled.";
    return textToolResult(Json(out).dump(2));
}

Json latestPendingPrompt(const RunLabClient& client) {
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().promptQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return textToolResult("RunLab prompt queue unavailable: " + pathError);
    }

    std::string readError;
    const std::string text = readTextFile(queuePath.value(), &readError);
    Json::Object result;
    result["available"] = false;
    result["queue_path"] = queuePath->string();
    if (!readError.empty()) {
        result["reason"] = readError;
        return textToolResult(Json(result).dump(2));
    }

    std::set<int> handled;
    Json latest;
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
        const std::string status = statusValue->asString();
        if (status == "handled" || status == "done" || status == "ack") {
            handled.insert(id);
            const Json* latestId = latest.find("id");
            if (latestId && latestId->isNumber() && latestId->asInt(0) == id) {
                latest = Json();
            }
            continue;
        }
        if (status == "pending" && handled.find(id) == handled.end()) {
            latest = *parsed;
        }
    }

    if (!latest.isNull()) {
        result["available"] = true;
        result["prompt"] = latest;
        result["instruction"] = "Interpret this user prompt, then act through RunLab MCP tools such as runlab_get_control_context, runlab_get_visual_context, runlab_control_macro, and runlab_visual_annotate. Call runlab_ack_prompt when done.";
        const Json* idValue = latest.find("id");
        const int id = idValue && idValue->isNumber() ? idValue->asInt(0) : 0;
        appendFeedback(client, "prompt_read", "MCP read prompt #" + std::to_string(id), id);
    } else {
        result["reason"] = "no pending prompt";
    }
    return textToolResult(Json(result).dump(2));
}

Json ackPrompt(const RunLabClient& client, const Json& arguments) {
    const int id = argId(arguments);
    if (id <= 0) {
        return textToolResult("RunLab prompt ack requires numeric id.");
    }
    std::string pathError;
    const auto queuePath = safeConfiguredPath(client.config().promptQueuePath, &pathError);
    if (!queuePath.has_value()) {
        return textToolResult("RunLab prompt queue unavailable: " + pathError);
    }
    Json::Object obj;
    obj["id"] = id;
    obj["status"] = "handled";
    obj["note"] = argString(arguments, "note");
    std::string writeError;
    if (!appendTextFile(queuePath.value(), Json(obj).dump() + "\n", &writeError)) {
        return textToolResult("RunLab prompt ack failed: " + writeError);
    }
    const std::string note = argString(arguments, "note");
    appendFeedback(client, "prompt_done", note.empty() ? ("MCP finished prompt #" + std::to_string(id)) : ("MCP done: " + note), id);
    Json::Object out;
    out["handled"] = true;
    out["id"] = id;
    out["queue_path"] = queuePath->string();
    return textToolResult(Json(out).dump(2));
}

} // namespace

Json toolsList() {
    Json::Array tools;
    tools.push_back(tool("runlab_get_status", "Get RunLab status", "Read concise RunLab status.", schema()));
    tools.push_back(tool("runlab_list_entities", "List entities", "List semantic entities, optionally filtered by type.", schema({{"typeFilter", Json::Object{{"type", "string"}, {"description", "Optional entity type filter: Player, Enemy, Item, Boss, Unknown"}}}})));
    tools.push_back(tool("runlab_list_memory_labels", "List memory labels", "List semantic memory labels, optionally filtered by entity.", schema({{"entity", Json::Object{{"type", "string"}, {"description", "Optional entity label filter"}}}})));
    tools.push_back(tool("runlab_get_recent_events", "Get recent events", "Return recent RunLab events.", schema({{"limit", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 100}, {"default", 20}}}, {"typeFilter", Json::Object{{"type", "string"}, {"description", "Optional event type filter"}}}})));
    tools.push_back(tool("runlab_get_active_goal", "Get active goal", "Return active goal status.", schema()));
    tools.push_back(tool("runlab_get_splits", "Get splits", "Return split definitions and status.", schema()));
    tools.push_back(tool("runlab_analyze_current_state", "Analyze current state", "Return an analysis-ready summary without route advice.", schema()));
    tools.push_back(tool("runlab_export_profile", "Return profile", "Return existing profile JSON; does not write files.", schema({{"includeEvents", Json::Object{{"type", "boolean"}, {"default", false}}}})));
    tools.push_back(tool("runlab_control_tap", "Tap buttons", "Append a read-only-control queue command that taps buttons for a few frames.", schema({{"buttons", stringArrayProperty("Buttons: right, left, up, down, a, b, select, start")}, {"frames", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 600}, {"default", 6}}}})));
    tools.push_back(tool("runlab_control_hold", "Hold buttons", "Append a control queue command that holds buttons for N frames.", schema({{"buttons", stringArrayProperty("Buttons: right, left, up, down, a, b, select, start")}, {"frames", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 600}, {"default", 30}}}})));
    tools.push_back(tool("runlab_control_release_all", "Release MCP buttons", "Append a control queue command that releases buttons controlled by RunLab MCP.", schema()));
    tools.push_back(tool("runlab_control_advance_frames", "Advance frames", "Append a no-button command for N frames while the emulator is unpaused.", schema({{"frames", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 600}, {"default", 1}}}})));
    tools.push_back(tool("runlab_control_pause", "Pause emulator", "Append a control queue command that pauses the emulator.", schema()));
    tools.push_back(tool("runlab_control_resume", "Resume emulator", "Append a control queue command that resumes the emulator.", schema()));
    tools.push_back(tool("runlab_control_step_frame", "Step frames", "Append a control queue command that advances N frames even while paused.", schema({{"frames", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 600}, {"default", 1}}}})));
    tools.push_back(tool("runlab_get_control_queue_status", "Get control queue status", "Read MCP control queue status from current RunLab state.", schema()));
    tools.push_back(tool("runlab_get_control_context", "Get control context", "Return one compact observation packet for an AI control loop.", schema()));
    tools.push_back(tool("runlab_get_visual_context", "Get visual context", "Return screenshot metadata and semantic hints for screen interpretation.", schema({{"limit", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 100}, {"default", 20}}}})));
    tools.push_back(tool("runlab_get_pending_prompt", "Get pending prompt", "Read the latest unhandled prompt submitted from the emulator Ctrl+T box.", schema()));
    tools.push_back(tool("runlab_ack_prompt", "Acknowledge prompt", "Mark a RunLab prompt id as handled after acting on it.", schema({{"id", Json::Object{{"type", "number"}, {"minimum", 1}}}, {"note", Json::Object{{"type", "string"}}}})));
    tools.push_back(tool("runlab_visual_annotate", "Annotate screen", "Append a visual overlay annotation for player/enemy/scenario/object boxes.", schema({
        {"label", Json::Object{{"type", "string"}, {"description", "Short label to draw, such as PLAYER, ENEMY, SCENARIO"}}},
        {"type", Json::Object{{"type", "string"}, {"description", "Annotation type: player, enemy, scenario, item, object"}}},
        {"x", Json::Object{{"type", "number"}, {"minimum", 0}, {"maximum", 159}}},
        {"y", Json::Object{{"type", "number"}, {"minimum", 0}, {"maximum", 143}}},
        {"w", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 160}}},
        {"h", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 144}}},
        {"frames", Json::Object{{"type", "number"}, {"minimum", 1}, {"maximum", 600}, {"default", 180}}}
    })));
    tools.push_back(tool("runlab_control_macro", "Queue control macro", "Append up to 32 input/control commands in one call.", schema({{"commands", Json::Object{{"type", "array"}, {"description", "Array of command objects: command, buttons, frames"}}}})));
    Json::Object result;
    result["tools"] = Json(tools);
    return Json(result);
}

Json callTool(const RunLabClient& client, const std::string& name, const Json& arguments, bool* invalidParams) {
    if (invalidParams) *invalidParams = false;
    if (!arguments.isObject()) {
        if (invalidParams) *invalidParams = true;
        return Json();
    }

    if (name == "runlab_control_tap") {
        const auto buttons = argButtons(arguments);
        if (buttons.empty()) {
            if (invalidParams) *invalidParams = true;
            return Json();
        }
        return enqueueControlCommand(client, "tap", buttons, argFrames(arguments, 6));
    }
    if (name == "runlab_control_hold") {
        const auto buttons = argButtons(arguments);
        if (buttons.empty()) {
            if (invalidParams) *invalidParams = true;
            return Json();
        }
        return enqueueControlCommand(client, "hold", buttons, argFrames(arguments, 30));
    }
    if (name == "runlab_control_release_all") {
        return enqueueControlCommand(client, "release_all", {}, 1);
    }
    if (name == "runlab_control_advance_frames") {
        return enqueueControlCommand(client, "advance_frames", {}, argFrames(arguments, 1));
    }
    if (name == "runlab_control_pause") {
        return enqueueControlCommand(client, "pause", {}, 1);
    }
    if (name == "runlab_control_resume") {
        return enqueueControlCommand(client, "resume", {}, 1);
    }
    if (name == "runlab_control_step_frame") {
        return enqueueControlCommand(client, "step_frame", {}, argFrames(arguments, 1));
    }
    if (name == "runlab_visual_annotate") {
        return enqueueVisualAnnotation(client, arguments);
    }
    if (name == "runlab_get_pending_prompt") {
        return latestPendingPrompt(client);
    }
    if (name == "runlab_ack_prompt") {
        return ackPrompt(client, arguments);
    }
    if (name == "runlab_control_macro") {
        const Json* commands = arguments.find("commands");
        if (!commands || !commands->isArray()) {
            if (invalidParams) *invalidParams = true;
            return Json();
        }
        return enqueueControlLines(client, commands->asArray());
    }

    RunLabState state;
    if (name != "runlab_export_profile") {
        Json loadResult = loadOrError(client, state);
        if (!state.ok()) return loadResult;
    } else {
        state = client.loadState();
    }

    if (name == "runlab_get_status") {
        const Json status = state.status();
        std::ostringstream out;
        out << statusText(status);
        out << "Entities: " << state.entities().asArray().size() << "\n";
        out << "Memory labels: " << state.memoryLabels().asArray().size() << "\n";
        out << "Recent events: " << state.events(20).asArray().size() << "\n";
        out << "Active goal:\n" << state.activeGoal().dump(2);
        return textToolResult(out.str());
    }
    if (name == "runlab_list_entities") {
        return textToolResult(jsonText(state.entities(argString(arguments, "typeFilter"))));
    }
    if (name == "runlab_list_memory_labels") {
        return textToolResult(jsonText(state.memoryLabels(argString(arguments, "entity"))));
    }
    if (name == "runlab_get_recent_events") {
        return textToolResult(jsonText(state.events(argLimit(arguments), argString(arguments, "typeFilter"))));
    }
    if (name == "runlab_get_active_goal") {
        return textToolResult(jsonText(state.activeGoal()));
    }
    if (name == "runlab_get_splits") {
        return textToolResult(jsonText(state.splits()));
    }
    if (name == "runlab_analyze_current_state") {
        return textToolResult(jsonText(state.analysisSummary()));
    }
    if (name == "runlab_get_control_context") {
        Json::Object out;
        out["status"] = state.status();
        const Json* control = state.root().find("control");
        out["control"] = control ? *control : Json::Object{{"available", false}};
        out["entities"] = state.entities();
        out["memory_labels"] = state.memoryLabels();
        out["recent_events"] = state.events(argLimit(arguments), argString(arguments, "typeFilter"));
        out["active_goal"] = state.activeGoal();
        out["splits"] = state.splits();
        out["analysis"] = state.analysisSummary();
        out["prompt_hint"] = "If the user submitted a Ctrl+T prompt, call runlab_get_pending_prompt, act on it, then call runlab_ack_prompt.";
        out["control_hint"] = "Use runlab_control_macro for short observe-act loops. Prefer pause/step_frame for precise control. Do not request memory writes or cheats.";
        return textToolResult(Json(out).dump(2));
    }
    if (name == "runlab_get_visual_context") {
        Json::Object out;
        const Json status = state.status();
        out["status"] = status;
        Json::Object screenshot;
        screenshot["resource"] = "runlab://screenshot/current";
        screenshot["width"] = 160;
        screenshot["height"] = 144;
        const Json* path = status.find("screenshot_path");
        screenshot["path"] = path && path->isString() ? path->asString() : "";
        screenshot["format"] = "PPM P6 RGB24";
        out["screenshot"] = Json(screenshot);
        out["entities"] = state.entities();
        out["memory_labels"] = state.memoryLabels();
        out["recent_events"] = state.events(argLimit(arguments), argString(arguments, "typeFilter"));
        out["visual_hints"] = Json::Array{
            "Use the current screenshot plus entity/OAM hints to infer likely player, enemies, items, and scenario.",
            "Coordinates for runlab_visual_annotate are Game Boy screen pixels: x 0-159, y 0-143.",
            "Use short labels like PLAYER, ENEMY, SCENARIO. This is visual annotation only; it does not write memory."
        };
        return textToolResult(Json(out).dump(2));
    }
    if (name == "runlab_get_control_queue_status") {
        const Json* control = state.root().find("control");
        if (!control) {
            return textToolResult("{\n  \"available\": false,\n  \"reason\": \"current state has no control object\"\n}");
        }
        return textToolResult(jsonText(*control));
    }
    if (name == "runlab_export_profile") {
        const bool includeEvents = arguments.find("includeEvents") && arguments.find("includeEvents")->asBool(false);
        if (!state.ok() && !client.config().profilePath.has_value()) {
            return textToolResult("RunLab profile unavailable: no profile path configured and state unavailable.");
        }
        auto profile = state.profileJson(client.config(), includeEvents);
        if (!profile.has_value()) {
            return textToolResult("RunLab profile unavailable: configure ORBITALBOY_RUNLAB_PROFILE_PATH or embed profile in state.");
        }
        return textToolResult(profile.value());
    }

    if (invalidParams) *invalidParams = true;
    return Json();
}

} // namespace orbitalboy::mcp
