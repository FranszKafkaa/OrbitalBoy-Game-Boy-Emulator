#include "factory.hpp"
#include "common.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class MBC5Mapper final : public Mapper {
public:
    MBC5Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {}

    u8 read(u16 address) const override {
        if (address < 0x4000) {
            if (address < rom_.size()) {
                return rom_[address];
            }
            return 0xFF;
        }

        if (address < 0x8000) {
            u32 bank = static_cast<u32>(romBankLow_) | (static_cast<u32>(romBankHigh_) << 8);
            bank %= romBankCount_;
            const u32 idx = bank * 0x4000 + (address - 0x4000);
            if (idx < rom_.size()) {
                return rom_[idx];
            }
            return 0xFF;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(ramBank_ % ramBankCount_);
            const u32 idx = bank * 0x2000 + (address - 0xA000);
            if (idx < ram_.size()) {
                return ram_[idx];
            }
        }

        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address <= 0x1FFF) {
            ramEnabled_ = (value & 0x0F) == 0x0A;
            return;
        }
        if (address <= 0x2FFF) {
            romBankLow_ = value;
            return;
        }
        if (address <= 0x3FFF) {
            romBankHigh_ = static_cast<u8>(value & 0x01);
            return;
        }
        if (address <= 0x5FFF) {
            ramBank_ = static_cast<u8>(value & 0x0F);
            return;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(ramBank_ % ramBankCount_);
            const u32 idx = bank * 0x2000 + (address - 0xA000);
            if (idx < ram_.size()) {
                ram_[idx] = value;
            }
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return std::vector<u8>{romBankLow_, romBankHigh_, ramBank_, static_cast<u8>(ramEnabled_ ? 1 : 0)};
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 4) {
            return;
        }
        romBankLow_ = s[0];
        romBankHigh_ = static_cast<u8>(s[1] & 0x01);
        ramBank_ = static_cast<u8>(s[2] & 0x0F);
        ramEnabled_ = s[3] != 0;
    }

private:
    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;
    u32 ramBankCount_ = 0;

    u8 romBankLow_ = 1;
    u8 romBankHigh_ = 0;
    u8 ramBank_ = 0;
    bool ramEnabled_ = false;
};

} // namespace

std::unique_ptr<Mapper> makeMbc5Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MBC5Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
