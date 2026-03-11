#include "gb/core/cartridge.hpp"
#include "cartridge/mappers/factory.hpp"

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
        mapper_ = cartridge_mapper::makeNoMbcMapper(rom_, ram_);
    } else if (type >= 0x01 && type <= 0x03) {
        mapper_ = cartridge_mapper::makeMbc1Mapper(rom_, ram_);
    } else if (type >= 0x0B && type <= 0x0D) { // MMM01 family (fallback)
        mapper_ = cartridge_mapper::makeMbc1Mapper(rom_, ram_);
    } else if (type == 0x05 || type == 0x06) {
        mapper_ = cartridge_mapper::makeMbc2Mapper(rom_, ram_);
    } else if (type >= 0x0F && type <= 0x13) {
        mapper_ = cartridge_mapper::makeMbc3Mapper(rom_, ram_);
    } else if (type == 0xFE) { // HuC3 (tratado como MBC3+RTC)
        mapper_ = cartridge_mapper::makeMbc3Mapper(rom_, ram_);
    } else if (type == 0xFF) { // HuC1 (tratado como MBC1)
        mapper_ = cartridge_mapper::makeMbc1Mapper(rom_, ram_);
    } else if (type == 0x22 || type == 0xFC) { // MBC7 / Pocket Camera fallback
        mapper_ = cartridge_mapper::makeMbc5Mapper(rom_, ram_);
    } else if (type >= 0x19 && type <= 0x1E) {
        mapper_ = cartridge_mapper::makeMbc5Mapper(rom_, ram_);
    } else {
        mapper_ = cartridge_mapper::makeNoMbcMapper(rom_, ram_);
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
