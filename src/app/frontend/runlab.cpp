#include "gb/app/frontend/runlab.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <utility>

namespace gb::frontend::runlab {

namespace {

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string safeFileStem(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (unsigned char ch : raw) {
        char lower = static_cast<char>(std::tolower(ch));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
            out.push_back(lower);
        } else if (lower == '-' || lower == '_') {
            out.push_back(lower);
        } else if (lower == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "game";
    }
    return out;
}

bool ramDiffAddress(gb::u16 addr) {
    return (addr >= 0xA000 && addr <= 0xBFFF)
        || (addr >= 0xC000 && addr <= 0xDFFF)
        || (addr >= 0xFF80 && addr <= 0xFFFE);
}

int readSampleValue(const CorrelationSample& sample, std::size_t offset, MemoryValueType type) {
    if (offset >= sample.wram.size()) {
        return 0;
    }
    const gb::u8 lo = sample.wram[offset];
    if (type == MemoryValueType::U16Le) {
        if (offset + 1 >= sample.wram.size()) {
            return lo;
        }
        return static_cast<int>(lo | (static_cast<gb::u16>(sample.wram[offset + 1]) << 8));
    }
    return lo;
}

double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double signedDeltaAgreement(const std::vector<int>& entity, const std::vector<int>& memory, bool inverse) {
    int considered = 0;
    int matched = 0;
    for (std::size_t i = 1; i < entity.size() && i < memory.size(); ++i) {
        const int ed = entity[i] - entity[i - 1];
        const int mdRaw = memory[i] - memory[i - 1];
        const int md = inverse ? -mdRaw : mdRaw;
        if (ed == 0 && md == 0) {
            continue;
        }
        if (ed == 0 || md == 0) {
            ++considered;
            continue;
        }
        ++considered;
        if ((ed > 0 && md > 0) || (ed < 0 && md < 0)) {
            ++matched;
        }
    }
    if (considered == 0) {
        return 0.0;
    }
    return static_cast<double>(matched) / static_cast<double>(considered);
}

double movementTriggeredChangeRatio(const std::vector<int>& entity, const std::vector<int>& memory) {
    int movingFrames = 0;
    int changedWhileMoving = 0;
    int stillFrames = 0;
    int changedWhileStill = 0;
    for (std::size_t i = 1; i < entity.size() && i < memory.size(); ++i) {
        const bool entityMoved = entity[i] != entity[i - 1];
        const bool memoryChanged = memory[i] != memory[i - 1];
        if (entityMoved) {
            ++movingFrames;
            if (memoryChanged) {
                ++changedWhileMoving;
            }
        } else {
            ++stillFrames;
            if (memoryChanged) {
                ++changedWhileStill;
            }
        }
    }
    if (movingFrames == 0) {
        return 0.0;
    }
    const double moving = static_cast<double>(changedWhileMoving) / static_cast<double>(movingFrames);
    const double stillPenalty = stillFrames == 0 ? 0.0 : static_cast<double>(changedWhileStill) / static_cast<double>(stillFrames);
    return clamp01(moving - stillPenalty * 0.65);
}

double closenessScore(const std::vector<int>& entity, const std::vector<int>& memory) {
    if (entity.empty() || memory.empty()) {
        return 0.0;
    }
    long long totalDistance = 0;
    const std::size_t n = std::min(entity.size(), memory.size());
    for (std::size_t i = 0; i < n; ++i) {
        totalDistance += std::llabs(static_cast<long long>(entity[i]) - static_cast<long long>(memory[i]));
    }
    const double avg = static_cast<double>(totalDistance) / static_cast<double>(n);
    return clamp01(1.0 - avg / 96.0);
}

std::size_t uniqueValueCount(const std::vector<int>& values) {
    std::vector<int> copy = values;
    std::sort(copy.begin(), copy.end());
    return static_cast<std::size_t>(std::unique(copy.begin(), copy.end()) - copy.begin());
}

int changedFrameCount(const std::vector<int>& values) {
    int changes = 0;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] != values[i - 1]) {
            ++changes;
        }
    }
    return changes;
}

void addTopCandidate(std::vector<CorrelationCandidate>& out, const CorrelationCandidate& candidate, std::size_t maxItems) {
    if (candidate.score <= 0.0) {
        return;
    }
    out.push_back(candidate);
    std::sort(out.begin(), out.end(), [](const CorrelationCandidate& lhs, const CorrelationCandidate& rhs) {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        if (lhs.address != rhs.address) {
            return lhs.address < rhs.address;
        }
        return static_cast<int>(lhs.type) < static_cast<int>(rhs.type);
    });
    if (out.size() > maxItems) {
        out.resize(maxItems);
    }
}

std::string lowerText(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char ch : text) {
        out.push_back(static_cast<char>(std::tolower(ch)));
    }
    return out;
}

bool semanticNameMatches(const MemoryLabel& label, const char* expected) {
    const std::string want = expected;
    const std::string full = lowerText(label.label);
    const std::string entity = lowerText(label.entity);
    const std::string field = lowerText(label.field);
    if (full == want || field == want) {
        return true;
    }
    if (!entity.empty() && full == entity + "." + want) {
        return true;
    }
    return full.size() > want.size()
        && full.compare(full.size() - want.size() - 1, want.size() + 1, "." + want) == 0;
}

bool semanticNameMatchesAny(const MemoryLabel& label, const std::vector<const char*>& expected) {
    for (const char* name : expected) {
        if (semanticNameMatches(label, name)) {
            return true;
        }
    }
    return false;
}

TimelineEvent makeEvent(
    std::uint64_t frame,
    RunLabEventType type,
    const MemoryLabel& label,
    std::int32_t previous,
    std::int32_t current,
    const std::string& description,
    double confidence
) {
    TimelineEvent event{};
    event.frame = frame;
    event.type = type;
    event.label = label.label;
    event.previous = previous;
    event.current = current;
    event.entity = label.entity;
    event.description = description;
    event.confidence = confidence;
    event.semantic = eventTypeName(type);
    event.reason = description;
    return event;
}

} // namespace

