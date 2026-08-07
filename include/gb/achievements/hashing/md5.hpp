#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gb::achievements::hashing {

// MD5 is used only as a RetroAchievements-compatible ROM identifier, never as
// a password or security primitive.
struct Md5Digest {
    std::array<std::uint8_t, 16> bytes{};

    std::string hexLowercase() const;
};

class Md5 {
public:
    Md5();

    void update(const std::uint8_t* data, std::size_t size);
    void update(std::string_view text);

    Md5Digest digest() const;

private:
    void transform(const std::uint8_t block[64]);

    std::array<std::uint32_t, 4> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bytesProcessed_ = 0U;
    std::size_t bufferedBytes_ = 0U;
};

Md5Digest md5(const std::uint8_t* data, std::size_t size);
Md5Digest md5(const std::vector<std::uint8_t>& bytes);
Md5Digest md5(std::string_view text);

} // namespace gb::achievements::hashing
