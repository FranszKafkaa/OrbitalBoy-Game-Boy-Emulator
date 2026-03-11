#include "factory.hpp"

namespace gb {
namespace cartridge_mapper {

std::unique_ptr<Mapper> makeHuC3Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    // Implementacao inicial baseada em MBC3+RTC.
    return makeMbc3Mapper(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