const char* entityTypeName(EntityType type) {
    switch (type) {
    case EntityType::Player: return "Player";
    case EntityType::Enemy: return "Enemy";
    case EntityType::Item: return "Item";
    case EntityType::Boss: return "Boss";
    default: return "Unknown";
    }
}

const char* memoryValueTypeName(MemoryValueType type) {
    switch (type) {
    case MemoryValueType::I8: return "i8";
    case MemoryValueType::U16Le: return "u16_le";
    case MemoryValueType::I16Le: return "i16_le";
    case MemoryValueType::Hex8: return "hex8";
    case MemoryValueType::Hex16: return "hex16";
    default: return "u8";
    }
}

const char* eventTypeName(RunLabEventType type) {
    switch (type) {
    case RunLabEventType::ValueIncreased: return "value_increased";
    case RunLabEventType::ValueDecreased: return "value_decreased";
    case RunLabEventType::DamageCandidate: return "damage_candidate";
    case RunLabEventType::DeathCandidate: return "death_candidate";
    case RunLabEventType::LevelChangeCandidate: return "level_change_candidate";
    case RunLabEventType::LevelClearCandidate: return "level_clear_candidate";
    case RunLabEventType::SplitCandidate: return "split_candidate";
    case RunLabEventType::GoalReachedCandidate: return "goal_reached_candidate";
    case RunLabEventType::Custom: return "custom";
    default: return "memory_changed";
    }
}

const char* conditionOperatorName(ConditionOperator op) {
    switch (op) {
    case ConditionOperator::ChangedFromInitial: return "changed_from_initial";
    case ConditionOperator::Equal: return "==";
    case ConditionOperator::NotEqual: return "!=";
    case ConditionOperator::Greater: return ">";
    case ConditionOperator::GreaterEqual: return ">=";
    case ConditionOperator::Less: return "<";
    case ConditionOperator::LessEqual: return "<=";
    case ConditionOperator::Decreased: return "decreased";
    case ConditionOperator::Increased: return "increased";
    case ConditionOperator::ChangedToNonzero: return "changed_to_nonzero";
    default: return "changed";
    }
}

EntityType nextEntityType(EntityType type) {
    switch (type) {
    case EntityType::Unknown: return EntityType::Player;
    case EntityType::Player: return EntityType::Enemy;
    case EntityType::Enemy: return EntityType::Item;
    case EntityType::Item: return EntityType::Boss;
    default: return EntityType::Unknown;
    }
}

int oamIndexFromAddress(gb::u16 address) {
    if (address < 0xFE00 || address > 0xFE9F) {
        return -1;
    }
    return static_cast<int>((address - 0xFE00) / 4);
}

SpriteBounds spriteBounds(const OamSpriteRef& sprite, const gb::Bus& bus) {
    const gb::u8 lcdc = bus.peek(0xFF40);
    const int h = (lcdc & 0x04) != 0 ? 16 : 8;
    return SpriteBounds{static_cast<int>(sprite.x) - 8, static_cast<int>(sprite.y) - 16, 8, h};
}

std::string formatAddress(gb::u16 address) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%04X", address);
    return buf;
}

std::int32_t readMemoryLabelValue(const gb::Bus& bus, const MemoryLabel& label) {
    const gb::u8 lo = bus.peek(label.address);
    switch (label.type) {
    case MemoryValueType::I8:
        return static_cast<std::int8_t>(lo);
    case MemoryValueType::U16Le:
    case MemoryValueType::Hex16:
        return static_cast<std::int32_t>(lo | (static_cast<gb::u16>(bus.peek(static_cast<gb::u16>(label.address + 1))) << 8));
    case MemoryValueType::I16Le: {
        const auto value = static_cast<std::uint16_t>(lo | (static_cast<gb::u16>(bus.peek(static_cast<gb::u16>(label.address + 1))) << 8));
        return static_cast<std::int16_t>(value);
    }
    default:
        return lo;
    }
}

EntityCandidate makeEntityCandidate(const OamSpriteRef& sprite, const gb::Bus& bus, const std::string& label, EntityType type) {
    EntityCandidate entity{};
    entity.label = label;
    entity.type = type;
    const int idx = oamIndexFromAddress(sprite.addr);
    if (idx >= 0) {
        entity.oamIndices.push_back(idx);
    }
    entity.lastBounds = spriteBounds(sprite, bus);
    return entity;
}

std::size_t createOrSelectEntityFromSprite(State& state, const OamSpriteRef& sprite, const gb::Bus& bus) {
    const int idx = oamIndexFromAddress(sprite.addr);
    for (std::size_t i = 0; i < state.entities.size(); ++i) {
        if (std::find(state.entities[i].oamIndices.begin(), state.entities[i].oamIndices.end(), idx) != state.entities[i].oamIndices.end()) {
            state.selectedEntity = i;
            state.entities[i].lastBounds = spriteBounds(sprite, bus);
            return i;
        }
    }

    std::string label;
    if (state.entities.empty()) {
        label = "player";
    } else {
        label = "entity_" + std::to_string(state.nextEntityId);
    }
    ++state.nextEntityId;
    state.entities.push_back(makeEntityCandidate(sprite, bus, label, state.entities.empty() ? EntityType::Player : EntityType::Unknown));
    state.selectedEntity = state.entities.size() - 1;
    return state.selectedEntity.value();
}

