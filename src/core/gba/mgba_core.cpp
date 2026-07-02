#include "gb/core/gba/mgba_core.hpp"

#ifdef GBEMU_HAVE_MGBA

#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/config.h>
#include <mgba/core/interface.h>
#include <mgba/core/log.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#endif
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/arm/arm.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <mgba/internal/gba/input.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

namespace gb::gba {

namespace {

constexpr unsigned AudioBufferSamples = 1536;

void quietMgbaLog(mLogger*, int, mLogLevel, const char*, va_list) {
}

mLogger quietMgbaLogger{quietMgbaLog, nullptr};

u16 mgbaColorToRgb565(color_t pixel) {
#ifdef COLOR_16_BIT
#ifdef COLOR_5_6_5
    return static_cast<u16>(pixel);
#else
    const u16 r5 = static_cast<u16>(pixel & 0x1FU);
    const u16 g5 = static_cast<u16>((pixel >> 5U) & 0x1FU);
    const u16 b5 = static_cast<u16>((pixel >> 10U) & 0x1FU);
    const u16 g6 = static_cast<u16>((g5 << 1U) | (g5 >> 4U));
    return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
#endif
#else
    const u16 r5 = static_cast<u16>((pixel & 0x000000FFU) >> 3U);
    const u16 g6 = static_cast<u16>(((pixel >> 8U) & 0x000000FFU) >> 2U);
    const u16 b5 = static_cast<u16>(((pixel >> 16U) & 0x000000FFU) >> 3U);
    return static_cast<u16>((r5 << 11U) | (g6 << 5U) | b5);
#endif
}

std::vector<u8> readFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<u8>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uint32_t inputToMgbaKeys(const InputState& input) {
    std::uint32_t keys = 0;
    keys |= input.a ? (1U << GBA_KEY_A) : 0U;
    keys |= input.b ? (1U << GBA_KEY_B) : 0U;
    keys |= input.select ? (1U << GBA_KEY_SELECT) : 0U;
    keys |= input.start ? (1U << GBA_KEY_START) : 0U;
    keys |= input.right ? (1U << GBA_KEY_RIGHT) : 0U;
    keys |= input.left ? (1U << GBA_KEY_LEFT) : 0U;
    keys |= input.up ? (1U << GBA_KEY_UP) : 0U;
    keys |= input.down ? (1U << GBA_KEY_DOWN) : 0U;
    keys |= input.r ? (1U << GBA_KEY_R) : 0U;
    keys |= input.l ? (1U << GBA_KEY_L) : 0U;
    return keys;
}

std::string privilegeModeName(PrivilegeMode mode) {
    switch (mode) {
    case MODE_USER: return "USER";
    case MODE_FIQ: return "FIQ";
    case MODE_IRQ: return "IRQ";
    case MODE_SUPERVISOR: return "SVC";
    case MODE_ABORT: return "ABT";
    case MODE_UNDEFINED: return "UND";
    case MODE_SYSTEM: return "SYS";
    default: return "UNK";
    }
}

bool hasCoreDebug(const mCore* core) {
    return core != nullptr && core->cpu != nullptr;
}

} // namespace

struct MgbaCore::Impl {
    mCore* core = nullptr;
    mAVStream av{};
    std::vector<color_t> videoBuffer;
    std::array<u16, FramebufferSize> framebuffer{};
    std::vector<std::int16_t> samples;
    InputState input{};
    std::string loadedRom;
    std::string coreName = "mGBA";
    bool gameLoaded = false;

    static Impl* active;

    void configureAvStream() {
        av = {};
        av.videoDimensionsChanged = videoDimensionsChanged;
        av.audioRateChanged = audioRateChanged;
        av.postVideoFrame = postVideoFrame;
        av.postAudioFrame = nullptr;
        av.postAudioBuffer = postAudioBuffer;
    }

    static void videoDimensionsChanged(mAVStream*, unsigned, unsigned) {
    }

    static void audioRateChanged(mAVStream*, unsigned) {
    }

