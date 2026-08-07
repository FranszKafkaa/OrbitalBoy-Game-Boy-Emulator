#include "gb/achievements/hashing/rom_hasher.hpp"

#include <array>
#include <fstream>
#include <limits>

#include "gb/achievements/hashing/md5.hpp"

namespace gb::achievements::hashing {
namespace {

bool isSupportedSystem(RomSystem system) {
    switch (system) {
    case RomSystem::GameBoy:
    case RomSystem::GameBoyColor:
    case RomSystem::GameBoyAdvance:
        return true;
    }
    return false;
}

} // namespace

RomHashResult hashRomBytes(
    RomSystem system,
    const std::vector<std::uint8_t>& romBytes
) {
    static_cast<void>(isSupportedSystem(system));
    const auto digest = md5(romBytes);
    return {digest.hexLowercase(), static_cast<std::uint64_t>(romBytes.size())};
}

std::optional<RomHashResult> hashRomFile(
    RomSystem system,
    const std::filesystem::path& path
) {
    if (!isSupportedSystem(system)) {
        return std::nullopt;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    const auto expectedSize = std::filesystem::file_size(path, error);
    if (error || expectedSize > std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }

    Md5 hasher;
    std::uint64_t bytesHashed = 0U;
    std::array<std::uint8_t, 64U * 1024U> chunk{};
    while (true) {
        input.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
        const auto count = input.gcount();
        if (count > 0) {
            const auto bytesRead = static_cast<std::uint64_t>(count);
            if (bytesHashed > std::numeric_limits<std::uint64_t>::max() - bytesRead) {
                return std::nullopt;
            }
            hasher.update(chunk.data(), static_cast<std::size_t>(count));
            bytesHashed += bytesRead;
        }

        if (input.bad()) {
            return std::nullopt;
        }
        if (input.eof()) {
            break;
        }
        if (input.fail()) {
            return std::nullopt;
        }
    }

    const auto finalSize = std::filesystem::file_size(path, error);
    if (error || finalSize != expectedSize || bytesHashed != expectedSize) {
        return std::nullopt;
    }
    return RomHashResult{hasher.digest().hexLowercase(), bytesHashed};
}

} // namespace gb::achievements::hashing
