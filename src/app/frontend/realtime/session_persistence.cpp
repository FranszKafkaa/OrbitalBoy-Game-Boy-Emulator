#include "gb/app/frontend/realtime/session_persistence.hpp"

#ifdef GBEMU_USE_SDL2
#include <filesystem>
#include <utility>

namespace gb::frontend {

SessionPersistence::SessionPersistence(SessionPaths paths)
    : paths_(std::move(paths)),
      globalControlsPath_((std::filesystem::path("states") / "global.controls").string()) {
    for (const std::string* path : {
             &paths_.palette,
             &paths_.filters,
             &paths_.controls,
             &paths_.cheats,
         }) {
        const auto parent = std::filesystem::path(*path).parent_path();
        if (!parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
        }
    }
}

std::optional<DisplayPaletteMode> SessionPersistence::loadPalette() const {
    return loadPalettePreference(paths_.palette);
}

std::optional<VideoFilterMode> SessionPersistence::loadFilter() const {
    return loadFilterPreference(paths_.filters);
}

CheatFileResult SessionPersistence::loadCheats() const {
    return loadCheatsFromFile(paths_.cheats);
}

bool SessionPersistence::loadControls(ControlBindings& controls) const {
    return loadControlBindingsWithFallback(paths_.controls, globalControlsPath_, controls);
}

void SessionPersistence::savePalette(DisplayPaletteMode mode) const {
    savePalettePreference(paths_.palette, mode);
}

void SessionPersistence::saveFilter(VideoFilterMode mode) const {
    saveFilterPreference(paths_.filters, mode);
}

bool SessionPersistence::saveControls(const ControlBindings& controls) const {
    return saveControlBindingsWithMirror(paths_.controls, globalControlsPath_, controls);
}

} // namespace gb::frontend
#endif
