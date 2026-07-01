#include "gb/app/frontend/realtime/top_menu.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace gb::frontend {

namespace {

constexpr int kBarHeight = 22;
constexpr int kSectionY = 2;
constexpr int kSectionHeight = 18;
constexpr int kSectionStartX = 8;
constexpr int kSectionGap = 6;
constexpr int kSectionPad = 12;
constexpr int kItemHeight = 16;
constexpr int kDropdownPad = 4;
constexpr int kCharWidth = 6;

constexpr std::array<const char*, static_cast<std::size_t>(TopMenuSection::Count)> kSectionLabels{
    "SESSAO",
    "IMAGEM",
    "DEBUG",
    "CONTROLES",
    "REDE",
};

const std::vector<TopMenuItem> kSessionItems{
    {TopMenuAction::TogglePause, "PAUSAR CONTINUAR"},
    {TopMenuAction::ToggleMute, "MUTAR AUDIO"},
    {TopMenuAction::ToggleFastForward, "FAST FORWARD"},
    {TopMenuAction::SaveState, "SALVAR STATE"},
    {TopMenuAction::LoadState, "CARREGAR STATE"},
    {TopMenuAction::BackToMenu, "VOLTAR MENU ROM"},
    {TopMenuAction::ExitApp, "SAIR"},
};

const std::vector<TopMenuItem> kImageItems{
    {TopMenuAction::ToggleFullscreen, "FULLSCREEN"},
    {TopMenuAction::ToggleScaleMenu, "MENU ESCALA"},
    {TopMenuAction::TogglePaletteMenu, "MENU PALETA"},
    {TopMenuAction::CycleFilter, "CICLAR FILTRO"},
    {TopMenuAction::CaptureFrame, "CAPTURA TELA"},
};

const std::vector<TopMenuItem> kDebugItems{
    {TopMenuAction::ToggleDebugPanel, "MOSTRAR DEBUG"},
    {TopMenuAction::ToggleBreakpointMenu, "MENU BP WP"},
    {TopMenuAction::ToggleSearchPanel, "BUSCA MEMORIA"},
    {TopMenuAction::ToggleRunLabMcpBridge, "RUNLAB MCP"},
    {TopMenuAction::RunLabExportProfile, "RUNLAB EXPORT"},
};

const std::vector<TopMenuItem> kControlsItems{
    {TopMenuAction::OpenControlsMenu, "ABRIR MENU F11"},
};

const std::vector<TopMenuItem> kNetworkItems{
    {TopMenuAction::CycleLinkMode, "CICLAR LINK"},
    {TopMenuAction::NetplayDelayDown, "DELAY NETPLAY -"},
    {TopMenuAction::NetplayDelayUp, "DELAY NETPLAY +"},
};

std::size_t sectionIndex(TopMenuSection section) {
    return static_cast<std::size_t>(section);
}

int textPixelWidth(const char* text) {
    if (!text) {
        return 0;
    }
    int count = 0;
    while (text[count] != '\0') {
        ++count;
    }
    return count * kCharWidth;
}

int sectionWidth(TopMenuSection section) {
    return textPixelWidth(topMenuSectionLabel(section)) + kSectionPad;
}

} // namespace

int topMenuBarHeight() {
    return kBarHeight;
}

int topMenuItemHeight() {
    return kItemHeight;
}

bool topMenuRectContains(const TopMenuRect& rect, int px, int py) {
    return px >= rect.x && px < rect.x + rect.w
        && py >= rect.y && py < rect.y + rect.h;
}

const char* topMenuSectionLabel(TopMenuSection section) {
    const std::size_t idx = sectionIndex(section);
    if (idx >= kSectionLabels.size()) {
        return "";
    }
    return kSectionLabels[idx];
}

const std::vector<TopMenuItem>& topMenuItems(TopMenuSection section) {
    switch (section) {
    case TopMenuSection::Session: return kSessionItems;
    case TopMenuSection::Image: return kImageItems;
    case TopMenuSection::Debug: return kDebugItems;
    case TopMenuSection::Controls: return kControlsItems;
    default: return kNetworkItems;
    }
}

TopMenuRect topMenuSectionRect(int outputW, TopMenuSection section) {
    (void)outputW;
    int x = kSectionStartX;
    for (int i = 0; i < static_cast<int>(TopMenuSection::Count); ++i) {
        const auto current = static_cast<TopMenuSection>(i);
        const int w = sectionWidth(current);
        if (current == section) {
            return TopMenuRect{x, kSectionY, w, kSectionHeight};
        }
        x += w + kSectionGap;
    }
    return TopMenuRect{};
}

TopMenuRect topMenuDropdownRect(int outputW, TopMenuSection section) {
    const TopMenuRect sec = topMenuSectionRect(outputW, section);
    const auto& items = topMenuItems(section);
    int maxLabel = 0;
    for (const auto& item : items) {
        maxLabel = std::max(maxLabel, textPixelWidth(item.label));
    }
    const int w = std::max(sec.w + 30, maxLabel + 16);
    const int h = static_cast<int>(items.size()) * kItemHeight + kDropdownPad * 2;
    return TopMenuRect{sec.x, kBarHeight, w, h};
}

std::optional<TopMenuSection> hitTestTopMenuSection(int outputW, int px, int py) {
    if (py < 0 || py >= kBarHeight) {
        return std::nullopt;
    }
    for (int i = 0; i < static_cast<int>(TopMenuSection::Count); ++i) {
        const auto section = static_cast<TopMenuSection>(i);
        const TopMenuRect rect = topMenuSectionRect(outputW, section);
        if (topMenuRectContains(rect, px, py)) {
            return section;
        }
    }
    return std::nullopt;
}

std::optional<TopMenuAction> hitTestTopMenuAction(int outputW, TopMenuSection section, int px, int py) {
    const TopMenuRect drop = topMenuDropdownRect(outputW, section);
    if (!topMenuRectContains(drop, px, py)) {
        return std::nullopt;
    }
    const int localY = py - drop.y - kDropdownPad;
    if (localY < 0) {
        return std::nullopt;
    }
    const int row = localY / kItemHeight;
    const auto& items = topMenuItems(section);
    if (row < 0 || row >= static_cast<int>(items.size())) {
        return std::nullopt;
    }
    return items[static_cast<std::size_t>(row)].action;
}

} // namespace gb::frontend
