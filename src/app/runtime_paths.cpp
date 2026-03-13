#include "gb/app/runtime_paths.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <optional>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace gb {

namespace {

std::string romStemOrDefault(const std::string& romPath) {
    const std::filesystem::path rom(romPath);
    std::string stem = rom.stem().string();
    if (stem.empty()) {
        stem = "default";
    }
    return stem;
}

std::string buildStatesPath(const std::string& romPath, const std::string& extension) {
    std::error_code ec;
    std::filesystem::create_directories("states", ec);
    std::filesystem::path p = std::filesystem::path("states") / (romStemOrDefault(romPath) + extension);
    return p.string();
}

std::string buildStateSlotPath(const std::string& romPath, int slot, const std::string& extension) {
    if (slot < 0) {
        slot = 0;
    }
    if (slot > 9) {
        slot = 9;
    }
    std::error_code ec;
    std::filesystem::create_directories("states", ec);
    std::filesystem::path p = std::filesystem::path("states")
        / (romStemOrDefault(romPath) + ".slot" + std::to_string(slot) + extension);
    return p.string();
}

std::filesystem::path executableDirectory() {
#if defined(_WIN32)
    std::vector<wchar_t> buffer(static_cast<std::size_t>(MAX_PATH), L'\0');
    while (true) {
        const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (len == 0U) {
            return {};
        }
        if (len < buffer.size()) {
            buffer.resize(static_cast<std::size_t>(len));
            return std::filesystem::path(buffer).parent_path();
        }
        buffer.resize(buffer.size() * 2U, L'\0');
    }
#elif defined(__linux__)
    std::array<char, 4096> raw{};
    const ssize_t len = ::readlink("/proc/self/exe", raw.data(), raw.size() - 1U);
    if (len <= 0) {
        return {};
    }
    raw[static_cast<std::size_t>(len)] = '\0';
    return std::filesystem::path(raw.data()).parent_path();
#else
    return {};
#endif
}

std::vector<std::filesystem::path> runtimeRootCandidates() {
    std::vector<std::filesystem::path> roots;
    const auto pushUnique = [&roots](const std::filesystem::path& value) {
        if (value.empty()) {
            return;
        }
        if (std::find(roots.begin(), roots.end(), value) == roots.end()) {
            roots.push_back(value);
        }
    };
    std::error_code ec;

    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    pushUnique(cwd.lexically_normal());

    const std::filesystem::path exeDir = executableDirectory();
    if (!exeDir.empty()) {
        pushUnique(exeDir.lexically_normal());
        if (!exeDir.parent_path().empty()) {
            pushUnique(exeDir.parent_path().lexically_normal());
        }
        if (!exeDir.parent_path().parent_path().empty()) {
            pushUnique(exeDir.parent_path().parent_path().lexically_normal());
        }
    }
    return roots;
}

std::optional<std::filesystem::path> canonicalExistingFile(const std::filesystem::path& candidate) {
    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec) || !std::filesystem::is_regular_file(candidate, ec)) {
        return std::nullopt;
    }
    const std::filesystem::path weak = std::filesystem::weakly_canonical(candidate, ec);
    if (!weak.empty()) {
        return weak;
    }
    const std::filesystem::path abs = std::filesystem::absolute(candidate, ec);
    if (!abs.empty()) {
        return abs.lexically_normal();
    }
    return candidate.lexically_normal();
}

} // namespace

std::string statePathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".state");
}

std::string stateSlotPathForRom(const std::string& romPath, int slot) {
    return buildStateSlotPath(romPath, slot, ".state");
}

std::string stateSlotMetaPathForRom(const std::string& romPath, int slot) {
    return buildStateSlotPath(romPath, slot, ".meta");
}

std::string stateSlotThumbnailPathForRom(const std::string& romPath, int slot) {
    return buildStateSlotPath(romPath, slot, ".ppm");
}

std::string legacyStatePathForRom(const std::string& romPath) {
    std::filesystem::path p(romPath);
    p.replace_extension(".state");
    return p.string();
}

std::string batteryRamPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".sav");
}

std::string palettePathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".palette");
}

std::string rtcPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".rtc");
}

std::string controlsPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".controls");
}

std::string filtersPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".filters");
}

std::string cheatsPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".cheats");
}

std::string replayPathForRom(const std::string& romPath) {
    return buildStatesPath(romPath, ".replay");
}

std::string captureDirForRom(const std::string& romPath) {
    std::error_code ec;
    std::filesystem::create_directories("captures", ec);
    const std::filesystem::path p = std::filesystem::path("captures") / romStemOrDefault(romPath);
    std::filesystem::create_directories(p, ec);
    return p.string();
}

std::vector<std::string> romSearchDirectoriesForRuntime() {
    std::vector<std::string> out;
    const auto pushUnique = [&out](const std::filesystem::path& value) {
        if (value.empty()) {
            return;
        }
        const std::string text = value.lexically_normal().string();
        if (std::find(out.begin(), out.end(), text) == out.end()) {
            out.push_back(text);
        }
    };
    std::error_code ec;
    for (const auto& root : runtimeRootCandidates()) {
        const std::filesystem::path romDir = root / "rom";
        if (std::filesystem::exists(romDir, ec) && std::filesystem::is_directory(romDir, ec)) {
            pushUnique(romDir);
        }
        const std::filesystem::path romsDir = root / "roms";
        if (std::filesystem::exists(romsDir, ec) && std::filesystem::is_directory(romsDir, ec)) {
            pushUnique(romsDir);
        }
    }
    return out;
}

std::string resolveRomPathForRuntime(const std::string& romPath) {
    if (romPath.empty()) {
        return romPath;
    }

    const std::filesystem::path input(romPath);
    if (const auto exact = canonicalExistingFile(input)) {
        return exact->string();
    }

    for (const auto& root : runtimeRootCandidates()) {
        if (const auto rooted = canonicalExistingFile(root / input)) {
            return rooted->string();
        }
    }

    for (const auto& dirText : romSearchDirectoriesForRuntime()) {
        const std::filesystem::path dir(dirText);
        if (const auto fromDir = canonicalExistingFile(dir / input)) {
            return fromDir->string();
        }
        if (const auto byFileName = canonicalExistingFile(dir / input.filename())) {
            return byFileName->string();
        }
    }

    return romPath;
}

} // namespace gb
