#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gb::frontend {

struct SaveSlotMeta {
    int slot = 0;
    std::string title{};
    std::string timestamp{};
    std::uint64_t frame = 0;
};

int normalizeSaveSlot(int slot, int maxSlots = 5);
std::string saveSlotStatePath(const std::string& baseStatePath, int slot);
std::string saveSlotMetaPath(const std::string& baseStatePath, int slot);
std::string saveSlotThumbnailPath(const std::string& baseStatePath, int slot);

bool writeSaveSlotMeta(const std::string& metaPath, const SaveSlotMeta& meta);
std::optional<SaveSlotMeta> readSaveSlotMeta(const std::string& metaPath);
std::string nowIso8601Local();

} // namespace gb::frontend
