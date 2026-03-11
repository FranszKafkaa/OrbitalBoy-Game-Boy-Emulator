#include "factory.hpp"
#include "common.hpp"

#include <algorithm>
#include <array>

namespace gb {
namespace cartridge_mapper {
namespace {

class PocketCameraMapper final : public Mapper {
public:
    PocketCameraMapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {
        if (ram_.empty()) {
            ram_.assign(0x2000, 0x00);
            ramBankCount_ = safeRamBankCount(ram_);
        }
    }

    u8 read(u16 address) const override {
        if (address < 0x4000) {
            return address < rom_.size() ? rom_[address] : 0xFF;
        }
        if (address < 0x8000) {
            const u32 bank = static_cast<u32>(romBank_ % romBankCount_);
            const u32 idx = bank * 0x4000 + (address - 0x4000);
            return idx < rom_.size() ? rom_[idx] : 0xFF;
        }
        if (address < 0xA000 || address > 0xBFFF || !ramEnabled_) {
            return 0xFF;
        }

        if (cameraRegMode_ && address <= 0xA07F) {
            const std::size_t idx = static_cast<std::size_t>(address - 0xA000) & 0x7F;
            return cameraRegs_[idx];
        }

        const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(ramBank_ % ramBankCount_);
        const u32 idx = bank * 0x2000 + (address - 0xA000);
        return idx < ram_.size() ? ram_[idx] : 0xFF;
    }

    void write(u16 address, u8 value) override {
        if (address <= 0x1FFF) {
            ramEnabled_ = (value & 0x0F) == 0x0A;
            return;
        }
        if (address <= 0x2FFF) {
            romBankLow_ = value;
            rebuildRomBank();
            return;
        }
        if (address <= 0x3FFF) {
            romBankHigh_ = static_cast<u8>(value & 0x01);
            rebuildRomBank();
            return;
        }
        if (address <= 0x5FFF) {
            ramBank_ = static_cast<u8>(value & 0x0F);
            cameraRegMode_ = (value & 0x10) != 0;
            return;
        }
        if (address < 0xA000 || address > 0xBFFF || !ramEnabled_) {
            return;
        }

        if (cameraRegMode_ && address <= 0xA07F) {
            const std::size_t idx = static_cast<std::size_t>(address - 0xA000) & 0x7F;
            cameraRegs_[idx] = value;
            return;
        }

        const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(ramBank_ % ramBankCount_);
        const u32 idx = bank * 0x2000 + (address - 0xA000);
        if (idx < ram_.size()) {
            ram_[idx] = value;
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        std::vector<u8> out;
        out.reserve(6 + cameraRegs_.size());
        out.push_back(romBankLow_);
        out.push_back(romBankHigh_);
        out.push_back(ramBank_);
        out.push_back(static_cast<u8>(ramEnabled_ ? 1 : 0));
        out.push_back(static_cast<u8>(cameraRegMode_ ? 1 : 0));
        out.push_back(static_cast<u8>(romBank_ & 0xFF));
        out.insert(out.end(), cameraRegs_.begin(), cameraRegs_.end());
        return out;
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 6) {
            return;
        }
        romBankLow_ = s[0];
        romBankHigh_ = static_cast<u8>(s[1] & 0x01);
        ramBank_ = static_cast<u8>(s[2] & 0x0F);
        ramEnabled_ = s[3] != 0;
        cameraRegMode_ = s[4] != 0;
        rebuildRomBank();

        const std::size_t maxCopy = std::min<std::size_t>(cameraRegs_.size(), s.size() - 6);
        for (std::size_t i = 0; i < maxCopy; ++i) {
            cameraRegs_[i] = s[6 + i];
        }
    }

private:
    void rebuildRomBank() {
        romBank_ = static_cast<u16>(romBankLow_ | ((romBankHigh_ & 0x01) << 8));
        if (romBank_ == 0) {
            romBank_ = 1;
        }
    }

    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;
    u32 ramBankCount_ = 0;

    u8 romBankLow_ = 1;
    u8 romBankHigh_ = 0;
    u8 ramBank_ = 0;
    bool ramEnabled_ = false;
    bool cameraRegMode_ = false;
    u16 romBank_ = 1;
    std::array<u8, 0x80> cameraRegs_{};
};

} // namespace

std::unique_ptr<Mapper> makePocketCameraMapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<PocketCameraMapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
