#pragma once

#include <optional>
#include <vector>

namespace gb::frontend {

struct TopMenuRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class TopMenuSection : int {
    Session = 0,
    Image = 1,
    Debug = 2,
    Controls = 3,
    Network = 4,
    Count = 5,
};

enum class TopMenuAction : int {
    None = 0,
    TogglePause,
    ToggleMute,
    ToggleFastForward,
    SaveState,
    LoadState,
    BackToMenu,
    ExitApp,
    ToggleFullscreen,
    ToggleScaleMenu,
    TogglePaletteMenu,
    CycleFilter,
    CaptureFrame,
    ToggleDebugPanel,
    ToggleBreakpointMenu,
    ToggleSearchPanel,
    ToggleRunLabMcpBridge,
    RunLabExportProfile,
    OpenControlsMenu,
    CycleLinkMode,
    NetplayDelayDown,
    NetplayDelayUp,
};

struct TopMenuItem {
    TopMenuAction action = TopMenuAction::None;
    const char* label = "";
};

int topMenuBarHeight();
int topMenuItemHeight();

bool topMenuRectContains(const TopMenuRect& rect, int px, int py);

const char* topMenuSectionLabel(TopMenuSection section);
const std::vector<TopMenuItem>& topMenuItems(TopMenuSection section);

TopMenuRect topMenuSectionRect(int outputW, TopMenuSection section);
TopMenuRect topMenuDropdownRect(int outputW, TopMenuSection section);

std::optional<TopMenuSection> hitTestTopMenuSection(int outputW, int px, int py);
std::optional<TopMenuAction> hitTestTopMenuAction(int outputW, TopMenuSection section, int px, int py);

} // namespace gb::frontend
