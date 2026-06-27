#include "mcp_resources.h"

#include <utility>

namespace orbitalboy::mcp {

namespace {

Json resource(const char* uri, const char* name, const char* description, const char* mime = "application/json") {
    Json::Object obj;
    obj["uri"] = uri;
    obj["name"] = name;
    obj["description"] = description;
    obj["mimeType"] = mime;
    return Json(obj);
}

Json contents(const std::string& uri, const std::string& text, const std::string& mime = "application/json") {
    Json::Object item;
    item["uri"] = uri;
    item["mimeType"] = mime;
    item["text"] = text;
    Json::Object result;
    result["contents"] = Json::Array{Json(item)};
    return Json(result);
}

Json stateUnavailable(const std::string& uri, const RunLabState& state) {
    Json::Object obj;
    obj["available"] = false;
    obj["error"] = state.error();
    return contents(uri, Json(obj).dump(2));
}

std::string screenshotPathFromStatus(const RunLabState& state) {
    const Json status = state.status();
    const Json* path = status.find("screenshot_path");
    return path && path->isString() ? path->asString() : "";
}

} // namespace

Json resourcesList() {
    Json::Array out;
    out.push_back(resource("runlab://status", "RunLab status", "Current RunLab status snapshot."));
    out.push_back(resource("runlab://profile/current", "Current profile", "Current exported or embedded RunLab profile."));
    out.push_back(resource("runlab://entities", "Entities", "Current semantic entity candidates and labels."));
    out.push_back(resource("runlab://memory-labels", "Memory labels", "Current semantic memory labels."));
    out.push_back(resource("runlab://events/recent", "Recent events", "Recent RunLab semantic timeline events."));
    out.push_back(resource("runlab://goals/active", "Active goal", "Active goal and completion status."));
    out.push_back(resource("runlab://splits", "Splits", "Current split definitions and trigger state."));
    out.push_back(resource("runlab://screenshot/current", "Current screenshot", "Configured screenshot path metadata.", "application/json"));
    Json::Object result;
    result["resources"] = Json(out);
    return Json(result);
}

Json readResource(const RunLabClient& client, const std::string& uri, bool* invalidParams) {
    if (invalidParams) *invalidParams = false;
    RunLabState state = client.loadState();
    if (uri == "runlab://profile/current") {
        auto profile = state.profileJson(client.config(), false);
        if (profile.has_value()) return contents(uri, profile.value());
        if (!state.ok()) return stateUnavailable(uri, state);
        return contents(uri, "{\"available\": false, \"error\": \"profile not present\"}");
    }
    if (!state.ok()) return stateUnavailable(uri, state);

    if (uri == "runlab://status") return contents(uri, state.status().dump(2));
    if (uri == "runlab://entities") return contents(uri, state.entities().dump(2));
    if (uri == "runlab://memory-labels") return contents(uri, state.memoryLabels().dump(2));
    if (uri == "runlab://events/recent") return contents(uri, state.events(20).dump(2));
    if (uri == "runlab://goals/active") return contents(uri, state.activeGoal().dump(2));
    if (uri == "runlab://splits") return contents(uri, state.splits().dump(2));
    if (uri == "runlab://screenshot/current") {
        Json::Object obj;
        const std::string configured = screenshotPathFromStatus(state);
        obj["available"] = false;
        obj["path"] = configured;
        if (!configured.empty()) {
            std::string pathError;
            auto safe = safeScreenshotPath(configured, client.config(), &pathError);
            if (safe.has_value()) {
                obj["available"] = true;
                obj["path"] = safe->string();
            } else {
                obj["error"] = pathError;
            }
        }
        return contents(uri, Json(obj).dump(2));
    }

    if (invalidParams) *invalidParams = true;
    return Json();
}

} // namespace orbitalboy::mcp
