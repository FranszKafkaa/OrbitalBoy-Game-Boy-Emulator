#pragma once

#include <optional>
#include <cstdint>
#include <string>
#include <vector>

#include "gb/achievements/memory/memory_reader.hpp"
#include "gb/achievements/parser/condition_ast.hpp"

namespace gb::achievements::runtime {

enum class ConditionEvaluationStatus : unsigned char {
    Waiting,
    Primed,
    Triggered,
    Paused,
    Reset,
    Unsupported,
    MemoryError,
};

struct ConditionEvaluation {
    ConditionEvaluationStatus status = ConditionEvaluationStatus::Waiting;
    std::optional<parser::SourceSpan> sourceSpan{};
    std::string reason{};
    std::optional<std::uint32_t> measuredValue{};
    std::optional<std::uint32_t> measuredPercent{};
};

class ConditionEvaluator {
public:
    explicit ConditionEvaluator(memory::MemoryReader reader);

    ConditionEvaluation evaluate(const parser::ConditionTrigger& trigger);
    void reset() noexcept;

private:
    friend class EvaluationFrame;

    struct MemoryHistory {
        std::uint32_t address = 0U;
        parser::MemorySize size = parser::MemorySize::EightBit;
        std::uint32_t last = 0U;
        std::uint32_t prior = 0U;
        bool hasLast = false;
        bool hasPrior = false;
    };

    struct HitCount {
        std::string source{};
        parser::SourceSpan span{};
        std::uint32_t count = 0U;
    };

    struct MeasuredState {
        std::string source{};
        std::uint32_t value = 0U;
        std::optional<std::uint32_t> percent{};
        bool remembered = false;
    };

    memory::MemoryReader reader_;
    std::vector<MemoryHistory> histories_;
    std::vector<HitCount> hitCounts_;
    std::vector<MeasuredState> measured_;
    std::string activeSource_;
};

} // namespace gb::achievements::runtime
