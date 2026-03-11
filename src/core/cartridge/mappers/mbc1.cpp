#include "factory.hpp"
#include "common.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class MBC1Mapper final : public Mapper {
public:
    MBC1Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {}

    u8 read(u16 address) const override {
        if (address < 0x4000) {
            u32 bank = 0;
            if (mode_ == 1) {
                bank = static_cast<u32>(bankHigh_) << 5;
                bank %= romBankCount_;
            }
            const u32 idx = bank * 0x4000 + address;
            if (idx < rom_.size()) {
                return rom_[idx];
            }
            return 0xFF;
        }

        if (address < 0x8000) {
            u32 bank = romBankLow_;
            bank |= static_cast<u32>(bankHigh_) << 5;
            bank %= romBankCount_;
            if (bank == 0) {
                bank = 1;
            }
            const u32 idx = bank * 0x4000 + (address - 0x4000);
            if (idx < rom_.size()) {
                return rom_[idx];
            }
            return 0xFF;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            u32 bank = 0;
            if (mode_ == 1 && ramBankCount_ > 0) {
                bank = static_cast<u32>(bankHigh_) % ramBankCount_;
            }
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
        if (address <= 0x3FFF) {
            romBankLow_ = static_cast<u8>(value & 0x1F);
            if (romBankLow_ == 0) {
                romBankLow_ = 1;
            }
            return;
        }
        if (address <= 0x5FFF) {
            bankHigh_ = static_cast<u8>(value & 0x03);
            return;
        }
        if (address <= 0x7FFF) {
            mode_ = static_cast<u8>(value & 0x01);
            return;
        }

        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            u32 bank = 0;
            if (mode_ == 1 && ramBankCount_ > 0) {
                bank = static_cast<u32>(bankHigh_) % ramBankCount_;
            }
            const u32 idx = bank * 0x2000 + (address - 0xA000);
            if (idx < ram_.size()) {
                ram_[idx] = value;
            }
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return std::vector<u8>{romBankLow_, bankHigh_, mode_, static_cast<u8>(ramEnabled_ ? 1 : 0)};
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 4) {
            return;
        }
        romBankLow_ = s[0] == 0 ? 1 : s[0];
        bankHigh_ = static_cast<u8>(s[1] & 0x03);
        mode_ = static_cast<u8>(s[2] & 0x01);
        ramEnabled_ = s[3] != 0;
    }

private:
    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;
    u32 ramBankCount_ = 0;

    u8 romBankLow_ = 1;
    u8 bankHigh_ = 0;
    u8 mode_ = 0;
    bool ramEnabled_ = false;
};

} // namespace

std::unique_ptr<Mapper> makeMbc1Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MBC1Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
