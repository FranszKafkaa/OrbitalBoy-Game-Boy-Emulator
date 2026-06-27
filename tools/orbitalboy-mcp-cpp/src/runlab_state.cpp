#include "runlab_state.h"

#include <algorithm>
#include <sstream>

namespace orbitalboy::mcp {

namespace {

Json getObj(const Json& root, const std::string& key) {
    const Json* value = root.find(key);
    return value ? *value : Json::Object{};
}

Json getArray(const Json& root, const std::string& key) {
    const Json* value = root.find(key);
    return value && value->isArray() ? *value : Json::Array{};
}

std::string strField(const Json& obj, const std::string& key) {
    const Json* v = obj.find(key);
    return v && v->isString() ? v->asString() : "";
}

int intField(const Json& obj, const std::string& key, int fallback = 0) {
    const Json* v = obj.find(key);
    return v ? v->asInt(fallback) : fallback;
}

bool boolField(const Json& obj, const std::string& key, bool fallback = false) {
    const Json* v = obj.find(key);
    return v ? v->asBool(fallback) : fallback;
}

bool hasLabel(const Json::Array& labels, const std::string& name) {
    for (const auto& label : labels) {
        const std::string labelName = strField(label, "label");
        const std::string entity = strField(label, "entity");
        const std::string field = strField(label, "field");
        if (labelName == name || (!entity.empty() && !field.empty() && entity + "." + field == name)) {
            return true;
        }
    }
    return false;
}

bool hasAnyLabel(const Json::Array& labels, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (hasLabel(labels, name)) return true;
    }
    return false;
}

} // namespace

RunLabState RunLabState::load(const ServerConfig& config) {
    RunLabState state;
    std::string pathError;
    const auto safePath = safeConfiguredPath(config.statePath, &pathError);
    if (!safePath.has_value()) {
        state.error_ = pathError;
        return state;
    }
    std::string readError;
    const std::string text = readTextFile(safePath.value(), &readError);
    if (!readError.empty()) {
        state.error_ = readError;
        return state;
    }
    std::string parseError;
    auto root = Json::parse(text, &parseError);
    if (!root.has_value()) {
        state.error_ = "invalid state JSON: " + parseError;
        return state;
    }
    state.root_ = *root;
    return state;
}

bool RunLabState::ok() const { return error_.empty(); }
const std::string& RunLabState::error() const { return error_; }
const Json& RunLabState::root() const { return root_; }

Json RunLabState::status() const {
    return getObj(root_, "status");
}

Json RunLabState::entities(const std::string& typeFilter) const {
    const Json arrJson = getArray(root_, "entities");
    const auto& arr = arrJson.asArray();
    Json::Array out;
    for (const auto& item : arr) {
        if (!typeFilter.empty() && strField(item, "type") != typeFilter) {
            continue;
        }
        out.push_back(item);
    }
    return Json(out);
}

Json RunLabState::memoryLabels(const std::string& entityFilter) const {
    const Json arrJson = getArray(root_, "memory_labels");
    const auto& arr = arrJson.asArray();
    Json::Array out;
    for (const auto& item : arr) {
        if (!entityFilter.empty() && strField(item, "entity") != entityFilter) {
            continue;
        }
        out.push_back(item);
    }
    return Json(out);
}

Json RunLabState::events(int limit, const std::string& typeFilter) const {
    limit = std::max(1, std::min(100, limit));
    const Json arrJson = getArray(root_, "events");
    const auto& arr = arrJson.asArray();
    Json::Array filtered;
    for (const auto& item : arr) {
        if (!typeFilter.empty() && strField(item, "type") != typeFilter) {
            continue;
        }
        filtered.push_back(item);
    }
    Json::Array out;
    const int start = std::max(0, static_cast<int>(filtered.size()) - limit);
    for (int i = start; i < static_cast<int>(filtered.size()); ++i) {
        out.push_back(filtered[static_cast<std::size_t>(i)]);
    }
    return Json(out);
}

Json RunLabState::activeGoal() const {
    const Json* goal = root_.find("goal");
    if (goal && !goal->isNull()) {
        return *goal;
    }
    const Json* goals = root_.find("goals");
    if (goals && goals->isArray()) {
        for (const auto& g : goals->asArray()) {
            if (boolField(g, "active")) return g;
        }
    }
    Json::Object empty;
    empty["active"] = false;
    empty["message"] = "No active RunLab goal.";
    return Json(empty);
}

Json RunLabState::splits() const {
    return getArray(root_, "splits");
}

Json RunLabState::analysisSummary() const {
    const auto statusObj = status();
    const Json entitiesJson = entities();
    const auto& entitiesArr = entitiesJson.asArray();
    const Json labelsJson = memoryLabels();
    const auto& labelsArr = labelsJson.asArray();
    bool hasPlayer = false;
    for (const auto& entity : entitiesArr) {
        if (strField(entity, "label") == "player" || strField(entity, "type") == "Player") {
            hasPlayer = true;
        }
    }
    Json::Array warnings;
    if (!hasPlayer) warnings.push_back("Missing player entity.");
    if (!hasLabel(labelsArr, "player.x")) warnings.push_back("Missing player.x.");
    if (!hasLabel(labelsArr, "player.y")) warnings.push_back("Missing player.y.");
    if (!hasAnyLabel(labelsArr, {"lives", "player.hp", "hp", "health"})) warnings.push_back("Missing lives/hp.");
    if (!hasAnyLabel(labelsArr, {"level_id", "stage", "room", "room_id"})) warnings.push_back("Missing level_id/stage/room, so finish_current_level cannot be inferred reliably.");

    Json::Object out;
    out["status"] = statusObj;
    out["has_player_entity"] = hasPlayer;
    out["has_player_x"] = hasLabel(labelsArr, "player.x");
    out["has_player_y"] = hasLabel(labelsArr, "player.y");
    out["has_lives_or_hp"] = hasAnyLabel(labelsArr, {"lives", "player.hp", "hp", "health"});
    out["has_level_or_room"] = hasAnyLabel(labelsArr, {"level_id", "stage", "room", "room_id"});
    out["recent_events"] = events(10, "");
    out["active_goal"] = activeGoal();
    out["warnings"] = Json(warnings);
    return Json(out);
}

std::optional<std::string> RunLabState::profileJson(const ServerConfig& config, bool includeEvents) const {
    (void)includeEvents;
    if (config.profilePath.has_value()) {
        std::string pathError;
        const auto path = safeConfiguredPath(config.profilePath.value(), &pathError);
        if (!path.has_value()) return std::nullopt;
        std::string readError;
        const std::string text = readTextFile(path.value(), &readError);
        if (readError.empty()) return text;
    }
    const Json* profile = root_.find("profile");
    if (profile) {
        return profile->dump(2);
    }
    return std::nullopt;
}

std::string jsonText(const Json& value) {
    return value.dump(2);
}

std::string statusText(const Json& status) {
    std::ostringstream out;
    out << "ROM: " << strField(status, "rom_name") << "\n";
    out << "Frame: " << intField(status, "frame") << "\n";
    out << "Running: " << (boolField(status, "running") ? "true" : "false") << "\n";
    out << "Paused: " << (boolField(status, "paused") ? "true" : "false") << "\n";
    return out.str();
}

} // namespace orbitalboy::mcp