    static void postVideoFrame(mAVStream*, const color_t* buffer, size_t stride) {
        if (active == nullptr || buffer == nullptr) {
            return;
        }
        for (int y = 0; y < ScreenHeight; ++y) {
            const color_t* row = buffer + static_cast<std::size_t>(y) * stride;
            for (int x = 0; x < ScreenWidth; ++x) {
                active->framebuffer[static_cast<std::size_t>(y) * ScreenWidth + static_cast<std::size_t>(x)] =
                    mgbaColorToRgb565(row[x]);
            }
        }
    }

    static void postAudioBuffer(mAVStream*, blip_t* left, blip_t* right) {
        if (active == nullptr || left == nullptr || right == nullptr) {
            return;
        }
        const int leftSamples = blip_samples_avail(left);
        const int rightSamples = blip_samples_avail(right);
        const int frames = std::min(leftSamples, rightSamples);
        if (frames <= 0) {
            return;
        }

        std::vector<std::int16_t> leftBuffer(static_cast<std::size_t>(frames));
        std::vector<std::int16_t> rightBuffer(static_cast<std::size_t>(frames));
        const int gotLeft = blip_read_samples(left, leftBuffer.data(), frames, 0);
        const int gotRight = blip_read_samples(right, rightBuffer.data(), frames, 0);
        const int gotFrames = std::min(gotLeft, gotRight);
        if (gotFrames <= 0) {
            return;
        }

        active->samples.reserve(active->samples.size() + static_cast<std::size_t>(gotFrames) * 2U);
        for (int i = 0; i < gotFrames; ++i) {
            const auto leftSample = leftBuffer[static_cast<std::size_t>(i)];
            const auto rightSample = rightBuffer[static_cast<std::size_t>(i)];
            active->samples.push_back(leftSample);
            active->samples.push_back(rightSample);
        }
    }

    void configureAudioChannels() {
        if (core == nullptr || core->getAudioChannel == nullptr || core->frequency == nullptr) {
            return;
        }
        blip_t* left = core->getAudioChannel(core, 0);
        blip_t* right = core->getAudioChannel(core, 1);
        if (left == nullptr || right == nullptr) {
            return;
        }
        const std::int32_t clockRate = core->frequency(core);
        blip_set_rates(left, clockRate, SampleRate);
        blip_set_rates(right, clockRate, SampleRate);
    }

