#include "gb/app/frontend/realtime/retroachievements_progress.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
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
constexpr std::uint8_t kBoundVersion = 2;
constexpr std::size_t kHashSize = 32;
constexpr std::size_t kStateFingerprintSize = 64;
constexpr std::size_t kHeaderSize = 4 + 1 + kHashSize + 4;
constexpr std::size_t kBoundHeaderSize =
    4 + 1 + kHashSize + kStateFingerprintSize + 4;
constexpr std::size_t kMaximumPayloadSize = 1024U * 1024U;
constexpr std::size_t kMaximumStateFileSize = 64U * 1024U * 1024U;

bool isValidRomHash(std::string_view romHash) {
    return romHash.size() == kHashSize
        && std::all_of(romHash.begin(), romHash.end(), [](char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
        });
}

bool isLowerHex(std::string_view value, std::size_t expectedSize) {
    return value.size() == expectedSize
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t rotateRight(std::uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

std::string sha256Hex(const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> padded = input;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(input.size()) * 8U;
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U) {
        padded.push_back(0U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<std::uint8_t>(bitLength >> shift));
    }

    std::array<std::uint32_t, 8> hash{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16U; ++index) {
            const std::size_t byte = offset + index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(padded[byte]) << 24U)
                | (static_cast<std::uint32_t>(padded[byte + 1U]) << 16U)
                | (static_cast<std::uint32_t>(padded[byte + 2U]) << 8U)
                | static_cast<std::uint32_t>(padded[byte + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index) {
            const std::uint32_t s0 =
                rotateRight(words[index - 15U], 7U)
                ^ rotateRight(words[index - 15U], 18U)
                ^ (words[index - 15U] >> 3U);
            const std::uint32_t s1 =
                rotateRight(words[index - 2U], 17U)
                ^ rotateRight(words[index - 2U], 19U)
                ^ (words[index - 2U] >> 10U);
            words[index] = words[index - 16U] + s0
                + words[index - 7U] + s1;
        }

        std::uint32_t a = hash[0];
        std::uint32_t b = hash[1];
        std::uint32_t c = hash[2];
        std::uint32_t d = hash[3];
        std::uint32_t e = hash[4];
        std::uint32_t f = hash[5];
        std::uint32_t g = hash[6];
        std::uint32_t h = hash[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t sum1 =
                rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = h + sum1 + choice
                + kSha256RoundConstants[index] + words[index];
            const std::uint32_t sum0 =
                rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        hash[0] += a;
        hash[1] += b;
        hash[2] += c;
        hash[3] += d;
        hash[4] += e;
        hash[5] += f;
        hash[6] += g;
        hash[7] += h;
    }

    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(kStateFingerprintSize);
    for (const std::uint32_t word : hash) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back(hex[(word >> shift) & 0x0FU]);
        }
    }
    return result;
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

std::uint32_t readBoundPayloadSize(
    const std::array<std::uint8_t, kBoundHeaderSize>& header
) {
    constexpr std::size_t offset = 5U + kHashSize + kStateFingerprintSize;
    return static_cast<std::uint32_t>(header[offset])
        | (static_cast<std::uint32_t>(header[offset + 1U]) << 8U)
        | (static_cast<std::uint32_t>(header[offset + 2U]) << 16U)
        | (static_cast<std::uint32_t>(header[offset + 3U]) << 24U);
}

} // namespace

std::string retroAchievementsProgressPathForState(const std::string& statePath) {
    return statePath + ".ra-progress";
}

