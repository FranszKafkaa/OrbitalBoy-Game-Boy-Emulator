#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "gb/core/bus.hpp"
#include "gb/core/types.hpp"

namespace gb::frontend::runlab {

enum class EntityType {
    Unknown = 0,
    Player,
    Enemy,
    Item,
    Boss,
};

enum class MemoryValueType {
    U8 = 0,
    I8,
    U16Le,
    I16Le,
    Hex8,
    Hex16,
};

enum class RunLabEventType {
    MemoryChanged = 0,
    ValueIncreased,
    ValueDecreased,
    DamageCandidate,
    DeathCandidate,
    LevelChangeCandidate,
    LevelClearCandidate,
    SplitCandidate,
    GoalReachedCandidate,
    Custom,
};

enum class ConditionOperator {
    Changed = 0,
    ChangedFromInitial,
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    Decreased,
    Increased,
    ChangedToNonzero,
};

struct SpriteBounds {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct OamSpriteRef {
    gb::u16 addr = 0;
    gb::u8 y = 0;
    gb::u8 x = 0;
    gb::u8 tile = 0;
    gb::u8 attr = 0;
};

struct EntityCandidate {
    std::string label;
    EntityType type = EntityType::Unknown;
    std::vector<int> oamIndices{};
    SpriteBounds lastBounds{};
};

struct MemoryLabel {
    std::string label;
    gb::u16 address = 0;
    MemoryValueType type = MemoryValueType::U8;
    std::string entity;
    std::string field;
    std::string notes;
    bool hasLastValue = false;
    std::int32_t lastValue = 0;
};

struct TimelineEvent {
    std::uint64_t frame = 0;
    RunLabEventType type = RunLabEventType::MemoryChanged;
    std::string label;
    std::int32_t previous = 0;
    std::int32_t current = 0;
    std::string entity;
    std::string description;
    double confidence = 1.0;
    std::string semantic;
    std::string reason;
};

struct GoalCondition {
    std::string label;
    ConditionOperator op = ConditionOperator::Changed;
    std::int32_t value = 0;
};

struct GoalDefinition {
    std::string name;
    std::string description;
    GoalCondition done{};
    std::vector<GoalCondition> fail{};
    std::string optimize = "min_frames";
    bool active = false;
    bool doneTriggered = false;
    bool failTriggered = false;
    std::uint64_t startFrame = 0;
    std::uint64_t doneFrame = 0;
    std::uint64_t failFrame = 0;
    std::vector<MemoryLabel> baseline{};
    std::string lastEvent;
};

struct SplitDefinition {
    std::string name;
    GoalCondition condition{};
    bool triggered = false;
    std::uint64_t triggeredFrame = 0;
    std::string label;
    std::string entity;
    std::vector<MemoryLabel> baseline{};
};

struct RamDiffEntry {
    gb::u16 address = 0;
    gb::u8 before = 0;
    gb::u8 after = 0;
};

struct CorrelationSample {
    std::uint64_t frame = 0;
    int entityX = 0;
    int entityY = 0;
    std::vector<gb::u8> wram{};
};

enum class CorrelationTarget {
    EntityX = 0,
    EntityY,
    CameraX,
    State,
    Custom,
};

struct CorrelationCandidate {
    gb::u16 address = 0;
    MemoryValueType type = MemoryValueType::U8;
    double score = 0.0;
    std::int32_t currentValue = 0;
    std::string reason;
};

struct CorrelationScanResult {
    std::vector<CorrelationCandidate> entityX{};
    std::vector<CorrelationCandidate> entityY{};
    std::vector<CorrelationCandidate> cameraX{};
    std::vector<CorrelationCandidate> state{};
    std::size_t sampleCount = 0;
};

struct State {
    std::vector<EntityCandidate> entities{};
    std::vector<MemoryLabel> memoryLabels{};
    std::vector<TimelineEvent> events{};
    std::vector<GoalDefinition> goals{};
    std::vector<SplitDefinition> splits{};
    std::optional<std::size_t> activeGoal{};
    std::optional<std::size_t> selectedEntity{};
    bool hasRamSnapshot = false;
    std::vector<gb::u8> ramSnapshot{};
    std::vector<RamDiffEntry> lastDiff{};
    std::size_t nextEntityId = 1;
    std::size_t nextMemoryLabelId = 1;
    std::string lastExportPath{};
    std::size_t correlationFrameLimit = 120;
    std::deque<CorrelationSample> correlationSamples{};
    CorrelationScanResult correlationResult{};
};

const char* entityTypeName(EntityType type);
const char* memoryValueTypeName(MemoryValueType type);
const char* eventTypeName(RunLabEventType type);
const char* conditionOperatorName(ConditionOperator op);
EntityType nextEntityType(EntityType type);

int oamIndexFromAddress(gb::u16 address);
SpriteBounds spriteBounds(const OamSpriteRef& sprite, const gb::Bus& bus);
std::string formatAddress(gb::u16 address);
std::int32_t readMemoryLabelValue(const gb::Bus& bus, const MemoryLabel& label);

EntityCandidate makeEntityCandidate(const OamSpriteRef& sprite, const gb::Bus& bus, const std::string& label, EntityType type);
std::size_t createOrSelectEntityFromSprite(State& state, const OamSpriteRef& sprite, const gb::Bus& bus);
bool updateSelectedEntitySprite(State& state, const OamSpriteRef& sprite, const gb::Bus& bus);
bool cycleSelectedEntityType(State& state);

MemoryLabel makeMemoryLabel(
    const std::string& label,
    gb::u16 address,
    MemoryValueType type,
    const std::string& entity = {},
    const std::string& field = {}
);
std::size_t addMemoryLabelForWatch(State& state, gb::u16 address);
std::size_t promoteDiffAddress(State& state, std::size_t diffIndex);
std::size_t promoteCorrelationCandidate(State& state, CorrelationTarget target, std::size_t candidateIndex = 0);

void updateTimeline(State& state, const gb::Bus& bus, std::uint64_t frame, std::size_t maxEvents = 200);
std::vector<TimelineEvent> detectEventsForChange(const MemoryLabel& label, std::int32_t previous, std::int32_t current, std::uint64_t frame);
bool eventIsImportant(const TimelineEvent& event);
void clearEvents(State& state);
void captureRamSnapshot(State& state, const gb::Bus& bus);
std::size_t buildRamDiff(State& state, const gb::Bus& bus, std::size_t maxEntries = 256);

bool evaluateCondition(
    const GoalCondition& condition,
    const std::vector<MemoryLabel>& current,
    const std::vector<MemoryLabel>& baseline
);
std::size_t createDefaultGoalFromLabels(State& state);
bool startGoal(State& state, std::size_t goalIndex, const gb::Bus& bus, std::uint64_t frame);
void resetActiveGoalBaseline(State& state, const gb::Bus& bus, std::uint64_t frame);
void evaluateGoalsAndSplits(State& state, const gb::Bus& bus, std::uint64_t frame);
std::size_t createDefaultSplitFromLabels(State& state);

bool refreshSelectedEntityBounds(State& state, const gb::Bus& bus);
bool captureCorrelationSample(State& state, const gb::Bus& bus, std::uint64_t frame);
CorrelationScanResult analyzeCorrelationSamples(const std::deque<CorrelationSample>& samples, std::size_t maxPerGroup = 3);
bool runCorrelationScan(State& state, std::size_t maxPerGroup = 3);

std::string exportProfileJson(const State& state, const std::string& gameName);
bool exportProfileFile(State& state, const std::string& gameName, const std::string& profilesDir = "profiles");
std::string exportCurrentStateJson(
    const State& state,
    const std::string& gameName,
    std::uint64_t frame,
    bool paused,
    bool running,
    const std::string& profilePath = {},
    const std::string& screenshotPath = {},
    const std::string& controlStatusJson = {}
);
bool exportCurrentStateFile(
    const State& state,
    const std::string& gameName,
    std::uint64_t frame,
    bool paused,
    bool running,
    const std::string& statePath = ".runlab/current-state.json",
    const std::string& profilePath = {},
    const std::string& screenshotPath = {},
    const std::string& controlStatusJson = {}
);

} // namespace gb::frontend::runlab