    void configureInternalAudioVolume() {
        if (core == nullptr || core->platform(core) != mPLATFORM_GBA || core->board == nullptr) {
            return;
        }
        static_cast<GBA*>(core->board)->audio.masterVolume = GBA_AUDIO_VOLUME_MAX;
    }
};

MgbaCore::Impl* MgbaCore::Impl::active = nullptr;

MgbaCore::MgbaCore()
    : impl_(new Impl()) {
}

MgbaCore::~MgbaCore() {
    unload();
    delete impl_;
}

bool MgbaCore::loadRomFromFile(const std::string& romPath) {
    unload();
    mLogSetDefaultLogger(&quietMgbaLogger);
    impl_->core = mCoreCreate(mPLATFORM_GBA);
    if (impl_->core == nullptr || !impl_->core->init(impl_->core)) {
        std::cerr << "falha ao inicializar core nativo mGBA\n";
        unload();
        return false;
    }
    mCoreInitConfig(impl_->core, "orbitalboy");
    mCoreLoadConfig(impl_->core);
    mCoreConfigSetOverrideValue(&impl_->core->config, "mute", "0");
    mCoreConfigSetOverrideValue(&impl_->core->config, "useBios", "0");
    mCoreConfigSetOverrideValue(&impl_->core->config, "skipBios", "1");
    mCoreConfigSetOverrideIntValue(&impl_->core->config, "logLevel", mLOG_FATAL | mLOG_ERROR);
    mCoreConfigSetOverrideIntValue(&impl_->core->config, "volume", 100);
    mCoreConfigSetOverrideUIntValue(&impl_->core->config, "sampleRate", SampleRate);
    mCoreConfigSetOverrideUIntValue(&impl_->core->config, "audioBuffers", AudioBufferSamples);
    impl_->core->loadConfig(impl_->core, &impl_->core->config);
    impl_->core->opts.mute = false;
    impl_->core->opts.logLevel = mLOG_FATAL | mLOG_ERROR;
    impl_->core->opts.volume = 100;
    impl_->core->opts.sampleRate = SampleRate;
    impl_->core->opts.audioBuffers = AudioBufferSamples;
    impl_->core->opts.audioSync = false;

    impl_->videoBuffer.assign(FramebufferSize, 0);
    impl_->core->setVideoBuffer(impl_->core, impl_->videoBuffer.data(), ScreenWidth);

    if (!mCoreLoadFile(impl_->core, romPath.c_str())) {
        std::cerr << "mGBA nao conseguiu carregar ROM: " << romPath << "\n";
        unload();
        return false;
    }

    impl_->core->setAudioBufferSize(impl_->core, AudioBufferSamples);
    impl_->configureAudioChannels();
    impl_->configureAvStream();
    impl_->core->setAVStream(impl_->core, &impl_->av);
    impl_->core->reset(impl_->core);
    impl_->configureInternalAudioVolume();
    impl_->core->setAudioBufferSize(impl_->core, AudioBufferSamples);
    impl_->configureAudioChannels();
    impl_->core->setAVStream(impl_->core, &impl_->av);
    impl_->loadedRom = romPath;
    impl_->gameLoaded = true;
    impl_->framebuffer.fill(0);
    impl_->samples.clear();
    Impl::active = impl_;
    return true;
}

void MgbaCore::unload() {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->core != nullptr) {
        if (impl_->gameLoaded) {
            impl_->core->unloadROM(impl_->core);
        }
        impl_->core->deinit(impl_->core);
        impl_->core = nullptr;
    }
    if (Impl::active == impl_) {
        Impl::active = nullptr;
    }
    impl_->gameLoaded = false;
    impl_->loadedRom.clear();
    impl_->samples.clear();
    impl_->videoBuffer.clear();
    impl_->framebuffer.fill(0);
}

void MgbaCore::setInputState(const InputState& input) {
    impl_->input = input;
}

void MgbaCore::runFrame() {
    if (!impl_->gameLoaded || impl_->core == nullptr) {
        return;
    }
    Impl::active = impl_;
    impl_->configureInternalAudioVolume();
    impl_->core->setKeys(impl_->core, inputToMgbaKeys(impl_->input));
    impl_->core->runFrame(impl_->core);
    Impl::postVideoFrame(&impl_->av, impl_->videoBuffer.data(), ScreenWidth);
}

void MgbaCore::stepInstruction() {
    if (!impl_->gameLoaded || impl_->core == nullptr || impl_->core->step == nullptr) {
        return;
    }
    Impl::active = impl_;
    impl_->configureInternalAudioVolume();
    impl_->core->setKeys(impl_->core, inputToMgbaKeys(impl_->input));
    impl_->core->step(impl_->core);
    Impl::postVideoFrame(&impl_->av, impl_->videoBuffer.data(), ScreenWidth);
}

const std::array<u16, MgbaCore::FramebufferSize>& MgbaCore::framebuffer() const {
    return impl_->framebuffer;
}

std::vector<std::int16_t> MgbaCore::takeSamples() {
    std::vector<std::int16_t> out;
    out.swap(impl_->samples);
    impl_->samples.reserve(out.capacity());
    return out;
}

bool MgbaCore::debugAvailable() const {
    return impl_->gameLoaded && hasCoreDebug(impl_->core);
}

