#pragma once

#include <memory>
#include <vector>

#include "gb/core/cartridge.hpp"

namespace gb {
namespace cartridge_mapper {

std::unique_ptr<Mapper> makeNoMbcMapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeMbc1Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeMbc2Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeMbc3Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeMbc5Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeHuC1Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeHuC3Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makeMbc7Mapper(std::vector<u8>& rom, std::vector<u8>& ram);
std::unique_ptr<Mapper> makePocketCameraMapper(std::vector<u8>& rom, std::vector<u8>& ram);

} // namespace cartridge_mapper
} // namespace gb
