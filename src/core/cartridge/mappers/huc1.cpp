#include "factory.hpp"

namespace gb {
namespace cartridge_mapper {

std::unique_ptr<Mapper> makeHuC1Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    // HuC1 compartilha comportamento base de bancamento com MBC1.
    return makeMbc1Mapper(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