GbaDebugSnapshot MgbaCore::debugSnapshot() const {
    GbaDebugSnapshot snapshot{};
    if (!debugAvailable()) {
        return snapshot;
    }

    snapshot.available = true;
    snapshot.frameCounter = impl_->core->frameCounter != nullptr ? impl_->core->frameCounter(impl_->core) : 0U;
    const auto* cpu = static_cast<const ARMCore*>(impl_->core->cpu);
    for (std::size_t i = 0; i < snapshot.cpu.regs.size(); ++i) {
        snapshot.cpu.regs[i] = static_cast<u32>(cpu->gprs[i]);
    }
    snapshot.cpu.cpsr = static_cast<u32>(cpu->cpsr.packed);
    snapshot.cpu.spsr = static_cast<u32>(cpu->spsr.packed);
    snapshot.cpu.thumb = cpu->executionMode == MODE_THUMB || cpu->cpsr.t != 0;
    snapshot.cpu.mode = privilegeModeName(cpu->privilegeMode);

    snapshot.memoryBlocks = {
        GbaMemoryBlockDebugInfo{"BIOS", "BIOS", 0x00000000U, 0x00003FFFU, 0x00004000U, true, false},
        GbaMemoryBlockDebugInfo{"EWRAM", "External Work RAM", 0x02000000U, 0x0203FFFFU, 0x00040000U, true, true},
        GbaMemoryBlockDebugInfo{"IWRAM", "Internal Work RAM", 0x03000000U, 0x03007FFFU, 0x00008000U, true, true},
        GbaMemoryBlockDebugInfo{"IO", "I/O Registers", 0x04000000U, 0x040003FEU, 0x00000400U, true, true},
        GbaMemoryBlockDebugInfo{"PRAM", "Palette RAM", 0x05000000U, 0x050003FFU, 0x00000400U, true, true},
        GbaMemoryBlockDebugInfo{"VRAM", "Video RAM", 0x06000000U, 0x06017FFFU, 0x00018000U, true, true},
        GbaMemoryBlockDebugInfo{"OAM", "Object Attribute Memory", 0x07000000U, 0x070003FFU, 0x00000400U, true, true},
        GbaMemoryBlockDebugInfo{"ROM", "Game Pak ROM", 0x08000000U, 0x0DFFFFFFU, 0x06000000U, true, false},
        GbaMemoryBlockDebugInfo{"SAVE", "Game Pak Save", 0x0E000000U, 0x0E00FFFFU, 0x00010000U, true, true},
    };

    return snapshot;
}

std::optional<u8> MgbaCore::debugRead8(u32 address) const {
    if (!debugAvailable() || impl_->core->busRead8 == nullptr) {
        return std::nullopt;
    }
    return static_cast<u8>(impl_->core->busRead8(impl_->core, address) & 0xFFU);
}

std::optional<u16> MgbaCore::debugRead16(u32 address) const {
    if (!debugAvailable() || impl_->core->busRead16 == nullptr) {
        return std::nullopt;
    }
    return static_cast<u16>(impl_->core->busRead16(impl_->core, address) & 0xFFFFU);
}

std::optional<u32> MgbaCore::debugRead32(u32 address) const {
    if (!debugAvailable() || impl_->core->busRead32 == nullptr) {
        return std::nullopt;
    }
    return static_cast<u32>(impl_->core->busRead32(impl_->core, address));
}

bool MgbaCore::debugWrite8(u32 address, u8 value) {
    if (!debugAvailable() || impl_->core->busWrite8 == nullptr) {
        return false;
    }
    impl_->core->busWrite8(impl_->core, address, value);
    return true;
}

bool MgbaCore::debugWrite16(u32 address, u16 value) {
    if (!debugAvailable() || impl_->core->busWrite16 == nullptr) {
        return false;
    }
    impl_->core->busWrite16(impl_->core, address, value);
    return true;
}

bool MgbaCore::debugWrite32(u32 address, u32 value) {
    if (!debugAvailable() || impl_->core->busWrite32 == nullptr) {
        return false;
    }
    impl_->core->busWrite32(impl_->core, address, value);
    return true;
}

bool MgbaCore::loadBackupFromFile(const std::string& path) {
    if (!impl_->gameLoaded || impl_->core == nullptr || !std::filesystem::exists(path)) {
        return false;
    }
    const auto data = readFileBytes(path);
    if (data.empty() || impl_->core->savedataRestore == nullptr) {
        return false;
    }
    return impl_->core->savedataRestore(impl_->core, data.data(), data.size(), true);
}

