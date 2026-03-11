#include "gb/core/cartridge.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace gb {

namespace {

constexpr u32 RtcMagic = 0x43545247; // "GRTC"
constexpr u32 RtcVersion = 1;

template <typename T>
bool writePod(std::ostream& os, const T& value) {
    os.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(os);
}

template <typename T>
bool readPod(std::istream& is, T& value) {
    is.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(is);
}

u32 ramSizeFromCode(u8 code) {
    switch (code) {
    case 0x00: return 0;
    case 0x01: return 2 * 1024;
    case 0x02: return 8 * 1024;
    case 0x03: return 32 * 1024;
    case 0x04: return 128 * 1024;
    case 0x05: return 64 * 1024;
    default: return 0;
    }
}

u32 safeRomBankCount(const std::vector<u8>& rom) {
    const u32 banks = static_cast<u32>(rom.size() / 0x4000);
    return banks == 0 ? 1 : banks;
}

u32 safeRamBankCount(const std::vector<u8>& ram) {
    if (ram.empty()) {
        return 0;
    }
    const u32 banks = static_cast<u32>(ram.size() / 0x2000);
    return banks == 0 ? 1 : banks;
}

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

bool Cartridge::loadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    rom_.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    if (rom_.size() < 0x150) {
        rom_.clear();
        return false;
    }
    loadedPath_ = path;

    ram_.assign(ramSizeFromCode(rom_[0x149]), 0x00);

    const auto type = cartridgeType();
    if ((type == 0x05 || type == 0x06) && ram_.empty()) {
        // MBC2 possui 512 x 4-bit de RAM interna.
        ram_.assign(0x200, 0x00);
    }
    if (type == 0x22 && ram_.empty()) {
        // MBC7 usa EEPROM pequena; buffer simples para fallback.
        ram_.assign(0x200, 0x00);
    }
    if (type == 0xFC && ram_.empty()) {
        // Pocket Camera costuma expor RAM salva por bateria.
        ram_.assign(0x2000, 0x00);
    }
    if (type == 0x00 || type == 0x08 || type == 0x09) {
        mapper_ = std::make_unique<NoMBCMapper>(rom_, ram_);
    } else if (type >= 0x01 && type <= 0x03) {
        mapper_ = std::make_unique<MBC1Mapper>(rom_, ram_);
    } else if (type >= 0x0B && type <= 0x0D) { // MMM01 family (fallback)
        mapper_ = std::make_unique<MBC1Mapper>(rom_, ram_);
    } else if (type == 0x05 || type == 0x06) {
        mapper_ = std::make_unique<MBC2Mapper>(rom_, ram_);
    } else if (type >= 0x0F && type <= 0x13) {
        mapper_ = std::make_unique<MBC3Mapper>(rom_, ram_);
    } else if (type == 0xFE) { // HuC3 (tratado como MBC3+RTC)
        mapper_ = std::make_unique<MBC3Mapper>(rom_, ram_);
    } else if (type == 0xFF) { // HuC1 (tratado como MBC1)
        mapper_ = std::make_unique<MBC1Mapper>(rom_, ram_);
    } else if (type == 0x22 || type == 0xFC) { // MBC7 / Pocket Camera fallback
        mapper_ = std::make_unique<MBC5Mapper>(rom_, ram_);
    } else if (type >= 0x19 && type <= 0x1E) {
        mapper_ = std::make_unique<MBC5Mapper>(rom_, ram_);
    } else {
        mapper_ = std::make_unique<NoMBCMapper>(rom_, ram_);
    }

    return true;
}

u8 Cartridge::read(u16 address) const {
    if (!mapper_) {
        return 0xFF;
    }
    return mapper_->read(address);
}

void Cartridge::write(u16 address, u8 value) {
    if (mapper_) {
        mapper_->write(address, value);
    }
}

std::string Cartridge::title() const {
    if (rom_.size() < 0x144) {
        return "<invalid>";
    }

    std::string title;
    for (u16 i = 0x134; i <= 0x143; ++i) {
        if (rom_[i] == 0) {
            break;
        }
        title.push_back(static_cast<char>(rom_[i]));
    }
    return title;
}

u8 Cartridge::cartridgeType() const {
    if (rom_.size() <= 0x147) {
        return 0xFF;
    }
    return rom_[0x147];
}

