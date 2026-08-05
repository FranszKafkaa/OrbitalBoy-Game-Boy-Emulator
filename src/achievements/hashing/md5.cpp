#include "gb/achievements/hashing/md5.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace gb::achievements::hashing {
namespace {

constexpr std::array<std::uint32_t, 64> kConstants{{
    0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
    0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
    0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
    0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
    0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
    0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
    0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
    0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
    0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
    0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
    0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
    0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
    0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
    0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
    0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
    0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U,
}};

constexpr std::array<std::uint32_t, 64> kRotations{{
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U, 5U, 9U, 14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
}};

std::uint32_t rotateLeft(std::uint32_t value, std::uint32_t amount) {
    return (value << amount) | (value >> (32U - amount));
}

std::uint32_t readLittleEndian32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8U)
        | (static_cast<std::uint32_t>(bytes[2]) << 16U)
        | (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

void writeLittleEndian32(std::uint8_t* bytes, std::uint32_t value) {
    bytes[0] = static_cast<std::uint8_t>(value);
    bytes[1] = static_cast<std::uint8_t>(value >> 8U);
    bytes[2] = static_cast<std::uint8_t>(value >> 16U);
    bytes[3] = static_cast<std::uint8_t>(value >> 24U);
}

} // namespace

Md5::Md5()
    : state_{{0x67452301U, 0xefcdab89U, 0x98badcfeU, 0x10325476U}} {}

void Md5::update(const std::uint8_t* data, std::size_t size) {
    if (size == 0U || data == nullptr) {
        return;
    }

    bytesProcessed_ += static_cast<std::uint64_t>(size);

    if (bufferedBytes_ != 0U) {
        const auto needed = buffer_.size() - bufferedBytes_;
        const auto copied = std::min(needed, size);
        std::memcpy(buffer_.data() + bufferedBytes_, data, copied);
        bufferedBytes_ += copied;
        data += copied;
        size -= copied;

        if (bufferedBytes_ == buffer_.size()) {
            transform(buffer_.data());
            bufferedBytes_ = 0U;
        }
    }

    while (size >= buffer_.size()) {
        transform(data);
        data += buffer_.size();
        size -= buffer_.size();
    }

    if (size != 0U) {
        std::memcpy(buffer_.data(), data, size);
        bufferedBytes_ = size;
    }
}

void Md5::update(std::string_view text) {
    update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

Md5Digest Md5::digest() const {
    Md5 completed = *this;
    const auto messageBits = completed.bytesProcessed_ * 8U;
    std::array<std::uint8_t, 64> padding{};
    padding[0] = 0x80U;
    const auto paddingSize = completed.bufferedBytes_ < 56U
        ? 56U - completed.bufferedBytes_
        : 120U - completed.bufferedBytes_;
    completed.update(padding.data(), paddingSize);

    std::array<std::uint8_t, 8> length{};
    for (std::size_t index = 0U; index < length.size(); ++index) {
        length[index] = static_cast<std::uint8_t>(messageBits >> (index * 8U));
    }
    completed.update(length.data(), length.size());

    Md5Digest result;
    for (std::size_t index = 0U; index < completed.state_.size(); ++index) {
        writeLittleEndian32(result.bytes.data() + index * 4U, completed.state_[index]);
    }
    return result;
}

void Md5::transform(const std::uint8_t block[64]) {
    std::array<std::uint32_t, 16> words{};
    for (std::size_t index = 0U; index < words.size(); ++index) {
        words[index] = readLittleEndian32(block + index * 4U);
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];

    for (std::size_t index = 0U; index < 64U; ++index) {
        std::uint32_t function = 0U;
        std::size_t wordIndex = 0U;
        if (index < 16U) {
            function = (b & c) | (~b & d);
            wordIndex = index;
        } else if (index < 32U) {
            function = (d & b) | (~d & c);
            wordIndex = (5U * index + 1U) % 16U;
        } else if (index < 48U) {
            function = b ^ c ^ d;
            wordIndex = (3U * index + 5U) % 16U;
        } else {
            function = c ^ (b | ~d);
            wordIndex = (7U * index) % 16U;
        }

        const auto next = b + rotateLeft(
            a + function + kConstants[index] + words[wordIndex],
            kRotations[index]
        );
        a = d;
        d = c;
        c = b;
        b = next;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
}

std::string Md5Digest::hexLowercase() const {
    static constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
        result.push_back(kHexDigits[byte >> 4U]);
        result.push_back(kHexDigits[byte & 0x0fU]);
    }
    return result;
}

Md5Digest md5(const std::uint8_t* data, std::size_t size) {
    Md5 hasher;
    hasher.update(data, size);
    return hasher.digest();
}

Md5Digest md5(const std::vector<std::uint8_t>& bytes) {
    return md5(bytes.data(), bytes.size());
}

Md5Digest md5(std::string_view text) {
    Md5 hasher;
    hasher.update(text);
    return hasher.digest();
}

} // namespace gb::achievements::hashing