bool updateSelectedEntitySprite(State& state, const OamSpriteRef& sprite, const gb::Bus& bus) {
    if (!state.selectedEntity.has_value() || state.selectedEntity.value() >= state.entities.size()) {
        return false;
    }
    auto& entity = state.entities[state.selectedEntity.value()];
    const int idx = oamIndexFromAddress(sprite.addr);
    if (idx >= 0 && std::find(entity.oamIndices.begin(), entity.oamIndices.end(), idx) == entity.oamIndices.end()) {
        entity.oamIndices.push_back(idx);
        std::sort(entity.oamIndices.begin(), entity.oamIndices.end());
    }
    entity.lastBounds = spriteBounds(sprite, bus);
    return true;
}

bool cycleSelectedEntityType(State& state) {
    if (!state.selectedEntity.has_value() || state.selectedEntity.value() >= state.entities.size()) {
        return false;
    }
    auto& entity = state.entities[state.selectedEntity.value()];
    entity.type = nextEntityType(entity.type);
    return true;
}

MemoryLabel makeMemoryLabel(
    const std::string& label,
    gb::u16 address,
    MemoryValueType type,
    const std::string& entity,
    const std::string& field
) {
    MemoryLabel out{};
    out.label = label;
    out.address = address;
    out.type = type;
    out.entity = entity;
    out.field = field;
    return out;
}

std::size_t addMemoryLabelForWatch(State& state, gb::u16 address) {
    std::string entity;
    std::string field;
    std::string label;
    if (state.selectedEntity.has_value() && state.selectedEntity.value() < state.entities.size()) {
        entity = state.entities[state.selectedEntity.value()].label;
        field = "field_" + std::to_string(state.nextMemoryLabelId);
        label = entity + "." + field;
    } else {
        label = "mem_" + std::to_string(state.nextMemoryLabelId);
    }
    ++state.nextMemoryLabelId;
    state.memoryLabels.push_back(makeMemoryLabel(label, address, MemoryValueType::U8, entity, field));
    return state.memoryLabels.size() - 1;
}

std::size_t promoteDiffAddress(State& state, std::size_t diffIndex) {
    if (diffIndex >= state.lastDiff.size()) {
        return state.memoryLabels.size();
    }
    return addMemoryLabelForWatch(state, state.lastDiff[diffIndex].address);
}

std::size_t promoteCorrelationCandidate(State& state, CorrelationTarget target, std::size_t candidateIndex) {
    const std::vector<CorrelationCandidate>* candidates = nullptr;
    switch (target) {
    case CorrelationTarget::EntityX: candidates = &state.correlationResult.entityX; break;
    case CorrelationTarget::EntityY: candidates = &state.correlationResult.entityY; break;
    case CorrelationTarget::CameraX: candidates = &state.correlationResult.cameraX; break;
    case CorrelationTarget::State: candidates = &state.correlationResult.state; break;
    default: candidates = &state.correlationResult.state; break;
    }
    if (!candidates || candidateIndex >= candidates->size()) {
        return state.memoryLabels.size();
    }

    const auto& candidate = (*candidates)[candidateIndex];
    std::string entity;
    std::string field;
    std::string label;
    if (target == CorrelationTarget::CameraX) {
        field = "camera_x";
        label = "camera_x";
    } else if (state.selectedEntity.has_value() && state.selectedEntity.value() < state.entities.size()) {
        entity = state.entities[state.selectedEntity.value()].label;
        if (target == CorrelationTarget::EntityX) {
            field = "x";
        } else if (target == CorrelationTarget::EntityY) {
            field = "y";
        } else if (target == CorrelationTarget::State) {
            field = "state";
        } else {
            field = "custom_" + std::to_string(state.nextMemoryLabelId);
        }
        label = entity + "." + field;
    } else {
        if (target == CorrelationTarget::EntityX) {
            label = "entity_x";
            field = "x";
        } else if (target == CorrelationTarget::EntityY) {
            label = "entity_y";
            field = "y";
        } else if (target == CorrelationTarget::State) {
            label = "entity_state";
            field = "state";
        } else {
            label = "custom_" + std::to_string(state.nextMemoryLabelId);
        }
    }
    ++state.nextMemoryLabelId;
    state.memoryLabels.push_back(makeMemoryLabel(label, candidate.address, candidate.type, entity, field));
    state.memoryLabels.back().notes = candidate.reason;
    return state.memoryLabels.size() - 1;
}

std::vector<TimelineEvent> detectEventsForChange(
    const MemoryLabel& label,
    std::int32_t previous,
    std::int32_t current,
    std::uint64_t frame
) {
    std::vector<TimelineEvent> out;
    if (previous == current) {
        return out;
    }

    out.push_back(makeEvent(frame, RunLabEventType::MemoryChanged, label, previous, current, "memory label changed", 1.0));
    if (current > previous) {
        out.push_back(makeEvent(frame, RunLabEventType::ValueIncreased, label, previous, current, "value increased", 0.9));
    } else {
        out.push_back(makeEvent(frame, RunLabEventType::ValueDecreased, label, previous, current, "value decreased", 0.9));
    }

    const bool healthLike = semanticNameMatchesAny(label, {"lives", "life", "hp", "health", "player.hp", "player.health"});
    if (healthLike && current < previous) {
        out.push_back(makeEvent(frame, RunLabEventType::DamageCandidate, label, previous, current, "health or lives decreased", 0.78));
        if (current == 0) {
            out.push_back(makeEvent(frame, RunLabEventType::DeathCandidate, label, previous, current, "health or lives reached zero", 0.88));
        }
    }

    const bool levelLike = semanticNameMatchesAny(label, {"level_id", "stage", "stage_id", "room", "room_id", "map", "map_id"});
    if (levelLike) {
        out.push_back(makeEvent(frame, RunLabEventType::LevelChangeCandidate, label, previous, current, "level, stage, room, or map changed", 0.82));
        out.push_back(makeEvent(frame, RunLabEventType::SplitCandidate, label, previous, current, "possible split point", 0.74));
    }

    const bool stateLike = semanticNameMatchesAny(label, {"game_state", "state", "mode"});
    const bool clearValue = current == 2 || current == 3 || current == 4 || current == 0x10 || current == 0xFF;
    if (stateLike && clearValue) {
        out.push_back(makeEvent(frame, RunLabEventType::LevelClearCandidate, label, previous, current, "game_state entered a possible clear/victory value", 0.52));
    }

    const bool goalLike = semanticNameMatchesAny(label, {"goal_flag", "clear_flag", "finish_flag", "victory_flag"});
    if (goalLike && previous == 0 && current != 0) {
        out.push_back(makeEvent(frame, RunLabEventType::GoalReachedCandidate, label, previous, current, "goal or finish flag activated", 0.86));
    }

    return out;
}

