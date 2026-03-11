#include "factory.hpp"
#include "common.hpp"

#include <chrono>

namespace gb {
namespace cartridge_mapper {
namespace {

class MBC3Mapper final : public Mapper {
public:
    MBC3Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {
        lastUpdate_ = std::chrono::steady_clock::now();
    }

    u8 read(u16 address) const override {
        const_cast<MBC3Mapper*>(this)->updateRtc();

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

        if (address < 0xA000 || address > 0xBFFF || !ramRtcEnabled_) {
            return 0xFF;
        }

        if (select_ <= 0x03) {
            if (ram_.empty()) {
                return 0xFF;
            }
            const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(select_ % ramBankCount_);
            const u32 idx = bank * 0x2000 + (address - 0xA000);
            if (idx < ram_.size()) {
                return ram_[idx];
            }
            return 0xFF;
        }

        if (select_ >= 0x08 && select_ <= 0x0C) {
            const auto& active = latched_ ? latchedRtc_ : rtc_;
            return readRtcReg(active, select_);
        }

        return 0xFF;
    }

    void write(u16 address, u8 value) override {
        updateRtc();

        if (address <= 0x1FFF) {
            ramRtcEnabled_ = (value & 0x0F) == 0x0A;
            return;
        }
        if (address <= 0x3FFF) {
            romBank_ = static_cast<u8>(value & 0x7F);
            if (romBank_ == 0) {
                romBank_ = 1;
            }
            return;
        }
        if (address <= 0x5FFF) {
            select_ = static_cast<u8>(value & 0x0F);
            return;
        }
        if (address <= 0x7FFF) {
            const u8 edge = static_cast<u8>(value & 0x01);
            if (latchArm_ == 0 && edge == 1) {
                latchedRtc_ = rtc_;
                latched_ = true;
            }
            latchArm_ = edge;
            return;
        }

        if (address < 0xA000 || address > 0xBFFF || !ramRtcEnabled_) {
            return;
        }

        if (select_ <= 0x03) {
            if (ram_.empty()) {
                return;
            }
            const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(select_ % ramBankCount_);
            const u32 idx = bank * 0x2000 + (address - 0xA000);
            if (idx < ram_.size()) {
                ram_[idx] = value;
            }
            return;
        }

        if (select_ >= 0x08 && select_ <= 0x0C) {
            writeRtcReg(select_, value);
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        return std::vector<u8>{
            romBank_,
            select_,
            static_cast<u8>(ramRtcEnabled_ ? 1 : 0),
            latchArm_,
            static_cast<u8>(latched_ ? 1 : 0),
            rtc_.seconds,
            rtc_.minutes,
            rtc_.hours,
            rtc_.dayLow,
            rtc_.dayHigh,
            latchedRtc_.seconds,
            latchedRtc_.minutes,
            latchedRtc_.hours,
            latchedRtc_.dayLow,
            latchedRtc_.dayHigh,
        };
    }

    void loadState(const std::vector<u8>& s) override {
        if (s.size() < 15) {
            return;
        }
        romBank_ = static_cast<u8>(s[0] & 0x7F);
        if (romBank_ == 0) {
            romBank_ = 1;
        }
        select_ = static_cast<u8>(s[1] & 0x0F);
        ramRtcEnabled_ = s[2] != 0;
        latchArm_ = static_cast<u8>(s[3] & 0x01);
        latched_ = s[4] != 0;
        rtc_.seconds = static_cast<u8>(s[5] % 60);
        rtc_.minutes = static_cast<u8>(s[6] % 60);
        rtc_.hours = static_cast<u8>(s[7] % 24);
        rtc_.dayLow = s[8];
        rtc_.dayHigh = static_cast<u8>(s[9] & 0xC1);
        latchedRtc_.seconds = static_cast<u8>(s[10] % 60);
        latchedRtc_.minutes = static_cast<u8>(s[11] % 60);
        latchedRtc_.hours = static_cast<u8>(s[12] % 24);
        latchedRtc_.dayLow = s[13];
        latchedRtc_.dayHigh = static_cast<u8>(s[14] & 0xC1);
        lastUpdate_ = std::chrono::steady_clock::now();
    }

private:
    struct RtcRegs {
        u8 seconds = 0;
        u8 minutes = 0;
        u8 hours = 0;
        u8 dayLow = 0;
        u8 dayHigh = 0;
    };

    static u8 readRtcReg(const RtcRegs& regs, u8 select) {
        switch (select) {
        case 0x08: return regs.seconds;
        case 0x09: return regs.minutes;
        case 0x0A: return regs.hours;
        case 0x0B: return regs.dayLow;
        case 0x0C: return regs.dayHigh;
        default: return 0xFF;
        }
    }

    void writeRtcReg(u8 select, u8 value) {
        switch (select) {
        case 0x08: rtc_.seconds = static_cast<u8>(value % 60); break;
        case 0x09: rtc_.minutes = static_cast<u8>(value % 60); break;
        case 0x0A: rtc_.hours = static_cast<u8>(value % 24); break;
        case 0x0B: rtc_.dayLow = value; break;
        case 0x0C: rtc_.dayHigh = static_cast<u8>(value & 0xC1); break;
        default: break;
        }
    }

    void updateRtc() {
        const auto now = std::chrono::steady_clock::now();
        const auto delta = std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate_).count();
        if (delta <= 0) {
            return;
        }
        lastUpdate_ = now;

        const bool halted = (rtc_.dayHigh & 0x40) != 0;
        if (halted) {
            return;
        }

        u32 days = static_cast<u32>(rtc_.dayLow | ((rtc_.dayHigh & 0x01) << 8));
        u32 totalSeconds = static_cast<u32>(rtc_.seconds)
            + static_cast<u32>(rtc_.minutes) * 60
            + static_cast<u32>(rtc_.hours) * 3600
            + days * 86400
            + static_cast<u32>(delta);

        const u32 nextDays = totalSeconds / 86400;
        totalSeconds %= 86400;

        rtc_.hours = static_cast<u8>(totalSeconds / 3600);
        totalSeconds %= 3600;
        rtc_.minutes = static_cast<u8>(totalSeconds / 60);
        rtc_.seconds = static_cast<u8>(totalSeconds % 60);

        const bool carry = nextDays > 511;
        days = nextDays % 512;
        const bool haltBit = (rtc_.dayHigh & 0x40) != 0;
        const bool prevCarry = (rtc_.dayHigh & 0x80) != 0;
        rtc_.dayLow = static_cast<u8>(days & 0xFF);
        rtc_.dayHigh = static_cast<u8>(
            ((days >> 8) & 0x01)
            | (haltBit ? 0x40 : 0x00)
            | ((carry || prevCarry) ? 0x80 : 0x00)
        );
    }

    std::vector<u8>& rom_;
    std::vector<u8>& ram_;
    u32 romBankCount_ = 1;
    u32 ramBankCount_ = 0;

    u8 romBank_ = 1;
    u8 select_ = 0;
    bool ramRtcEnabled_ = false;
    u8 latchArm_ = 0;
    bool latched_ = false;

    RtcRegs rtc_{};
    RtcRegs latchedRtc_{};
    std::chrono::steady_clock::time_point lastUpdate_{};
};

} // namespace

std::unique_ptr<Mapper> makeMbc3Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MBC3Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
