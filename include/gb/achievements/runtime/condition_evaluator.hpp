#pragma once

#include <optional>
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

    memory::MemoryReader reader_;
    std::vector<MemoryHistory> histories_;
    std::vector<HitCount> hitCounts_;
};

} // namespace gb::achievements::runtime