bool eventIsImportant(const TimelineEvent& event) {
    switch (event.type) {
    case RunLabEventType::DamageCandidate:
    case RunLabEventType::DeathCandidate:
    case RunLabEventType::LevelChangeCandidate:
    case RunLabEventType::LevelClearCandidate:
    case RunLabEventType::SplitCandidate:
    case RunLabEventType::GoalReachedCandidate:
    case RunLabEventType::Custom:
        return true;
    default:
        return false;
    }
}

void clearEvents(State& state) {
    state.events.clear();
}

void updateTimeline(State& state, const gb::Bus& bus, std::uint64_t frame, std::size_t maxEvents) {
    for (auto& label : state.memoryLabels) {
        const std::int32_t now = readMemoryLabelValue(bus, label);
        if (label.hasLastValue && label.lastValue != now) {
            auto detected = detectEventsForChange(label, label.lastValue, now, frame);
            state.events.insert(state.events.end(), std::make_move_iterator(detected.begin()), std::make_move_iterator(detected.end()));
        }
        label.lastValue = now;
        label.hasLastValue = true;
    }
    evaluateGoalsAndSplits(state, bus, frame);
    if (state.events.size() > maxEvents) {
        state.events.erase(state.events.begin(), state.events.begin() + static_cast<std::ptrdiff_t>(state.events.size() - maxEvents));
    }
}

void captureRamSnapshot(State& state, const gb::Bus& bus) {
    state.ramSnapshot.assign(0x10000, 0);
    for (int address = 0; address <= 0xFFFF; ++address) {
        state.ramSnapshot[static_cast<std::size_t>(address)] = bus.peek(static_cast<gb::u16>(address));
    }
    state.hasRamSnapshot = true;
    state.lastDiff.clear();
}

std::size_t buildRamDiff(State& state, const gb::Bus& bus, std::size_t maxEntries) {
    state.lastDiff.clear();
    if (!state.hasRamSnapshot || state.ramSnapshot.size() != 0x10000) {
        return 0;
    }
    std::size_t total = 0;
    for (int address = 0; address <= 0xFFFF; ++address) {
        const auto addr = static_cast<gb::u16>(address);
        if (!ramDiffAddress(addr)) {
            continue;
        }
        const gb::u8 before = state.ramSnapshot[static_cast<std::size_t>(address)];
        const gb::u8 after = bus.peek(addr);
        if (before == after) {
            continue;
        }
        ++total;
        if (state.lastDiff.size() < maxEntries) {
            state.lastDiff.push_back(RamDiffEntry{addr, before, after});
        }
    }
    return total;
}

const MemoryLabel* findLabelByName(const std::vector<MemoryLabel>& labels, const std::string& name) {
    const std::string want = lowerText(name);
    for (const auto& label : labels) {
        if (lowerText(label.label) == want) {
            return &label;
        }
        if (!label.entity.empty() && !label.field.empty() && lowerText(label.entity + "." + label.field) == want) {
            return &label;
        }
    }
    return nullptr;
}

std::vector<MemoryLabel> snapshotLabels(const std::vector<MemoryLabel>& labels, const gb::Bus& bus) {
    std::vector<MemoryLabel> out = labels;
    for (auto& label : out) {
        label.lastValue = readMemoryLabelValue(bus, label);
        label.hasLastValue = true;
    }
    return out;
}

bool evaluateCondition(
    const GoalCondition& condition,
    const std::vector<MemoryLabel>& current,
    const std::vector<MemoryLabel>& baseline
) {
    const MemoryLabel* nowLabel = findLabelByName(current, condition.label);
    if (!nowLabel || !nowLabel->hasLastValue) {
        return false;
    }
    const MemoryLabel* baseLabel = findLabelByName(baseline, condition.label);
    const std::int32_t now = nowLabel->lastValue;
    const bool hasBase = baseLabel && baseLabel->hasLastValue;
    const std::int32_t base = hasBase ? baseLabel->lastValue : now;

    switch (condition.op) {
    case ConditionOperator::Changed:
    case ConditionOperator::ChangedFromInitial:
        return hasBase && now != base;
    case ConditionOperator::Equal:
        return now == condition.value;
    case ConditionOperator::NotEqual:
        return now != condition.value;
    case ConditionOperator::Greater:
        return now > condition.value;
    case ConditionOperator::GreaterEqual:
        return now >= condition.value;
    case ConditionOperator::Less:
        return now < condition.value;
    case ConditionOperator::LessEqual:
        return now <= condition.value;
    case ConditionOperator::Decreased:
        return hasBase && now < base;
    case ConditionOperator::Increased:
        return hasBase && now > base;
    case ConditionOperator::ChangedToNonzero:
        return hasBase && base == 0 && now != 0;
    }
    return false;
}

