#include "factory.hpp"
#include "common.hpp"

#include <chrono>
#include <cstdint>

namespace gb {
namespace cartridge_mapper {
namespace {

std::int64_t currentUnixSeconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

class MBC3Mapper final : public Mapper {
public:
    MBC3Mapper(std::vector<u8>& rom, std::vector<u8>& ram)
        : rom_(rom), ram_(ram), romBankCount_(safeRomBankCount(rom)), ramBankCount_(safeRamBankCount(ram)) {}

    u8 read(u16 address) const override {
        const_cast<MBC3Mapper*>(this)->updateRtc();

        if (address < 0x4000) {
            return readRomByte(rom_, address);
        }

        if (address < 0x8000) {
            const u32 bank = static_cast<u32>(romBank_ % romBankCount_);
            return readRomBank(rom_, bank, address - 0x4000);
        }

        if (address < 0xA000 || address > 0xBFFF || !ramRtcEnabled_) {
            return 0xFF;
        }

        if (select_ <= 0x03) {
            if (ram_.empty()) {
                return 0xFF;
            }
            const u32 bank = ramBankCount_ == 0 ? 0 : static_cast<u32>(select_ % ramBankCount_);
            return readRamBank(ram_, bank, address - 0xA000);
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
            writeRamBank(ram_, bank, address - 0xA000, value);
            return;
        }

        if (select_ >= 0x08 && select_ <= 0x0C) {
            writeRtcReg(select_, value);
        }
    }

    [[nodiscard]] std::vector<u8> state() const override {
        std::vector<u8> out{
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
        const std::int64_t unixSeconds = lastUnixSeconds_ == 0 ? currentUnixSeconds() : lastUnixSeconds_;
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<u8>((static_cast<std::uint64_t>(unixSeconds) >> (i * 8)) & 0xFF));
        }
        return out;
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

        if (s.size() >= 23) {
            std::uint64_t packed = 0;
            for (int i = 0; i < 8; ++i) {
                packed |= static_cast<std::uint64_t>(s[15 + i]) << (i * 8);
            }
            lastUnixSeconds_ = static_cast<std::int64_t>(packed);
        } else {
            lastUnixSeconds_ = currentUnixSeconds();
        }

        updateRtc();
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
        const std::int64_t now = currentUnixSeconds();
        if (lastUnixSeconds_ == 0) {
            lastUnixSeconds_ = now;
            return;
        }
        if (now < lastUnixSeconds_) {
            lastUnixSeconds_ = now;
            return;
        }

        const std::int64_t delta = now - lastUnixSeconds_;
        if (delta <= 0) {
            return;
        }
        lastUnixSeconds_ = now;

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
    std::int64_t lastUnixSeconds_ = currentUnixSeconds();
};

} // namespace

std::unique_ptr<Mapper> makeMbc3Mapper(std::vector<u8>& rom, std::vector<u8>& ram) {
    return std::make_unique<MBC3Mapper>(rom, ram);
}

} // namespace cartridge_mapper
} // namespace gb
