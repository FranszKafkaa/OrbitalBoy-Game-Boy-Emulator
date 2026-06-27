#include "mcp_tools.h"

#include <sstream>
#include <utility>

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