std::size_t createDefaultGoalFromLabels(State& state) {
    GoalDefinition goal{};
    goal.name = "finish_current_level";
    goal.description = "Finish current level from labeled memory";
    goal.optimize = "min_frames";

    if (findLabelByName(state.memoryLabels, "level_id")) {
        goal.done = GoalCondition{"level_id", ConditionOperator::ChangedFromInitial, 0};
    } else if (findLabelByName(state.memoryLabels, "player.x")) {
        const auto* label = findLabelByName(state.memoryLabels, "player.x");
        goal.name = "reach_x_position";
        goal.description = "Reach a farther player.x position";
        goal.done = GoalCondition{"player.x", ConditionOperator::GreaterEqual, label ? label->lastValue + 128 : 128};
    } else {
        goal.name = "runlab_goal";
        goal.description = "Default generated RunLab goal";
        goal.done = GoalCondition{"goal_flag", ConditionOperator::ChangedToNonzero, 0};
    }

    if (findLabelByName(state.memoryLabels, "lives")) {
        goal.fail.push_back(GoalCondition{"lives", ConditionOperator::Decreased, 0});
    } else if (findLabelByName(state.memoryLabels, "player.hp")) {
        goal.fail.push_back(GoalCondition{"player.hp", ConditionOperator::Decreased, 0});
    }

    state.goals.push_back(goal);
    return state.goals.size() - 1;
}

bool startGoal(State& state, std::size_t goalIndex, const gb::Bus& bus, std::uint64_t frame) {
    if (goalIndex >= state.goals.size()) {
        return false;
    }
    for (auto& goal : state.goals) {
        goal.active = false;
    }
    auto& goal = state.goals[goalIndex];
    goal.active = true;
    goal.doneTriggered = false;
    goal.failTriggered = false;
    goal.startFrame = frame;
    goal.doneFrame = 0;
    goal.failFrame = 0;
    goal.baseline = snapshotLabels(state.memoryLabels, bus);
    goal.lastEvent.clear();
    for (auto& split : state.splits) {
        split.baseline = goal.baseline;
        split.triggered = false;
        split.triggeredFrame = 0;
    }
    state.activeGoal = goalIndex;
    return true;
}

void resetActiveGoalBaseline(State& state, const gb::Bus& bus, std::uint64_t frame) {
    if (!state.activeGoal.has_value() || state.activeGoal.value() >= state.goals.size()) {
        return;
    }
    (void)startGoal(state, state.activeGoal.value(), bus, frame);
}

std::size_t createDefaultSplitFromLabels(State& state) {
    SplitDefinition split{};
    if (findLabelByName(state.memoryLabels, "level_id")) {
        split.name = "Level Clear";
        split.condition = GoalCondition{"level_id", ConditionOperator::ChangedFromInitial, 0};
        split.label = "level_id";
    } else if (findLabelByName(state.memoryLabels, "room_id")) {
        split.name = "Room Transition";
        split.condition = GoalCondition{"room_id", ConditionOperator::ChangedFromInitial, 0};
        split.label = "room_id";
    } else {
        split.name = "Goal Flag";
        split.condition = GoalCondition{"goal_flag", ConditionOperator::ChangedToNonzero, 0};
        split.label = "goal_flag";
    }
    state.splits.push_back(split);
    return state.splits.size() - 1;
}

void evaluateGoalsAndSplits(State& state, const gb::Bus& bus, std::uint64_t frame) {
    for (auto& label : state.memoryLabels) {
        label.lastValue = readMemoryLabelValue(bus, label);
        label.hasLastValue = true;
    }

    if (state.activeGoal.has_value() && state.activeGoal.value() < state.goals.size()) {
        auto& goal = state.goals[state.activeGoal.value()];
        if (goal.active && !goal.doneTriggered && !goal.failTriggered) {
            for (const auto& fail : goal.fail) {
                if (evaluateCondition(fail, state.memoryLabels, goal.baseline)) {
                    goal.failTriggered = true;
                    goal.failFrame = frame;
                    goal.active = false;
                    goal.lastEvent = "fail";
                    break;
                }
            }
            if (!goal.failTriggered && evaluateCondition(goal.done, state.memoryLabels, goal.baseline)) {
                goal.doneTriggered = true;
                goal.doneFrame = frame;
                goal.active = false;
                goal.lastEvent = "done";
                if (const auto* label = findLabelByName(state.memoryLabels, goal.done.label)) {
                    state.events.push_back(makeEvent(frame, RunLabEventType::GoalReachedCandidate, *label, 0, label->lastValue, "goal condition reached", 0.92));
                }
            }
        }
    }

    for (auto& split : state.splits) {
        if (split.triggered) {
            continue;
        }
        if (evaluateCondition(split.condition, state.memoryLabels, split.baseline)) {
            split.triggered = true;
            split.triggeredFrame = frame;
            if (const auto* label = findLabelByName(state.memoryLabels, split.condition.label)) {
                split.label = label->label;
                split.entity = label->entity;
                state.events.push_back(makeEvent(frame, RunLabEventType::SplitCandidate, *label, 0, label->lastValue, "split condition triggered", 0.9));
            }
        }
    }
}

bool refreshSelectedEntityBounds(State& state, const gb::Bus& bus) {
    if (!state.selectedEntity.has_value() || state.selectedEntity.value() >= state.entities.size()) {
        return false;
    }
    auto& entity = state.entities[state.selectedEntity.value()];
    if (entity.oamIndices.empty()) {
        return false;
    }

    bool any = false;
    SpriteBounds merged{};
    for (const int idx : entity.oamIndices) {
        if (idx < 0 || idx >= 40) {
            continue;
        }
        const gb::u16 base = static_cast<gb::u16>(0xFE00 + idx * 4);
        const OamSpriteRef sprite{
            base,
            bus.peek(base),
            bus.peek(static_cast<gb::u16>(base + 1)),
            bus.peek(static_cast<gb::u16>(base + 2)),
            bus.peek(static_cast<gb::u16>(base + 3)),
        };
        const auto bounds = spriteBounds(sprite, bus);
        if (!any) {
            merged = bounds;
            any = true;
        } else {
            const int left = std::min(merged.x, bounds.x);
            const int top = std::min(merged.y, bounds.y);
            const int right = std::max(merged.x + merged.w, bounds.x + bounds.w);
            const int bottom = std::max(merged.y + merged.h, bounds.y + bounds.h);
            merged = SpriteBounds{left, top, right - left, bottom - top};
        }
    }
    if (!any) {
        return false;
    }
    entity.lastBounds = merged;
    return true;
}

