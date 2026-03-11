#include "gb/cartridge.hpp"

#include <fstream>
#include <iterator>

namespace gb {

namespace {

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

    ram_.assign(ramSizeFromCode(rom_[0x149]), 0x00);

    const auto type = cartridgeType();
    if (type == 0x00 || type == 0x08 || type == 0x09) {
        mapper_ = std::make_unique<NoMBCMapper>(rom_, ram_);
    } else if (type >= 0x01 && type <= 0x03) {
        mapper_ = std::make_unique<MBC1Mapper>(rom_, ram_);
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

} // namespace gb
