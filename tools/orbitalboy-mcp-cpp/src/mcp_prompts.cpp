#include "mcp_prompts.h"

namespace orbitalboy::mcp {

namespace {

Json prompt(const char* name, const char* description) {
    Json::Object obj;
    obj["name"] = name;
    obj["description"] = description;
    obj["arguments"] = Json::Array{};
    return Json(obj);
}

Json promptResult(const std::string& description, const std::string& text) {
    Json::Object content;
    content["type"] = "text";
    content["text"] = text;
    Json::Object message;
    message["role"] = "user";
    message["content"] = Json(content);
    Json::Object result;
    result["description"] = description;
    result["messages"] = Json::Array{Json(message)};
    return Json(result);
}

} // namespace

Json promptsList() {
    Json::Array prompts;
    prompts.push_back(prompt("explain_recent_death", "Explain likely causes for recent death or damage events."));
    prompts.push_back(prompt("suggest_splits_from_events", "Suggest speedrun splits from observed RunLab events."));
    prompts.push_back(prompt("suggest_goal_from_profile", "Suggest a read-only goal definition from the current profile."));
    prompts.push_back(prompt("analyze_speedrun_segment", "Analyze the current speedrun segment using RunLab labels and events."));
    prompts.push_back(prompt("suggest_missing_memory_labels", "Suggest useful memory labels that are missing from the profile."));
    prompts.push_back(prompt("control_game_with_runlab", "Control the game through RunLab MCP using observe-act loops."));
    prompts.push_back(prompt("identify_screen_objects", "Identify likely player, enemies, and scenario from the current screen."));
    prompts.push_back(prompt("handle_prompt_box", "Handle a prompt submitted from the emulator Ctrl+T box."));
    Json::Object result;
    result["prompts"] = Json(prompts);
    return Json(result);
}

Json getPrompt(const std::string& name, const Json& arguments, bool* invalidParams) {
    (void)arguments;
    if (invalidParams) *invalidParams = false;
    if (name == "explain_recent_death") {
        return promptResult(name, "Use runlab_get_recent_events with typeFilter damage_candidate or death_candidate, then explain the likely memory-label evidence. Do not claim certainty; RunLab events are heuristic.");
    }
    if (name == "suggest_splits_from_events") {
        return promptResult(name, "Inspect runlab_get_recent_events, runlab_get_splits, and memory labels such as level_id, room, stage, goal_flag, and game_state. Suggest read-only split candidates with the event frame and supporting label.");
    }
    if (name == "suggest_goal_from_profile") {
        return promptResult(name, "Inspect runlab_export_profile and runlab_analyze_current_state. Suggest a goal using existing labels only; do not propose memory writes, cheats, route optimization, or emulator control.");
    }
    if (name == "analyze_speedrun_segment") {
        return promptResult(name, "Use status, recent events, active goal, splits, entities, and memory labels to summarize progress, damage/death risk, level transitions, and missing evidence.");
    }
    if (name == "suggest_missing_memory_labels") {
        return promptResult(name, "Use runlab_analyze_current_state and list missing labels that would improve read-only inference, especially player.x, player.y, lives/hp, level_id/room/stage, state, and goal_flag.");
    }
    if (name == "control_game_with_runlab") {
        return promptResult(
            name,
            "You are controlling a Game Boy game only through RunLab MCP input tools. First call runlab_get_control_context. If control.enabled is false or client_recently_seen is false, explain that the emulator bridge/client is not ready. Use runlab_control_pause and runlab_control_step_frame for precise observation. Use runlab_control_macro for short safe action batches such as tap/hold/advance_frames, then observe again. Never request memory writes, cheats, arbitrary file reads, TAS solving, or route optimization. Stop and report evidence if recent events indicate damage, death, level transition, or goal completion."
        );
    }
    if (name == "identify_screen_objects") {
        return promptResult(
            name,
            "Call runlab_get_visual_context, inspect the current screenshot path/resource with vision, and use RunLab entities/OAM hints as supporting evidence. Then call runlab_visual_annotate for likely PLAYER, ENEMY, ITEM, and SCENARIO boxes using Game Boy screen coordinates. Explain that visual identification is heuristic and may confuse multi-sprite objects, HUD tiles, background, and camera-scrolled objects. Do not write memory or request cheats."
        );
    }
    if (name == "handle_prompt_box") {
        return promptResult(
            name,
            "Call runlab_get_pending_prompt. If no prompt is pending, say that the Ctrl+T prompt queue is empty. If a prompt exists, interpret it as the user's requested action for the current game state. Use runlab_get_control_context and runlab_get_visual_context to observe; use runlab_control_macro and runlab_visual_annotate to act when appropriate. Never write emulator memory, request cheats, solve TAS routes, or claim certainty from heuristics. When finished, call runlab_ack_prompt with the prompt id and a short note."
        );
    }
    if (invalidParams) *invalidParams = true;
    return Json();
}

} // namespace orbitalboy::mcp
