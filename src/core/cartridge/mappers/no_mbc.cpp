#include "factory.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class NoMBCMapper final : public Mapper {
public:
    NoMBCMapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram) {}

    u8 read(u16 address) const override {
        if (address < 0x8000) {
            if (address < rom_.size()) {
                return rom_[address];
            }
            return 0xFF;
        }

        if (address >= 0xA000 && address <= 0xBFFF && !ram_.empty()) {
            const u32 idx = address - 0xA000;
            if (idx < ram_.size()) {
                return ram_[idx];
            }
        }

        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address >= 0xA000 && address <= 0xBFFF && !ram_.empty()) {
            const u32 idx = address - 0xA000;
            if (idx < ram_.size()) {
                ram_[idx] = value;
            }
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
