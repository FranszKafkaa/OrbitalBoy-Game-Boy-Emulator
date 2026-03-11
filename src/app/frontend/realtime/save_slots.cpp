#include "gb/app/frontend/realtime/save_slots.hpp"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace gb::frontend {

namespace {

std::filesystem::path replaceWithSlotExt(const std::string& baseStatePath, int slot, const char* ext) {
    const int normalized = normalizeSaveSlot(slot);
    std::filesystem::path p(baseStatePath);
    std::ostringstream name;
    name << p.stem().string() << ".slot" << normalized << ext;
    p.replace_filename(name.str());
    return p;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace

int normalizeSaveSlot(int slot, int maxSlots) {
    if (maxSlots <= 0) {
        return 0;
    }
    if (slot < 0) {
        slot = 0;
    }
    if (slot >= maxSlots) {
        slot = maxSlots - 1;
    }
    return slot;
}

std::string saveSlotStatePath(const std::string& baseStatePath, int slot) {
    return replaceWithSlotExt(baseStatePath, slot, ".state").string();
}

std::string saveSlotMetaPath(const std::string& baseStatePath, int slot) {
    return replaceWithSlotExt(baseStatePath, slot, ".meta").string();
}

std::string saveSlotThumbnailPath(const std::string& baseStatePath, int slot) {
    return replaceWithSlotExt(baseStatePath, slot, ".ppm").string();
}

bool writeSaveSlotMeta(const std::string& metaPath, const SaveSlotMeta& meta) {
    std::error_code ec;
    const std::filesystem::path p(metaPath);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(metaPath, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "slot=" << meta.slot << "\n";
    out << "title=" << meta.title << "\n";
    out << "timestamp=" << meta.timestamp << "\n";
    out << "frame=" << meta.frame << "\n";
    return static_cast<bool>(out);
}

std::optional<SaveSlotMeta> readSaveSlotMeta(const std::string& metaPath) {
    std::ifstream in(metaPath);
    if (!in) {
        return std::nullopt;
    }

    SaveSlotMeta out{};
    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, pos));
        const std::string value = trim(line.substr(pos + 1));
        if (key == "slot") {
            out.slot = std::stoi(value);
        } else if (key == "title") {
            out.title = value;
        } else if (key == "timestamp") {
            out.timestamp = value;
        } else if (key == "frame") {
            out.frame = static_cast<std::uint64_t>(std::stoull(value));
        }
    }
    return out;
}

std::string nowIso8601Local() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

} // namespace gb::frontend