bool captureCorrelationSample(State& state, const gb::Bus& bus, std::uint64_t frame) {
    if (!refreshSelectedEntityBounds(state, bus)) {
        return false;
    }
    const auto& entity = state.entities[state.selectedEntity.value()];
    if (!state.correlationSamples.empty() && state.correlationSamples.back().frame == frame) {
        return false;
    }

    CorrelationSample sample{};
    sample.frame = frame;
    sample.entityX = entity.lastBounds.x;
    sample.entityY = entity.lastBounds.y;
    sample.wram.assign(0x2000, 0);
    for (int i = 0; i < 0x2000; ++i) {
        sample.wram[static_cast<std::size_t>(i)] = bus.peek(static_cast<gb::u16>(0xC000 + i));
    }
    state.correlationSamples.push_back(std::move(sample));
    while (state.correlationSamples.size() > state.correlationFrameLimit) {
        state.correlationSamples.pop_front();
    }
    return true;
}

CorrelationScanResult analyzeCorrelationSamples(const std::deque<CorrelationSample>& samples, std::size_t maxPerGroup) {
    CorrelationScanResult result{};
    result.sampleCount = samples.size();
    if (samples.size() < 3) {
        return result;
    }

    std::vector<int> xs;
    std::vector<int> ys;
    xs.reserve(samples.size());
    ys.reserve(samples.size());
    for (const auto& sample : samples) {
        xs.push_back(sample.entityX);
        ys.push_back(sample.entityY);
    }

    const int xChanges = changedFrameCount(xs);
    const int yChanges = changedFrameCount(ys);
    if (xChanges == 0 && yChanges == 0) {
        return result;
    }

    const std::array<MemoryValueType, 2> types{MemoryValueType::U8, MemoryValueType::U16Le};
    for (std::size_t offset = 0; offset < 0x2000; ++offset) {
        for (const MemoryValueType type : types) {
            if (type == MemoryValueType::U16Le && offset + 1 >= 0x2000) {
                continue;
            }
            std::vector<int> values;
            values.reserve(samples.size());
            for (const auto& sample : samples) {
                values.push_back(readSampleValue(sample, offset, type));
            }

            const int changes = changedFrameCount(values);
            if (changes == 0) {
                continue;
            }
            const double changeRatio = static_cast<double>(changes) / static_cast<double>(samples.size() - 1);
            const gb::u16 address = static_cast<gb::u16>(0xC000 + offset);
            const std::int32_t current = values.back();

            const double xMove = movementTriggeredChangeRatio(xs, values);
            const double xSign = signedDeltaAgreement(xs, values, false);
            const double xClose = closenessScore(xs, values);
            const double xNoisePenalty = changeRatio > 0.85 ? (changeRatio - 0.85) * 1.8 * (1.0 - std::max(xMove, xSign)) : 0.0;
            const double xScore = clamp01((xMove * 0.42) + (xSign * 0.34) + (xClose * 0.24) - xNoisePenalty);
            addTopCandidate(
                result.entityX,
                CorrelationCandidate{address, type, xScore, current, "moves with selected entity X"},
                maxPerGroup
            );

            const double yMove = movementTriggeredChangeRatio(ys, values);
            const double ySign = signedDeltaAgreement(ys, values, false);
            const double yClose = closenessScore(ys, values);
            const double yNoisePenalty = changeRatio > 0.85 ? (changeRatio - 0.85) * 1.8 * (1.0 - std::max(yMove, ySign)) : 0.0;
            const double yScore = clamp01((yMove * 0.42) + (ySign * 0.34) + (yClose * 0.24) - yNoisePenalty);
            addTopCandidate(
                result.entityY,
                CorrelationCandidate{address, type, yScore, current, "moves with selected entity Y"},
                maxPerGroup
            );

            const double camSign = signedDeltaAgreement(xs, values, true);
            const double camMove = movementTriggeredChangeRatio(xs, values);
            const double camNoisePenalty = changeRatio > 0.85 ? (changeRatio - 0.85) * 1.8 * (1.0 - std::max(camMove, camSign)) : 0.0;
            const double camScore = clamp01((camMove * 0.46) + (camSign * 0.42) + (1.0 - xClose) * 0.12 - camNoisePenalty);
            addTopCandidate(
                result.cameraX,
                CorrelationCandidate{address, type, camScore, current, "possible camera/global scroll"},
                maxPerGroup
            );

            const std::size_t uniques = uniqueValueCount(values);
            const double compact = uniques <= 1 ? 0.0 : clamp01(1.0 - static_cast<double>(uniques - 2) / 14.0);
            const double stateMove = std::max(xMove, yMove);
            const double stateNoisePenalty = changeRatio > 0.85 ? (changeRatio - 0.85) * 1.8 * (1.0 - stateMove) : 0.0;
            const double stateScore = clamp01((compact * 0.48) + (stateMove * 0.34) + ((changeRatio < 0.45) ? 0.18 : 0.0) - stateNoisePenalty);
            addTopCandidate(
                result.state,
                CorrelationCandidate{address, type, stateScore, current, "changes with movement state"},
                maxPerGroup
            );
        }
    }
    return result;
}

bool runCorrelationScan(State& state, std::size_t maxPerGroup) {
    if (!state.selectedEntity.has_value() || state.selectedEntity.value() >= state.entities.size()) {
        state.correlationResult = CorrelationScanResult{};
        return false;
    }
    state.correlationResult = analyzeCorrelationSamples(state.correlationSamples, maxPerGroup);
    return state.correlationResult.sampleCount >= 3;
}

