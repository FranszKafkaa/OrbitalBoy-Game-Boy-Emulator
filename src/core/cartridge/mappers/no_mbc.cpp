#include "factory.hpp"
#include "common.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class NoMBCMapper final : public Mapper {
public:
    NoMBCMapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram) {}

    u8 read(u16 address) const override {
        if (address < 0x8000) {
            return readRomByte(rom_, address);
        }

        if (address >= 0xA000 && address <= 0xBFFF && !ram_.empty()) {
            return readRamBank(ram_, 0, address - 0xA000);
        }

        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address >= 0xA000 && address <= 0xBFFF && !ram_.empty()) {
            writeRamBank(ram_, 0, address - 0xA000, value);
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return {};
    }

    void loadState(const std::vector<u8>&) override {}

private:
    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
};

} // namespace

std::unique_ptr<Mapper> makeNoMbcMapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<NoMBCMapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
