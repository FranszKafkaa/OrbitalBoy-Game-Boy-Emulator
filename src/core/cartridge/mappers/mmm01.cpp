#include "factory.hpp"
#include "common.hpp"

namespace gb {
namespace cartridge_mapper {
namespace {

class MMM01Mapper final : public Mapper {
public:
    MMM01Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {}

    u8 read(u16 address) const override {
        if (address < 0x4000) {
            u32 bank = baseBank_;
            if (mode_ == 1) {
                bank = (baseBank_ + (static_cast<u32>(bankHigh_) << 5)) % romBankCount_;
            }
            return readRomBank(rom_, bank, address);
        }
        if (address < 0x8000) {
            u32 bank = (baseBank_ + romBankLow_ + (static_cast<u32>(bankHigh_) << 5)) % romBankCount_;
            if (bank == baseBank_) {
                bank = (bank + 1) % romBankCount_;
            }
            return readRomBank(rom_, bank, address - 0x4000);
        }
        if (address >= 0xA000 && address <= 0xBFFF && ramEnabled_ && !ram_.empty()) {
            u32 bank = 0;
            if (mode_ == 1 && ramBankCount_ > 0) {
                bank = static_cast<u32>(bankHigh_) % ramBankCount_;
            }
            return readRamBank(ram_, bank, address - 0xA000);
        }
        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address <= 0x1FFF) {
            ramEnabled_ = (value & 0x0F) == 0x0A;
            if (ramEnabled_) {
                mappingLocked_ = true;
            }
            return;
        }
        if (address <= 0x3FFF) {
            if (!mappingLocked_) {
                baseBank_ = static_cast<u32>(value & 0x1F) % romBankCount_;
                return;
            }
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
            writeRamBank(ram_, bank, address - 0xA000, value);
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return std::vector<u8>{
            static_cast<u8>(baseBank_ & 0x1F),
            romBankLow_,
            bankHigh_,
            mode_,
            static_cast<u8>(ramEnabled_ ? 1 : 0),
            static_cast<u8>(mappingLocked_ ? 1 : 0),
        };
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 6) {
            return;
        }
        baseBank_ = static_cast<u32>(s[0] & 0x1F) % romBankCount_;
        romBankLow_ = s[1] == 0 ? 1 : static_cast<u8>(s[1] & 0x1F);
        bankHigh_ = static_cast<u8>(s[2] & 0x03);
        mode_ = static_cast<u8>(s[3] & 0x01);
        ramEnabled_ = s[4] != 0;
        mappingLocked_ = s[5] != 0;
    }

private:
    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;
    u32 ramBankCount_ = 0;

    u32 baseBank_ = 0;
    u8 romBankLow_ = 1;
    u8 bankHigh_ = 0;
    u8 mode_ = 0;
    bool ramEnabled_ = false;
    bool mappingLocked_ = false;
};

} // namespace

std::unique_ptr<Mapper> makeMmm01Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MMM01Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
