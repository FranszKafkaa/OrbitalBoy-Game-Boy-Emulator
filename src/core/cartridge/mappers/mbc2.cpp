#include "factory.hpp"
#include "common.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class MBC2Mapper final : public Mapper {
public:
    MBC2Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)) {}

    u8 read(u16 address) const override {
        if (address < 0x4000) {
            if (address < rom_.size()) {
                return rom_[address];
            }
            return 0xFF;
        }

        if (address < 0x8000) {
            const u32 bank = static_cast<u32>(romBank_ % romBankCount_);
            const u32 idx = bank * 0x4000 + (address - 0x4000);
            if (idx < rom_.size()) {
                return rom_[idx];
            }
            return 0xFF;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            const u32 idx = static_cast<u32>(address - 0xA000) & 0x01FF;
            return static_cast<u8>(0xF0 | (ram_[idx] & 0x0F));
        }

        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address <= 0x3FFF) {
            if ((address & 0x0100) == 0) {
                ramEnabled_ = (value & 0x0F) == 0x0A;
                return;
            }
            romBank_ = static_cast<u8>(value & 0x0F);
            if (romBank_ == 0) {
                romBank_ = 1;
            }
            return;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            const u32 idx = static_cast<u32>(address - 0xA000) & 0x01FF;
            ram_[idx] = static_cast<u8>(value & 0x0F);
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return std::vector<u8>{romBank_, static_cast<u8>(ramEnabled_ ? 1 : 0)};
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 2) {
            return;
        }
        romBank_ = static_cast<u8>(s[0] & 0x0F);
        if (romBank_ == 0) {
            romBank_ = 1;
        }
        ramEnabled_ = s[1] != 0;
    }

private:
    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;

    u8 romBank_ = 1;
    bool ramEnabled_ = false;
};

} // namespace

std::unique_ptr<Mapper> makeMbc2Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MBC2Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
