#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "gb/achievements/memory/memory_reader.hpp"
#include "gb/achievements/parser/condition_ast.hpp"
#include "gb/achievements/runtime/condition_evaluator.hpp"

namespace gb::achievements::runtime {

enum class FrameEventKind : unsigned char { Unlocked, Error };

struct FrameEvent {
    FrameEventKind kind = FrameEventKind::Error;
    std::string key{};
    ConditionEvaluationStatus status = ConditionEvaluationStatus::Waiting;
    std::string reason{};
};

struct FrameEvaluationSummary {
    std::size_t evaluated = 0U;
    std::size_t unlocked = 0U;
    std::size_t errors = 0U;
};

class AchievementFrameBridge {
public:
    using EventCallback = std::function<void(const FrameEvent&)>;

    explicit AchievementFrameBridge(memory::MemoryReader reader);
    bool addAchievement(std::string key, parser::ConditionTrigger trigger);
    FrameEvaluationSummary evaluateFrame();
    void reset();
    void clear();
    void setEventCallback(EventCallback callback);

private:
    struct Entry {
        std::string key;
        parser::ConditionTrigger trigger;
        bool unlocked = false;
    };
    ConditionEvaluator evaluator_;
    std::vector<Entry> entries_;
    EventCallback callback_;
};

} // namespace gb::achievements::runtime
