#include "gb/app/frontend/realtime/retroachievements_progress.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace gb::frontend {

namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'B', 'R', 'A'};
constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHashSize = 32;
constexpr std::size_t kHeaderSize = 4 + 1 + kHashSize + 4;
constexpr std::size_t kMaximumPayloadSize = 1024U * 1024U;

bool isValidRomHash(std::string_view romHash) {
    return romHash.size() == kHashSize
        && std::all_of(romHash.begin(), romHash.end(), [](char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        });
}

void removeTemporaryFile(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

bool replaceWithTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#if defined(_WIN32)
    return MoveFileExW(
        temporary.c_str(),
        destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
#else
    return ::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

std::array<std::uint8_t, kHeaderSize> makeHeader(
    std::string_view romHash,
    std::uint32_t payloadSize
) {
    std::array<std::uint8_t, kHeaderSize> header{};
    std::copy(kMagic.begin(), kMagic.end(), header.begin());
    header[4] = kVersion;
    std::copy(romHash.begin(), romHash.end(), header.begin() + 5);
    header[37] = static_cast<std::uint8_t>(payloadSize & 0xFFU);
    header[38] = static_cast<std::uint8_t>((payloadSize >> 8U) & 0xFFU);
    header[39] = static_cast<std::uint8_t>((payloadSize >> 16U) & 0xFFU);
    header[40] = static_cast<std::uint8_t>((payloadSize >> 24U) & 0xFFU);
    return header;
}

std::uint32_t readPayloadSize(const std::array<std::uint8_t, kHeaderSize>& header) {
    return static_cast<std::uint32_t>(header[37])
        | (static_cast<std::uint32_t>(header[38]) << 8U)
        | (static_cast<std::uint32_t>(header[39]) << 16U)
        | (static_cast<std::uint32_t>(header[40]) << 24U);
}

} // namespace

std::string retroAchievementsProgressPathForState(const std::string& statePath) {
    return statePath + ".ra-progress";
}

bool saveRetroAchievementsProgress(
    const std::string& path,
    std::string_view romHash,
    const std::vector<std::uint8_t>& payload
) {
    if (path.empty() || !isValidRomHash(romHash) || payload.size() > kMaximumPayloadSize) {
        return false;
    }

    const std::filesystem::path destination(path);
    const std::filesystem::path temporary(path + ".tmp");
    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    const auto header = makeHeader(romHash, static_cast<std::uint32_t>(payload.size()));
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            removeTemporaryFile(temporary);
            return false;
        }

        out.write(
            reinterpret_cast<const char*>(header.data()),
            static_cast<std::streamsize>(header.size())
        );
        if (!payload.empty()) {
            out.write(
                reinterpret_cast<const char*>(payload.data()),
                static_cast<std::streamsize>(payload.size())
            );
        }
        out.flush();
        if (!out) {
            out.close();
            removeTemporaryFile(temporary);
            return false;
        }
        out.close();
        if (out.fail()) {
            removeTemporaryFile(temporary);
            return false;
        }
    }

    if (!replaceWithTemporaryFile(temporary, destination)) {
        removeTemporaryFile(temporary);
        return false;
    }
    return true;
}

std::optional<RaStoredProgress> loadRetroAchievementsProgress(
    const std::string& path,
    std::string_view expectedRomHash
) {
    if (path.empty() || !isValidRomHash(expectedRomHash)) {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    std::array<std::uint8_t, kHeaderSize> header{};
    in.read(
        reinterpret_cast<char*>(header.data()),
        static_cast<std::streamsize>(header.size())
    );
    if (in.gcount() != static_cast<std::streamsize>(header.size())
        || !std::equal(kMagic.begin(), kMagic.end(), header.begin())
        || header[4] != kVersion) {
        return std::nullopt;
    }

    const std::string_view storedRomHash(
        reinterpret_cast<const char*>(header.data() + 5),
        kHashSize
    );
    if (!isValidRomHash(storedRomHash) || storedRomHash != expectedRomHash) {
        return std::nullopt;
    }

    const std::uint32_t payloadSize = readPayloadSize(header);
    if (payloadSize > kMaximumPayloadSize) {
        return std::nullopt;
    }

    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    if (sizeError || fileSize != kHeaderSize + static_cast<std::uintmax_t>(payloadSize)) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> payload(payloadSize);
    if (!payload.empty()) {
        in.read(
            reinterpret_cast<char*>(payload.data()),
            static_cast<std::streamsize>(payload.size())
        );
        if (in.gcount() != static_cast<std::streamsize>(payload.size())) {
            return std::nullopt;
        }
    }

    return RaStoredProgress{std::string(storedRomHash), std::move(payload)};
}

} // namespace gb::frontend
