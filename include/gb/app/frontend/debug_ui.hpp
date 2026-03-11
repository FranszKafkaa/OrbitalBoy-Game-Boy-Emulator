#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gb/core/bus.hpp"
#include "gb/core/gameboy.hpp"

#ifdef GBEMU_USE_SDL2
#include <SDL2/SDL.h>

namespace gb::frontend {

struct SpriteDebugRow {
    gb::u16 addr = 0;
    gb::u8 y = 0;
    gb::u8 x = 0;
    gb::u8 tile = 0;
    gb::u8 attr = 0;
};

struct MemoryWatch {
    gb::u16 address = 0xC000;
    std::array<gb::u8, 96> history{};
    std::size_t count = 0;
    std::size_t head = 0;
    bool freeze = false;
    gb::u8 freezeValue = 0;
};

struct MemoryEditState {
    bool active = false;
    bool editAddress = true;
    std::string addressHex;
    std::string valueHex;
};

struct MemoryWriteUiState {
    bool pending = false;
    gb::u16 pendingAddress = 0;
    gb::u8 pendingValue = 0;
    bool hasLast = false;
    bool lastOk = false;
    gb::u16 lastAddress = 0;
    gb::u8 lastValue = 0;
    std::uint64_t lastFrame = 0;
    std::string lastTag;
};

enum class MemorySearchMode {
    Exact = 0,
    Greater = 1,
    Less = 2,
    Changed = 3,
    Unchanged = 4,
};

struct MemorySearchUiState {
    bool visible = false;
    bool editingValue = false;
    MemorySearchMode mode = MemorySearchMode::Exact;
    std::string valueHex = "00";
    bool hasSnapshot = false;
    std::size_t totalMatches = 0;
    int scroll = 0;
    std::vector<gb::u16> matches{};
};

inline constexpr int kReadLineHeight = 14;
inline constexpr int kReadLines = 8;
inline constexpr int kBreakpointMenuTopY = 104;
inline constexpr int kBreakpointRowHeight = 12;
inline constexpr int kBreakpointRowYWatch = kBreakpointMenuTopY + 14;
inline constexpr int kBreakpointRowYPc = kBreakpointMenuTopY + 26;
inline constexpr int kBreakpointRowYAddr = kBreakpointMenuTopY + 38;
inline constexpr int kBreakpointListStartY = kBreakpointMenuTopY + 50;
inline constexpr int kBreakpointListLineHeight = 12;
inline constexpr int kBreakpointListMaxVisible = 4;
inline constexpr int kSearchOverlayTop = 140;
inline constexpr int kSearchOverlayBottomPad = 18;
inline constexpr int kSearchListYOffset = 74;
inline constexpr int kSearchListLineHeight = 12;
inline constexpr int kReadStartYWithoutBreakpointMenu = 118;
inline constexpr int kReadStartYWithBreakpointMenu = 214;
inline constexpr int kSelectedSectionTopGap = 6;
inline constexpr int kSelectedSectionHeight = 96;
inline constexpr int kSectionGap = 6;
inline constexpr int kSpriteHeaderOffset = 6;
inline constexpr int kSpriteSectionTopPad = 18;
inline constexpr int kSpriteLineHeight = 12;

void setButtonFromKey(gb::GameBoy& gb, int key, bool pressed);
void drawHexText(SDL_Renderer* renderer, int x, int y, const std::string& text, SDL_Color color, int scale);

void resetMemoryWatch(MemoryWatch& watch, const gb::Bus& bus);
void sampleMemoryWatch(MemoryWatch& watch, const gb::Bus& bus);
std::optional<gb::u16> parseHex16(const std::string& hex);
std::optional<gb::u8> parseHex8(const std::string& hex);
bool likelyWritableAddress(gb::u16 addr);

int readStartYFromLayout(bool showBreakpointMenu);
int spriteListYFromLayout(bool showBreakpointMenu);
int spriteVisibleLinesForPanel(int panelHeight, bool showBreakpointMenu);
int searchVisibleLinesForPanel(int panelHeight);
std::vector<SpriteDebugRow> snapshotSprites(const gb::Bus& bus);
std::optional<SpriteDebugRow> findSelectedSprite(
    const std::vector<SpriteDebugRow>& sprites,
    std::optional<gb::u16> selectedAddr
);

void drawSelectedSpriteOverlay(
    SDL_Renderer* renderer,
    const gb::Bus& bus,
    const std::optional<SpriteDebugRow>& selected,
    int scale,
    int gameX,
    int gameY
);

void drawMemoryPanel(
    SDL_Renderer* renderer,
    int panelX,
    int panelWidth,
    int panelHeight,
    const std::vector<gb::Bus::MemoryReadEvent>& reads,
    const std::vector<SpriteDebugRow>& sprites,
    int spriteScrollRows,
    const MemoryWatch& watch,
    const MemoryWriteUiState& writeUi,
    const MemorySearchUiState& search,
    const std::vector<std::string>& disasmLines,
    bool showBreakpointMenu,
    bool watchpointEnabled,
    const std::vector<gb::u16>& breakpoints,
    const std::string& breakpointAddressHex,
    bool breakpointAddressEditing,
    std::optional<gb::u16> selectedSpriteAddr,
    const gb::Bus& bus,
    gb::u16 execPc,
    gb::u8 execOp,
    gb::u16 nextPc,
    gb::u8 nextOp,
    bool paused,
    bool muted
);

void drawMemoryEditOverlay(SDL_Renderer* renderer, int panelX, int panelWidth, const MemoryEditState& edit);

} // namespace gb::frontend
#endif
