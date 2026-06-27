#include "gb/app/frontend/runlab.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

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

void updateTimeline(State& state, const gb::Bus& bus, std::uint64_t frame, std::size_t maxEvents) {
    for (auto& label : state.memoryLabels) {
        const std::int32_t now = readMemoryLabelValue(bus, label);
        if (label.hasLastValue && label.lastValue != now) {
            state.events.push_back(TimelineEvent{frame, label.label, label.lastValue, now});
        }
        label.lastValue = now;
        label.hasLastValue = true;
    }
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
    out << "  \"events\": {\n";
    out << "    \"death_candidate\": \"lives < previous.lives\",\n";
    out << "    \"level_change_candidate\": \"level_id != previous.level_id\"\n";
    out << "  }\n";
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

} // namespace gb::frontend::runlab