std::optional<RaStateFileImage> readRetroAchievementsStateFile(
    const std::string& path
) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return std::nullopt;
    }
    const std::streampos end = in.tellg();
    if (end < 0
        || static_cast<std::uint64_t>(end) > kMaximumStateFileSize) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        in.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
            return std::nullopt;
        }
    }
    return RaStateFileImage{bytes, sha256Hex(bytes)};
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

    const std::streamoff expectedSize =
        static_cast<std::streamoff>(kHeaderSize) + static_cast<std::streamoff>(payloadSize);
    in.seekg(0, std::ios::end);
    const std::streampos fileEnd = in.tellg();
    if (!in || fileEnd != std::streampos(expectedSize)) {
        return std::nullopt;
    }
    in.seekg(static_cast<std::streamoff>(kHeaderSize), std::ios::beg);
    if (!in) {
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

    return RaStoredProgress{std::string(storedRomHash), {}, std::move(payload)};
}

bool saveRetroAchievementsProgressV2(
    const std::string& path,
    std::string_view romHash,
    std::string_view stateFingerprint,
    const std::vector<std::uint8_t>& payload
) {
    if (path.empty() || !isValidRomHash(romHash)
        || !isLowerHex(stateFingerprint, kStateFingerprintSize)
        || payload.size() > kMaximumPayloadSize) {
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

    std::array<std::uint8_t, kBoundHeaderSize> header{};
    std::copy(kMagic.begin(), kMagic.end(), header.begin());
    header[4] = kBoundVersion;
    std::copy(romHash.begin(), romHash.end(), header.begin() + 5U);
    std::copy(
        stateFingerprint.begin(),
        stateFingerprint.end(),
        header.begin() + 5U + kHashSize
    );
    const std::size_t sizeOffset = 5U + kHashSize + kStateFingerprintSize;
    const std::uint32_t payloadSize = static_cast<std::uint32_t>(payload.size());
    for (unsigned byte = 0; byte < 4U; ++byte) {
        header[sizeOffset + byte] =
            static_cast<std::uint8_t>(payloadSize >> (byte * 8U));
    }

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

std::optional<RaStoredProgress> loadRetroAchievementsProgressV2(
    const std::string& path,
    std::string_view expectedRomHash,
    std::string_view expectedStateFingerprint
) {
    if (path.empty()
        || (!expectedRomHash.empty() && !isValidRomHash(expectedRomHash))
        || !isLowerHex(expectedStateFingerprint, kStateFingerprintSize)) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::array<std::uint8_t, kBoundHeaderSize> header{};
    in.read(
        reinterpret_cast<char*>(header.data()),
        static_cast<std::streamsize>(header.size())
    );
    if (in.gcount() != static_cast<std::streamsize>(header.size())
        || !std::equal(kMagic.begin(), kMagic.end(), header.begin())
        || header[4] != kBoundVersion) {
        return std::nullopt;
    }
    const std::string_view storedRomHash(
        reinterpret_cast<const char*>(header.data() + 5U),
        kHashSize
    );
    const std::string_view storedFingerprint(
        reinterpret_cast<const char*>(header.data() + 5U + kHashSize),
        kStateFingerprintSize
    );
    if (!isValidRomHash(storedRomHash)
        || (!expectedRomHash.empty() && storedRomHash != expectedRomHash)
        || storedFingerprint != expectedStateFingerprint) {
        return std::nullopt;
    }
    const std::uint32_t payloadSize = readBoundPayloadSize(header);
    if (payloadSize > kMaximumPayloadSize) {
        return std::nullopt;
    }
    const std::streamoff expectedSize =
        static_cast<std::streamoff>(kBoundHeaderSize)
        + static_cast<std::streamoff>(payloadSize);
    in.seekg(0, std::ios::end);
    if (!in || in.tellg() != std::streampos(expectedSize)) {
        return std::nullopt;
    }
    in.seekg(static_cast<std::streamoff>(kBoundHeaderSize), std::ios::beg);
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
    return RaStoredProgress{
        std::string(storedRomHash),
        std::string(storedFingerprint),
        std::move(payload),
    };
}

bool invalidateRetroAchievementsProgress(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) {
        return false;
    }
    if (!exists) {
        return true;
    }
    return std::filesystem::remove(path, error) && !error;
}

} // namespace gb::frontend