bool Cartridge::hasBatteryBackedRam() const {
    switch (cartridgeType()) {
    case 0x06: // MBC2+BATTERY
    case 0x03: // MBC1+RAM+BATTERY
    case 0x09: // ROM+RAM+BATTERY
    case 0x0F: // MBC3+TIMER+BATTERY
    case 0x10: // MBC3+TIMER+RAM+BATTERY
    case 0x13: // MBC3+RAM+BATTERY
    case 0x1B: // MBC5+RAM+BATTERY
    case 0x1E: // MBC5+RUMBLE+RAM+BATTERY
    case 0x0D: // MMM01+RAM+BATTERY
    case 0x22: // MBC7+SENSOR+RUMBLE+RAM+BATTERY
    case 0xFC: // Pocket Camera
    case 0xFE: // HuC3
    case 0xFF: // HuC1
        return !ram_.empty();
    default:
        return false;
    }
}

bool Cartridge::hasRam() const {
    return !ram_.empty();
}

bool Cartridge::hasRtc() const {
    if (!mapper_) {
        return false;
    }
    switch (cartridgeType()) {
    case 0x0F: // MBC3 + TIMER + BATTERY
    case 0x10: // MBC3 + TIMER + RAM + BATTERY
    case 0xFE: // HuC3 (mapeado como MBC3 RTC)
        return true;
    default:
        return false;
    }
}

const std::string& Cartridge::loadedPath() const {
    return loadedPath_;
}

bool Cartridge::cgbSupported() const {
    if (rom_.size() <= 0x143) {
        return false;
    }
    const u8 flag = rom_[0x143];
    return flag == 0x80 || flag == 0xC0;
}

bool Cartridge::cgbOnly() const {
    if (rom_.size() <= 0x143) {
        return false;
    }
    return rom_[0x143] == 0xC0;
}

bool Cartridge::shouldRunInCgbMode() const {
    return cgbSupported();
}

Cartridge::State Cartridge::state() const {
    State s{};
    s.type = cartridgeType();
    s.ram = ram_;
    if (mapper_) {
        s.mapper = mapper_->state();
    }
    return s;
}

void Cartridge::loadState(const State& s) {
    if (s.type != cartridgeType()) {
        return;
    }
    if (s.ram.size() == ram_.size()) {
        ram_ = s.ram;
    }
    if (mapper_) {
        mapper_->loadState(s.mapper);
    }
}

bool Cartridge::loadRamFromFile(const std::string& path) {
    if (ram_.empty()) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.read(reinterpret_cast<char*>(ram_.data()), static_cast<std::streamsize>(ram_.size()));
    return static_cast<std::size_t>(in.gcount()) == ram_.size();
}

bool Cartridge::saveRamToFile(const std::string& path) const {
    if (ram_.empty()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(ram_.data()), static_cast<std::streamsize>(ram_.size()));
    return static_cast<bool>(out);
}

bool Cartridge::loadRtcFromFile(const std::string& path) {
    if (!hasRtc() || !mapper_) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    u32 magic = 0;
    u32 version = 0;
    u8 type = 0;
    u32 payloadSize = 0;
    if (!readPod(in, magic)
        || !readPod(in, version)
        || !readPod(in, type)
        || !readPod(in, payloadSize)) {
        return false;
    }
    if (magic != RtcMagic || version != RtcVersion || type != cartridgeType()) {
        return false;
    }
    if (payloadSize > 4096) {
        return false;
    }

    std::vector<u8> payload(payloadSize, 0);
    if (payloadSize > 0) {
        in.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
        if (!in) {
            return false;
        }
    }

    const std::vector<u8> current = mapper_->state();
    if (payload.size() != current.size()) {
        return false;
    }
    mapper_->loadState(payload);
    return true;
}

bool Cartridge::saveRtcToFile(const std::string& path) const {
    if (!hasRtc() || !mapper_) {
        return false;
    }

    const std::vector<u8> payload = mapper_->state();
    if (payload.empty()) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    const u32 payloadSize = static_cast<u32>(payload.size());
    if (!writePod(out, RtcMagic)
        || !writePod(out, RtcVersion)
        || !writePod(out, cartridgeType())
        || !writePod(out, payloadSize)) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    return static_cast<bool>(out);
}

} // namespace gb