bool MgbaCore::saveBackupToFile(const std::string& path) const {
    if (!impl_->gameLoaded || impl_->core == nullptr || impl_->core->savedataClone == nullptr) {
        return false;
    }
    void* data = nullptr;
    const std::size_t size = impl_->core->savedataClone(impl_->core, &data);
    if (data == nullptr || size == 0U) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (out) {
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    std::free(data);
    return static_cast<bool>(out);
}

bool MgbaCore::loadStateFromFile(const std::string& path) {
    if (!impl_->gameLoaded || impl_->core == nullptr || impl_->core->loadState == nullptr || !std::filesystem::exists(path)) {
        return false;
    }
    const auto data = readFileBytes(path);
    const std::size_t expectedSize = impl_->core->stateSize != nullptr ? impl_->core->stateSize(impl_->core) : 0U;
    if (data.empty() || expectedSize == 0U || data.size() != expectedSize) {
        return false;
    }
    const bool loaded = impl_->core->loadState(impl_->core, data.data());
    if (loaded) {
        Impl::active = impl_;
        Impl::postVideoFrame(&impl_->av, impl_->videoBuffer.data(), ScreenWidth);
        impl_->samples.clear();
    }
    return loaded;
}

bool MgbaCore::saveStateToFile(const std::string& path) const {
    if (!impl_->gameLoaded || impl_->core == nullptr || impl_->core->stateSize == nullptr || impl_->core->saveState == nullptr) {
        return false;
    }
    const std::size_t size = impl_->core->stateSize(impl_->core);
    if (size == 0U) {
        return false;
    }
    std::vector<u8> data(size);
    if (!impl_->core->saveState(impl_->core, data.data())) {
        return false;
    }
    const std::filesystem::path outPath(path);
    std::error_code ec;
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

bool MgbaCore::loaded() const {
    return impl_->gameLoaded;
}

const std::string& MgbaCore::loadedRomPath() const {
    return impl_->loadedRom;
}

const std::string& MgbaCore::coreName() const {
    return impl_->coreName;
}

} // namespace gb::gba

#else

namespace gb::gba {

struct MgbaCore::Impl {};

MgbaCore::MgbaCore() : impl_(new Impl()) {}
MgbaCore::~MgbaCore() { delete impl_; }
bool MgbaCore::loadRomFromFile(const std::string&) { return false; }
void MgbaCore::unload() {}
void MgbaCore::setInputState(const InputState&) {}
void MgbaCore::runFrame() {}
void MgbaCore::stepInstruction() {}
const std::array<u16, MgbaCore::FramebufferSize>& MgbaCore::framebuffer() const {
    static const std::array<u16, FramebufferSize> empty{};
    return empty;
}
std::vector<std::int16_t> MgbaCore::takeSamples() { return {}; }
bool MgbaCore::debugAvailable() const { return false; }
GbaDebugSnapshot MgbaCore::debugSnapshot() const { return {}; }
std::optional<u8> MgbaCore::debugRead8(u32) const { return std::nullopt; }
std::optional<u16> MgbaCore::debugRead16(u32) const { return std::nullopt; }
std::optional<u32> MgbaCore::debugRead32(u32) const { return std::nullopt; }
bool MgbaCore::debugWrite8(u32, u8) { return false; }
bool MgbaCore::debugWrite16(u32, u16) { return false; }
bool MgbaCore::debugWrite32(u32, u32) { return false; }
bool MgbaCore::loadBackupFromFile(const std::string&) { return false; }
bool MgbaCore::saveBackupToFile(const std::string&) const { return false; }
bool MgbaCore::loadStateFromFile(const std::string&) { return false; }
bool MgbaCore::saveStateToFile(const std::string&) const { return false; }
bool MgbaCore::loaded() const { return false; }
const std::string& MgbaCore::loadedRomPath() const {
    static const std::string empty;
    return empty;
}
const std::string& MgbaCore::coreName() const {
    static const std::string name = "mGBA unavailable";
    return name;
}

} // namespace gb::gba

#endif
