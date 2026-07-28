#pragma once

#include "gb/app/frontend/realtime/cheat_engine.hpp"
#include "gb/app/frontend/realtime/control_bindings.hpp"
#include "gb/app/frontend/realtime_options.hpp"

#ifdef GBEMU_USE_SDL2
#include "gb/app/frontend/realtime_support.hpp"

namespace gb::frontend {

class SessionPersistence {
public:
    explicit SessionPersistence(SessionPaths paths);

    [[nodiscard]] std::optional<DisplayPaletteMode> loadPalette() const;
    [[nodiscard]] std::optional<VideoFilterMode> loadFilter() const;
    [[nodiscard]] CheatFileResult loadCheats() const;
    bool loadControls(ControlBindings& controls) const;

    void savePalette(DisplayPaletteMode mode) const;
    void saveFilter(VideoFilterMode mode) const;
    bool saveControls(const ControlBindings& controls) const;

private:
    SessionPaths paths_;
    std::string globalControlsPath_;
};

} // namespace gb::frontend
#endif
