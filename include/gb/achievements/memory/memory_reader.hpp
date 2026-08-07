#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace gb::achievements::memory {

using MemoryReader = std::function<std::size_t(std::uint32_t address, std::uint8_t* destination, std::size_t count)>;

} // namespace gb::achievements::memory