std::string exportProfileJson(const State& state, const std::string& gameName) {
    std::ostringstream out;
    const auto writeCondition = [&](const GoalCondition& condition, int indent) {
        const std::string pad(static_cast<std::size_t>(indent), ' ');
        out << pad << "{\n";
        out << pad << "  \"label\": \"" << jsonEscape(condition.label) << "\",\n";
        out << pad << "  \"operator\": \"" << conditionOperatorName(condition.op) << "\"";
        switch (condition.op) {
        case ConditionOperator::Equal:
        case ConditionOperator::NotEqual:
        case ConditionOperator::Greater:
        case ConditionOperator::GreaterEqual:
        case ConditionOperator::Less:
        case ConditionOperator::LessEqual:
            out << ",\n" << pad << "  \"value\": " << condition.value << "\n";
            break;
        default:
            out << "\n";
            break;
        }
        out << pad << "}";
    };

    out << "{\n";
    out << "  \"game\": {\n";
    out << "    \"name\": \"" << jsonEscape(gameName) << "\",\n";
    out << "    \"platform\": \"gb\",\n";
    out << "    \"rom_sha1\": null\n";
    out << "  },\n";
    out << "  \"entities\": [\n";
    for (std::size_t i = 0; i < state.entities.size(); ++i) {
        const auto& e = state.entities[i];
        out << "    {\n";
        out << "      \"label\": \"" << jsonEscape(e.label) << "\",\n";
        out << "      \"type\": \"" << entityTypeName(e.type) << "\",\n";
        out << "      \"oam_indices\": [";
        for (std::size_t j = 0; j < e.oamIndices.size(); ++j) {
            if (j != 0) out << ", ";
            out << e.oamIndices[j];
        }
        out << "],\n";
        out << "      \"last_bbox\": { \"x\": " << e.lastBounds.x
            << ", \"y\": " << e.lastBounds.y
            << ", \"w\": " << e.lastBounds.w
            << ", \"h\": " << e.lastBounds.h << " }\n";
        out << "    }" << (i + 1 == state.entities.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"memory_labels\": [\n";
    for (std::size_t i = 0; i < state.memoryLabels.size(); ++i) {
        const auto& m = state.memoryLabels[i];
        out << "    {\n";
        out << "      \"label\": \"" << jsonEscape(m.label) << "\",\n";
        out << "      \"address\": \"" << formatAddress(m.address) << "\",\n";
        out << "      \"type\": \"" << memoryValueTypeName(m.type) << "\"";
        if (!m.entity.empty()) {
            out << ",\n      \"entity\": \"" << jsonEscape(m.entity) << "\"";
        }
        if (!m.field.empty()) {
            out << ",\n      \"field\": \"" << jsonEscape(m.field) << "\"";
        }
        if (!m.notes.empty()) {
            out << ",\n      \"notes\": \"" << jsonEscape(m.notes) << "\"";
        }
        out << "\n    }" << (i + 1 == state.memoryLabels.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"event_detection_rules\": {\n";
    out << "    \"damage_candidate\": \"lives/hp/health decreased\",\n";
    out << "    \"death_candidate\": \"lives/hp/health reached zero\",\n";
    out << "    \"level_change_candidate\": \"level_id/stage/room/map changed\",\n";
    out << "    \"split_candidate\": \"level_id/stage/room/map changed\",\n";
    out << "    \"level_clear_candidate\": \"game_state entered possible clear/victory value\",\n";
    out << "    \"goal_reached_candidate\": \"goal_flag/clear_flag/finish_flag changed from zero to nonzero\"\n";
    out << "  },\n";
    out << "  \"goals\": [\n";
    for (std::size_t i = 0; i < state.goals.size(); ++i) {
        const auto& goal = state.goals[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(goal.name) << "\",\n";
        out << "      \"description\": \"" << jsonEscape(goal.description) << "\",\n";
        out << "      \"done\": ";
        writeCondition(goal.done, 6);
        out << ",\n";
        out << "      \"fail\": [\n";
        for (std::size_t j = 0; j < goal.fail.size(); ++j) {
            writeCondition(goal.fail[j], 8);
            out << (j + 1 == goal.fail.size() ? "\n" : ",\n");
        }
        out << "      ],\n";
        out << "      \"optimize\": \"" << jsonEscape(goal.optimize) << "\"\n";
        out << "    }" << (i + 1 == state.goals.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"splits\": [\n";
    for (std::size_t i = 0; i < state.splits.size(); ++i) {
        const auto& split = state.splits[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(split.name) << "\",\n";
        out << "      \"condition\": ";
        writeCondition(split.condition, 6);
        if (!split.label.empty()) {
            out << ",\n      \"label\": \"" << jsonEscape(split.label) << "\"";
        }
        if (!split.entity.empty()) {
            out << ",\n      \"entity\": \"" << jsonEscape(split.entity) << "\"";
        }
        out << "\n    }" << (i + 1 == state.splits.size() ? "" : ",") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

bool exportProfileFile(State& state, const std::string& gameName, const std::string& profilesDir) {
    std::filesystem::create_directories(profilesDir);
    const std::filesystem::path path = std::filesystem::path(profilesDir) / (safeFileStem(gameName) + ".runlab.json");
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    file << exportProfileJson(state, gameName);
    if (!file) {
        return false;
    }
    state.lastExportPath = path.string();
    return true;
}

std::string exportCurrentStateJson(
    const State& state,
    const std::string& gameName,
    std::uint64_t frame,
    bool paused,
    bool running,
    const std::string& profilePath,
    const std::string& screenshotPath
) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"status\": {\n";
    out << "    \"rom_name\": \"" << jsonEscape(gameName) << "\",\n";
    out << "    \"frame\": " << frame << ",\n";
    out << "    \"running\": " << (running ? "true" : "false") << ",\n";
    out << "    \"paused\": " << (paused ? "true" : "false");
    if (!profilePath.empty()) {
        out << ",\n    \"profile_path\": \"" << jsonEscape(profilePath) << "\"";
    }
    if (!screenshotPath.empty()) {
        out << ",\n    \"screenshot_path\": \"" << jsonEscape(screenshotPath) << "\"";
    }
    out << "\n  },\n";

    out << "  \"entities\": [\n";
    for (std::size_t i = 0; i < state.entities.size(); ++i) {
        const auto& e = state.entities[i];
        out << "    {\n";
        out << "      \"label\": \"" << jsonEscape(e.label) << "\",\n";
        out << "      \"type\": \"" << entityTypeName(e.type) << "\",\n";
        out << "      \"oam_indices\": [";
        for (std::size_t j = 0; j < e.oamIndices.size(); ++j) {
            if (j != 0) out << ", ";
            out << e.oamIndices[j];
        }
        out << "],\n";
        out << "      \"selected\": " << (state.selectedEntity.has_value() && state.selectedEntity.value() == i ? "true" : "false") << ",\n";
        out << "      \"last_bbox\": { \"x\": " << e.lastBounds.x
            << ", \"y\": " << e.lastBounds.y
            << ", \"w\": " << e.lastBounds.w
            << ", \"h\": " << e.lastBounds.h << " }\n";
        out << "    }" << (i + 1 == state.entities.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";

    out << "  \"memory_labels\": [\n";
    for (std::size_t i = 0; i < state.memoryLabels.size(); ++i) {
        const auto& m = state.memoryLabels[i];
        out << "    {\n";
        out << "      \"label\": \"" << jsonEscape(m.label) << "\",\n";
        out << "      \"address\": \"" << formatAddress(m.address) << "\",\n";
        out << "      \"type\": \"" << memoryValueTypeName(m.type) << "\"";
        if (!m.entity.empty()) out << ",\n      \"entity\": \"" << jsonEscape(m.entity) << "\"";
        if (!m.field.empty()) out << ",\n      \"field\": \"" << jsonEscape(m.field) << "\"";
        if (!m.notes.empty()) out << ",\n      \"notes\": \"" << jsonEscape(m.notes) << "\"";
        if (m.hasLastValue) out << ",\n      \"current_value\": " << m.lastValue;
        out << "\n    }" << (i + 1 == state.memoryLabels.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";

    out << "  \"events\": [\n";
    for (std::size_t i = 0; i < state.events.size(); ++i) {
        const auto& e = state.events[i];
        out << "    {\n";
        out << "      \"frame\": " << e.frame << ",\n";
        out << "      \"type\": \"" << eventTypeName(e.type) << "\",\n";
        out << "      \"label\": \"" << jsonEscape(e.label) << "\",\n";
        out << "      \"previous\": " << e.previous << ",\n";
        out << "      \"current\": " << e.current << ",\n";
        out << "      \"entity\": \"" << jsonEscape(e.entity) << "\",\n";
        out << "      \"description\": \"" << jsonEscape(e.description) << "\",\n";
        out << "      \"confidence\": " << e.confidence << ",\n";
        out << "      \"reason\": \"" << jsonEscape(e.reason) << "\"\n";
        out << "    }" << (i + 1 == state.events.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";

    out << "  \"goal\": ";
    if (state.activeGoal.has_value() && state.activeGoal.value() < state.goals.size()) {
        const auto& goal = state.goals[state.activeGoal.value()];
        out << "{\n";
        out << "    \"active\": true,\n";
        out << "    \"name\": \"" << jsonEscape(goal.name) << "\",\n";
        out << "    \"description\": \"" << jsonEscape(goal.description) << "\",\n";
        out << "    \"done_triggered\": " << (goal.doneTriggered ? "true" : "false") << ",\n";
        out << "    \"fail_triggered\": " << (goal.failTriggered ? "true" : "false") << ",\n";
        out << "    \"start_frame\": " << goal.startFrame << ",\n";
        out << "    \"done_frame\": " << goal.doneFrame << ",\n";
        out << "    \"fail_frame\": " << goal.failFrame << ",\n";
        out << "    \"last_event\": \"" << jsonEscape(goal.lastEvent) << "\"\n";
        out << "  },\n";
    } else {
        out << "{ \"active\": false },\n";
    }

    out << "  \"splits\": [\n";
    for (std::size_t i = 0; i < state.splits.size(); ++i) {
        const auto& split = state.splits[i];
        out << "    {\n";
        out << "      \"name\": \"" << jsonEscape(split.name) << "\",\n";
        out << "      \"label\": \"" << jsonEscape(split.label) << "\",\n";
        out << "      \"entity\": \"" << jsonEscape(split.entity) << "\",\n";
        out << "      \"triggered\": " << (split.triggered ? "true" : "false") << ",\n";
        out << "      \"triggered_frame\": " << split.triggeredFrame << "\n";
        out << "    }" << (i + 1 == state.splits.size() ? "" : ",") << "\n";
    }
    out << "  ],\n";
    out << "  \"profile\": " << exportProfileJson(state, gameName);
    out << "}\n";
    return out.str();
}

bool exportCurrentStateFile(
    const State& state,
    const std::string& gameName,
    std::uint64_t frame,
    bool paused,
    bool running,
    const std::string& statePath,
    const std::string& profilePath,
    const std::string& screenshotPath
) {
    const std::filesystem::path path(statePath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        return false;
    }
    file << exportCurrentStateJson(state, gameName, frame, paused, running, profilePath, screenshotPath);
    return static_cast<bool>(file);
}

} // namespace gb::frontend::runlab
