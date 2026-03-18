#include "gb/core/gba/apu.hpp"

#include "gb/core/environment.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <iostream>

namespace gb::gba {

namespace {

constexpr u32 kSoundCntL = 0x0080U;
constexpr u32 kSoundCntH = 0x0082U;
constexpr u32 kSoundCntX = 0x0084U;
constexpr u32 kSoundBias = 0x0088U;

static_assert((Apu::CpuFrequency % Apu::SampleRate) == 0, "GBA sample interval must be integral");
constexpr int kCyclesPerSample = Apu::CpuFrequency / Apu::SampleRate;
constexpr int kPsgFrameSequencerPeriod = 8192;

constexpr int kHalfVolumeScale = 1;
constexpr int kFullVolumeScale = 2;
constexpr int kBiasMask = 0x03FF;
constexpr float kDefaultOutputScale = 56.0f;
constexpr float kDefaultHighPassA = 0.998f;
constexpr float kDefaultLowPassA = 0.12f;

constexpr std::array<std::array<int, 8>, 4> kDutyPatterns = {{
    {{0, 0, 0, 0, 0, 0, 0, 1}},
    {{1, 0, 0, 0, 0, 0, 0, 1}},
    {{1, 0, 0, 0, 0, 1, 1, 1}},
    {{0, 1, 1, 1, 1, 1, 1, 0}},
}};

constexpr std::array<int, 4> kPsgMasterScale = {1, 2, 4, 4};

bool gbaAudioDisablePsg() {
    static const bool disabled = gb::environmentVariableEnabled("GBEMU_GBA_AUDIO_DISABLE_PSG");
    return disabled;
}

bool gbaAudioDisableFifo() {
    static const bool disabled = gb::environmentVariableEnabled("GBEMU_GBA_AUDIO_DISABLE_FIFO");
    return disabled;
}

bool gbaAudioStateLoggingEnabled() {
    static const bool enabled = gb::environmentVariableEnabled("GBEMU_GBA_LOG_AUDIO_STATE");
    return enabled;
}

int gbaAudioStateLogEvery() {
    static const int every = []() {
        const auto value = gb::readEnvironmentVariable("GBEMU_GBA_LOG_AUDIO_STATE_EVERY");
        if (!value.has_value() || value->empty()) {
            return 60;
        }
        try {
            return std::max(1, std::stoi(*value));
        } catch (...) {
            return 60;
        }
    }();
    return every;
}

float readFloatEnvironmentOrDefault(const char* name, float fallback) {
    const auto value = gb::readEnvironmentVariable(name);
    if (!value.has_value() || value->empty()) {
        return fallback;
    }
    try {
        return static_cast<float>(std::stod(*value));
    } catch (...) {
        return fallback;
    }
}

float gbaAudioOutputScale() {
    static const float scale = std::clamp(
        readFloatEnvironmentOrDefault("GBEMU_GBA_AUDIO_OUTPUT_SCALE", kDefaultOutputScale),
        1.0f,
        256.0f);
    return scale;
}

float gbaAudioHighPassCoeff() {
    static const float coeff = std::clamp(
        readFloatEnvironmentOrDefault("GBEMU_GBA_AUDIO_HIGHPASS_A", kDefaultHighPassA),
        0.0f,
        0.99995f);
    return coeff;
}

float gbaAudioLowPassCoeff() {
    static const float coeff = std::clamp(
        readFloatEnvironmentOrDefault("GBEMU_GBA_AUDIO_LOWPASS_A", kDefaultLowPassA),
        0.0f,
        1.0f);
    return coeff;
}

u8 envPeriodOr8(u8 period) {
    return period == 0 ? 8 : period;
}

int16_t clampOutputSample(float sample, float outputScale) {
    return static_cast<int16_t>(std::clamp(sample * outputScale, -32768.0f, 32767.0f));
}

void appendRepeatedStereoSamples(std::vector<int16_t>& buffer, int16_t left, int16_t right, int count) {
    if (count <= 0) {
        return;
    }

    const std::size_t start = buffer.size();
    buffer.resize(start + static_cast<std::size_t>(count) * 2U);
    int16_t* out = buffer.data() + start;
    for (int index = 0; index < count; ++index) {
        *out++ = left;
        *out++ = right;
    }
}

int16_t filterHardwareDacSample(
    int sample,
    float highPassA,
    float lowPassA,
    float outputScale,
    float& prevIn,
    float& prevOut,
    float& lowPassPrevOut
) {
    const float input = static_cast<float>(sample);
    float filtered = input;

    if (highPassA > 0.0f) {
        filtered = (input - prevIn) + (highPassA * prevOut);
    }
    prevIn = input;
    prevOut = filtered;

    if (lowPassA > 0.0f) {
        lowPassPrevOut += lowPassA * (filtered - lowPassPrevOut);
        filtered = lowPassPrevOut;
    } else {
        lowPassPrevOut = filtered;
    }

    return clampOutputSample(filtered, outputScale);
}

} // namespace

void Apu::reset() {
    cycleAccumulator_ = 0;
    sampleBuffer_.clear();
    lastTickStats_ = TickStats{};
    currentSampleA_ = 0;
    currentSampleB_ = 0;
    ch1_ = Apu::SquareChannel{};
    ch2_ = Apu::SquareChannel{};
    ch3_ = Apu::WaveChannel{};
    ch4_ = Apu::NoiseChannel{};
    nr10_ = 0;
    sweepEnabled_ = false;
    sweepPeriod_ = 0;
    sweepTimer_ = 0;
    sweepNegate_ = false;
    sweepShift_ = 0;
    sweepShadowFreq_ = 0;
    waveRam_.fill(0);
    psgFrameSeqCycles_ = 0;
    psgFrameSeqStep_ = 0;
    hpPrevInL_ = 0.0f;
    hpPrevOutL_ = 0.0f;
    hpPrevInR_ = 0.0f;
    hpPrevOutR_ = 0.0f;
    lpPrevOutL_ = 0.0f;
    lpPrevOutR_ = 0.0f;
}

const Apu::TickStats& Apu::lastTickStats() const {
    return lastTickStats_;
}

void Apu::triggerSquare(Apu::SquareChannel& channel, bool withSweep) {
    channel.enabled = channel.dacEnabled;
    if (channel.lengthCounter == 0) {
        channel.lengthCounter = 64;
    }
    channel.timerCycles = 0;
    channel.dutyStep = 0;
    channel.currentVolume = static_cast<u8>((channel.nrx2 >> 4U) & 0x0FU);
    channel.envelopeIncrease = (channel.nrx2 & 0x08U) != 0U;
    channel.envelopePeriod = static_cast<u8>(channel.nrx2 & 0x07U);
    channel.envelopeTimer = envPeriodOr8(channel.envelopePeriod);

    if (withSweep) {
        sweepShadowFreq_ = static_cast<u16>(((ch1_.nrx4 & 0x07U) << 8U) | ch1_.nrx3);
        sweepTimer_ = sweepPeriod_ == 0 ? 8 : sweepPeriod_;
        sweepEnabled_ = (sweepPeriod_ != 0) || (sweepShift_ != 0);
        if (sweepShift_ != 0) {
            applySweep(false);
        }
    }
}

void Apu::triggerWave() {
    ch3_.enabled = ch3_.dacEnabled;
    if (ch3_.lengthCounter == 0) {
        ch3_.lengthCounter = 256;
    }
    ch3_.waveStep = 0;
    ch3_.timerCycles = 0;
}

void Apu::triggerNoise() {
    ch4_.enabled = ch4_.dacEnabled;
    if (ch4_.lengthCounter == 0) {
        ch4_.lengthCounter = 64;
    }
    ch4_.timerCycles = 0;
    ch4_.lfsr = 0x7FFF;
    ch4_.currentVolume = static_cast<u8>((ch4_.nr42 >> 4U) & 0x0FU);
    ch4_.envelopeIncrease = (ch4_.nr42 & 0x08U) != 0U;
    ch4_.envelopePeriod = static_cast<u8>(ch4_.nr42 & 0x07U);
    ch4_.envelopeTimer = envPeriodOr8(ch4_.envelopePeriod);
}

void Apu::clockLength() {
    if (ch1_.lengthEnabled && ch1_.lengthCounter > 0) {
        --ch1_.lengthCounter;
        if (ch1_.lengthCounter == 0) {
            ch1_.enabled = false;
        }
    }
    if (ch2_.lengthEnabled && ch2_.lengthCounter > 0) {
        --ch2_.lengthCounter;
        if (ch2_.lengthCounter == 0) {
            ch2_.enabled = false;
        }
    }
    if (ch3_.lengthEnabled && ch3_.lengthCounter > 0) {
        --ch3_.lengthCounter;
        if (ch3_.lengthCounter == 0) {
            ch3_.enabled = false;
        }
    }
    if (ch4_.lengthEnabled && ch4_.lengthCounter > 0) {
        --ch4_.lengthCounter;
        if (ch4_.lengthCounter == 0) {
            ch4_.enabled = false;
        }
    }
}

void Apu::clockEnvelope() {
    for (Apu::SquareChannel* channel : {&ch1_, &ch2_}) {
        if (!channel->enabled || channel->envelopePeriod == 0) {
            continue;
        }
        if (channel->envelopeTimer > 0) {
            --channel->envelopeTimer;
        }
        if (channel->envelopeTimer != 0) {
            continue;
        }
        channel->envelopeTimer = envPeriodOr8(channel->envelopePeriod);
        if (channel->envelopeIncrease) {
            if (channel->currentVolume < 15) {
                ++channel->currentVolume;
            }
        } else if (channel->currentVolume > 0) {
            --channel->currentVolume;
        }
    }

    if (!ch4_.enabled || ch4_.envelopePeriod == 0) {
        return;
    }
    if (ch4_.envelopeTimer > 0) {
        --ch4_.envelopeTimer;
    }
    if (ch4_.envelopeTimer != 0) {
        return;
    }
    ch4_.envelopeTimer = envPeriodOr8(ch4_.envelopePeriod);
    if (ch4_.envelopeIncrease) {
        if (ch4_.currentVolume < 15) {
            ++ch4_.currentVolume;
        }
    } else if (ch4_.currentVolume > 0) {
        --ch4_.currentVolume;
    }
}

bool Apu::applySweep(bool writeBack) {
    if (sweepShift_ == 0) {
        return true;
    }

    const u16 delta = static_cast<u16>(sweepShadowFreq_ >> sweepShift_);
    u16 next = sweepShadowFreq_;
    if (sweepNegate_) {
        next = static_cast<u16>(sweepShadowFreq_ - delta);
    } else {
        next = static_cast<u16>(sweepShadowFreq_ + delta);
    }
    if (next > 2047) {
        return false;
    }

    if (writeBack) {
        sweepShadowFreq_ = next;
        ch1_.nrx3 = static_cast<u8>(next & 0xFFU);
        ch1_.nrx4 = static_cast<u8>((ch1_.nrx4 & 0xF8U) | ((next >> 8U) & 0x07U));
    }
    return true;
}

void Apu::clockSweep() {
    if (!sweepEnabled_) {
        return;
    }
    if (sweepTimer_ > 0) {
        --sweepTimer_;
    }
    if (sweepTimer_ != 0) {
        return;
    }

    sweepTimer_ = sweepPeriod_ == 0 ? 8 : sweepPeriod_;
    if (sweepPeriod_ == 0) {
        return;
    }

    if (!applySweep(true)) {
        ch1_.enabled = false;
        return;
    }
    if (!applySweep(false)) {
        ch1_.enabled = false;
    }
}

void Apu::frameSequencerStep() {
    psgFrameSeqStep_ = static_cast<u8>((psgFrameSeqStep_ + 1U) & 0x07U);
    if ((psgFrameSeqStep_ & 1U) == 0U) {
        clockLength();
    }
    if (psgFrameSeqStep_ == 2U || psgFrameSeqStep_ == 6U) {
        clockSweep();
    }
    if (psgFrameSeqStep_ == 7U) {
        clockEnvelope();
    }
}

int Apu::sampleSquare(const Apu::SquareChannel& channel) const {
    if (!channel.enabled || !channel.dacEnabled || channel.currentVolume == 0) {
        return 0;
    }
    const int duty = (channel.nrx1 >> 6U) & 0x03;
    const int wave = kDutyPatterns[static_cast<std::size_t>(duty)][channel.dutyStep] ? 1 : -1;
    return wave * static_cast<int>(channel.currentVolume);
}

int Apu::sampleWave() const {
    if (!ch3_.enabled || !ch3_.dacEnabled) {
        return 0;
    }

    const u8 sampleByte = waveRam_[static_cast<std::size_t>(ch3_.waveStep / 2U)];
    const u8 nibble = (ch3_.waveStep & 1U) != 0U
        ? static_cast<u8>(sampleByte & 0x0FU)
        : static_cast<u8>((sampleByte >> 4U) & 0x0FU);
    int sample = static_cast<int>(nibble) - 8;

    if ((ch3_.nr32 & 0x80U) != 0U) {
        sample = (sample * 3) / 4;
    } else {
        switch ((ch3_.nr32 >> 5U) & 0x03U) {
        case 0: return 0;
        case 1: break;
        case 2: sample >>= 1; break;
        case 3: sample >>= 2; break;
        default: return 0;
        }
    }

    return sample;
}

int Apu::sampleNoise() const {
    if (!ch4_.enabled || !ch4_.dacEnabled || ch4_.currentVolume == 0) {
        return 0;
    }
    const int wave = (ch4_.lfsr & 0x01U) == 0U ? 1 : -1;
    return wave * static_cast<int>(ch4_.currentVolume);
}

int Apu::mixPsgLeft(u16 soundCntL, u16 soundCntH) const {
    int mixed = 0;
    if ((soundCntL & 0x1000U) != 0U) {
        mixed += sampleSquare(ch1_);
    }
    if ((soundCntL & 0x2000U) != 0U) {
        mixed += sampleSquare(ch2_);
    }
    if ((soundCntL & 0x4000U) != 0U) {
        mixed += sampleWave();
    }
    if ((soundCntL & 0x8000U) != 0U) {
        mixed += sampleNoise();
    }
    return (mixed * kPsgMasterScale[static_cast<std::size_t>(soundCntH & 0x0003U)]
            * (static_cast<int>((soundCntL >> 4U) & 0x0007U) + 1)) / 16;
}

int Apu::mixPsgRight(u16 soundCntL, u16 soundCntH) const {
    int mixed = 0;
    if ((soundCntL & 0x0100U) != 0U) {
        mixed += sampleSquare(ch1_);
    }
    if ((soundCntL & 0x0200U) != 0U) {
        mixed += sampleSquare(ch2_);
    }
    if ((soundCntL & 0x0400U) != 0U) {
        mixed += sampleWave();
    }
    if ((soundCntL & 0x0800U) != 0U) {
        mixed += sampleNoise();
    }
    return (mixed * kPsgMasterScale[static_cast<std::size_t>(soundCntH & 0x0003U)]
            * (static_cast<int>(soundCntL & 0x0007U) + 1)) / 16;
}

void Apu::stepPsg(int cycles) {
    if (cycles <= 0) {
        return;
    }

    const auto advanceSquare = [cycles](Apu::SquareChannel& channel) {
        if (!channel.enabled) {
            return;
        }
        const u16 frequency = static_cast<u16>(((channel.nrx4 & 0x07U) << 8U) | channel.nrx3);
        const u32 period = static_cast<u32>((2048 - frequency) * 4);
        if (period == 0U) {
            return;
        }
        channel.timerCycles += static_cast<u32>(cycles);
        const u32 steps = channel.timerCycles / period;
        channel.timerCycles %= period;
        channel.dutyStep = static_cast<u8>((channel.dutyStep + steps) & 0x07U);
    };

    advanceSquare(ch1_);
    advanceSquare(ch2_);

    if (ch3_.enabled && ch3_.dacEnabled) {
        const u16 frequency = static_cast<u16>(((ch3_.nr34 & 0x07U) << 8U) | ch3_.nr33);
        const u32 period = static_cast<u32>((2048 - frequency) * 2);
        if (period != 0U) {
            ch3_.timerCycles += static_cast<u32>(cycles);
            const u32 steps = ch3_.timerCycles / period;
            ch3_.timerCycles %= period;
            ch3_.waveStep = static_cast<u8>((ch3_.waveStep + steps) & 0x1FU);
        }
    }

    if (ch4_.enabled && ch4_.dacEnabled) {
        const u8 divisorCode = static_cast<u8>(ch4_.nr43 & 0x07U);
        const u8 clockShift = static_cast<u8>((ch4_.nr43 >> 4U) & 0x0FU);
        const u32 divisor = divisorCode == 0 ? 8U : static_cast<u32>(divisorCode) * 16U;
        const u32 period = divisor << clockShift;
        if (period != 0U) {
            ch4_.timerCycles += static_cast<u32>(cycles);
            while (ch4_.timerCycles >= period) {
                ch4_.timerCycles -= period;
                const u16 xorBit = static_cast<u16>((ch4_.lfsr & 0x01U) ^ ((ch4_.lfsr >> 1U) & 0x01U));
                ch4_.lfsr = static_cast<u16>((ch4_.lfsr >> 1U) | (xorBit << 14U));
                if ((ch4_.nr43 & 0x08U) != 0U) {
                    ch4_.lfsr = static_cast<u16>((ch4_.lfsr & ~(1U << 6U)) | (xorBit << 6U));
                }
            }
        }
    }

    psgFrameSeqCycles_ += cycles;
    while (psgFrameSeqCycles_ >= kPsgFrameSequencerPeriod) {
        psgFrameSeqCycles_ -= kPsgFrameSequencerPeriod;
        frameSequencerStep();
    }
}

void Apu::applyAudioRegisterWrite(const Memory::AudioRegisterWriteEvent& event) {
    switch (event.ioOffset) {
    case 0x0060U:
        nr10_ = static_cast<u8>(event.value | 0x80U);
        sweepPeriod_ = static_cast<u8>((event.value >> 4U) & 0x07U);
        sweepNegate_ = (event.value & 0x08U) != 0U;
        sweepShift_ = static_cast<u8>(event.value & 0x07U);
        break;
    case 0x0062U:
        ch1_.nrx1 = event.value;
        ch1_.lengthCounter = static_cast<u8>(64 - (event.value & 0x3FU));
        break;
    case 0x0063U:
        ch1_.nrx2 = event.value;
        ch1_.dacEnabled = (event.value & 0xF8U) != 0U;
        if (!ch1_.dacEnabled) {
            ch1_.enabled = false;
        }
        break;
    case 0x0064U:
        ch1_.nrx3 = event.value;
        break;
    case 0x0065U:
        ch1_.nrx4 = event.value;
        ch1_.lengthEnabled = (event.value & 0x40U) != 0U;
        if ((event.value & 0x80U) != 0U) {
            triggerSquare(ch1_, true);
        }
        break;
    case 0x0068U:
        ch2_.nrx1 = event.value;
        ch2_.lengthCounter = static_cast<u8>(64 - (event.value & 0x3FU));
        break;
    case 0x0069U:
        ch2_.nrx2 = event.value;
        ch2_.dacEnabled = (event.value & 0xF8U) != 0U;
        if (!ch2_.dacEnabled) {
            ch2_.enabled = false;
        }
        break;
    case 0x006CU:
        ch2_.nrx3 = event.value;
        break;
    case 0x006DU:
        ch2_.nrx4 = event.value;
        ch2_.lengthEnabled = (event.value & 0x40U) != 0U;
        if ((event.value & 0x80U) != 0U) {
            triggerSquare(ch2_, false);
        }
        break;
    case 0x0070U:
        ch3_.cntL = event.value;
        ch3_.dacEnabled = (event.value & 0x80U) != 0U;
        if (!ch3_.dacEnabled) {
            ch3_.enabled = false;
        }
        break;
    case 0x0072U:
        ch3_.nr31 = event.value;
        ch3_.lengthCounter = static_cast<u16>(256 - event.value);
        break;
    case 0x0073U:
        ch3_.nr32 = event.value;
        break;
    case 0x0074U:
        ch3_.nr33 = event.value;
        break;
    case 0x0075U:
        ch3_.nr34 = event.value;
        ch3_.lengthEnabled = (event.value & 0x40U) != 0U;
        if ((event.value & 0x80U) != 0U) {
            triggerWave();
        }
        break;
    case 0x0078U:
        ch4_.nr41 = event.value;
        ch4_.lengthCounter = static_cast<u8>(64 - (event.value & 0x3FU));
        break;
    case 0x0079U:
        ch4_.nr42 = event.value;
        ch4_.dacEnabled = (event.value & 0xF8U) != 0U;
        if (!ch4_.dacEnabled) {
            ch4_.enabled = false;
        }
        break;
    case 0x007CU:
        ch4_.nr43 = event.value;
        break;
    case 0x007DU:
        ch4_.nr44 = event.value;
        ch4_.lengthEnabled = (event.value & 0x40U) != 0U;
        if ((event.value & 0x80U) != 0U) {
            triggerNoise();
        }
        break;
    default:
        if (event.ioOffset >= 0x0090U && event.ioOffset <= 0x009FU) {
            waveRam_[static_cast<std::size_t>(event.ioOffset - 0x0090U)] = event.value;
        }
        break;
    }
}

void Apu::tick(int cpuCycles, Memory& memory) {
    if (cpuCycles <= 0) {
        lastTickStats_ = TickStats{};
        memory.clearAudioFifoEvents();
        memory.clearAudioRegisterWriteEvents();
        return;
    }

    const u16 soundCntL = memory.readIo16(kSoundCntL);
    const u16 soundCntH = memory.readIo16(kSoundCntH);
    const u16 soundCntX = memory.readIo16(kSoundCntX);
    const u16 soundBias = memory.readIo16(kSoundBias);
    const bool masterEnable = (soundCntX & 0x0080U) != 0U;
    lastTickStats_ = TickStats{};
    lastTickStats_.soundCntL = soundCntL;
    lastTickStats_.soundCntH = soundCntH;
    lastTickStats_.soundCntX = soundCntX;
    lastTickStats_.soundBias = soundBias;
    lastTickStats_.masterEnable = masterEnable;

    const int scaleA = (soundCntH & 0x0004U) ? kFullVolumeScale : kHalfVolumeScale;
    const bool fifoARight = (soundCntH & 0x0100U) != 0U;
    const bool fifoALeft = (soundCntH & 0x0200U) != 0U;
    const int scaleB = (soundCntH & 0x0008U) ? kFullVolumeScale : kHalfVolumeScale;
    const bool fifoBRight = (soundCntH & 0x1000U) != 0U;
    const bool fifoBLeft = (soundCntH & 0x2000U) != 0U;
    const int bias = static_cast<int>(soundBias & kBiasMask);
    const bool disablePsg = gbaAudioDisablePsg();
    const bool disableFifo = gbaAudioDisableFifo();
    const float outputScale = gbaAudioOutputScale();
    const float highPassA = gbaAudioHighPassCoeff();
    const float lowPassA = gbaAudioLowPassCoeff();

    const auto applyBias = [bias](int sample) {
        int biased = sample + bias;
        if (biased < 0) {
            biased = 0;
        } else if (biased > kBiasMask) {
            biased = kBiasMask;
        }
        return biased - bias;
    };

    const auto flushSegment = [&](std::uint32_t segmentCycles) {
        if (segmentCycles == 0U) {
            return;
        }

        const int totalCycles = cycleAccumulator_ + static_cast<int>(segmentCycles);
        const int sampleCount = totalCycles / kCyclesPerSample;
        cycleAccumulator_ = totalCycles % kCyclesPerSample;
        if (sampleCount <= 0) {
            return;
        }

        const bool psgAnyEnabled = !disablePsg && (ch1_.enabled || ch2_.enabled || ch3_.enabled || ch4_.enabled);
        const bool psgAudible = masterEnable && psgAnyEnabled && (soundCntL & 0xFF00U) != 0U;

        if (!psgAudible) {
            if (masterEnable && psgAnyEnabled) {
                stepPsg(sampleCount * PsgCyclesPerSample);
            }

            int left = 0;
            int right = 0;
            if (masterEnable && !disableFifo) {
                const int scaledA = currentSampleA_ * scaleA;
                const int scaledB = currentSampleB_ * scaleB;
                if (fifoALeft) {
                    left += scaledA;
                }
                if (fifoARight) {
                    right += scaledA;
                }
                if (fifoBLeft) {
                    left += scaledB;
                }
                if (fifoBRight) {
                    right += scaledB;
                }
            }

            appendRepeatedStereoSamples(
                sampleBuffer_,
                filterHardwareDacSample(applyBias(left), highPassA, lowPassA, outputScale, hpPrevInL_, hpPrevOutL_, lpPrevOutL_),
                filterHardwareDacSample(applyBias(right), highPassA, lowPassA, outputScale, hpPrevInR_, hpPrevOutR_, lpPrevOutR_),
                sampleCount);
            return;
        }

        sampleBuffer_.reserve(sampleBuffer_.size() + static_cast<std::size_t>(sampleCount) * 2U);
        for (int index = 0; index < sampleCount; ++index) {
            if (masterEnable) {
                stepPsg(PsgCyclesPerSample);
            }

            int left = 0;
            int right = 0;
            if (masterEnable) {
                if (!disableFifo) {
                    const int scaledA = currentSampleA_ * scaleA;
                    const int scaledB = currentSampleB_ * scaleB;
                    if (fifoALeft) {
                        left += scaledA;
                    }
                    if (fifoARight) {
                        right += scaledA;
                    }
                    if (fifoBLeft) {
                        left += scaledB;
                    }
                    if (fifoBRight) {
                        right += scaledB;
                    }
                }
                if (!disablePsg) {
                    left += mixPsgLeft(soundCntL, soundCntH);
                    right += mixPsgRight(soundCntL, soundCntH);
                }
            }

            sampleBuffer_.push_back(filterHardwareDacSample(
                applyBias(left),
                highPassA,
                lowPassA,
                outputScale,
                hpPrevInL_,
                hpPrevOutL_,
                lpPrevOutL_));
            sampleBuffer_.push_back(filterHardwareDacSample(
                applyBias(right),
                highPassA,
                lowPassA,
                outputScale,
                hpPrevInR_,
                hpPrevOutR_,
                lpPrevOutR_));
        }
    };

    std::size_t fifoEventIndex = 0;
    std::size_t regEventIndex = 0;
    std::uint32_t elapsedCycles = 0;
    const auto& fifoEvents = memory.audioFifoEvents();
    const auto& regEvents = memory.audioRegisterWriteEvents();
    const std::size_t generatedStart = sampleBuffer_.size();
    lastTickStats_.fifoEventCount = static_cast<std::uint32_t>(fifoEvents.size());
    lastTickStats_.regEventCount = static_cast<std::uint32_t>(regEvents.size());

    while (fifoEventIndex < fifoEvents.size() || regEventIndex < regEvents.size()) {
        const std::uint32_t nextFifoOffset = fifoEventIndex < fifoEvents.size()
            ? std::min(fifoEvents[fifoEventIndex].cycleOffset, static_cast<std::uint32_t>(cpuCycles))
            : static_cast<std::uint32_t>(cpuCycles);
        const std::uint32_t nextRegOffset = regEventIndex < regEvents.size()
            ? std::min(regEvents[regEventIndex].cycleOffset, static_cast<std::uint32_t>(cpuCycles))
            : static_cast<std::uint32_t>(cpuCycles);
        const std::uint32_t nextOffset = std::min(nextFifoOffset, nextRegOffset);

        if (nextOffset > elapsedCycles) {
            flushSegment(nextOffset - elapsedCycles);
            elapsedCycles = nextOffset;
        }

        while (regEventIndex < regEvents.size()) {
            const auto& event = regEvents[regEventIndex];
            if (std::min(event.cycleOffset, static_cast<std::uint32_t>(cpuCycles)) != nextOffset) {
                break;
            }
            applyAudioRegisterWrite(event);
            ++regEventIndex;
        }

        while (fifoEventIndex < fifoEvents.size()) {
            const auto& event = fifoEvents[fifoEventIndex];
            if (std::min(event.cycleOffset, static_cast<std::uint32_t>(cpuCycles)) != nextOffset) {
                break;
            }
            if (event.fifoIndex == 0U) {
                currentSampleA_ = static_cast<int>(static_cast<int8_t>(event.sample));
            } else if (event.fifoIndex == 1U) {
                currentSampleB_ = static_cast<int>(static_cast<int8_t>(event.sample));
            }
            ++fifoEventIndex;
        }
    }

    if (static_cast<std::uint32_t>(cpuCycles) > elapsedCycles) {
        flushSegment(static_cast<std::uint32_t>(cpuCycles) - elapsedCycles);
    }

    const std::size_t generatedSamples = sampleBuffer_.size() - generatedStart;
    lastTickStats_.generatedFrames = static_cast<std::uint32_t>(generatedSamples / 2U);
    for (std::size_t sampleIndex = generatedStart; sampleIndex + 1U < sampleBuffer_.size(); sampleIndex += 2U) {
        const auto left = static_cast<std::int16_t>(std::abs(static_cast<int>(sampleBuffer_[sampleIndex])));
        const auto right = static_cast<std::int16_t>(std::abs(static_cast<int>(sampleBuffer_[sampleIndex + 1U])));
        if (left > lastTickStats_.peakLeft) {
            lastTickStats_.peakLeft = left;
        }
        if (right > lastTickStats_.peakRight) {
            lastTickStats_.peakRight = right;
        }
    }
    if (gbaAudioStateLoggingEnabled()) {
        static std::uint64_t tickCounter = 0;
        ++tickCounter;
        if ((tickCounter % static_cast<std::uint64_t>(gbaAudioStateLogEvery())) == 0U) {
            std::cerr << "[GBA][AUDIO] tick=" << tickCounter
                      << " master=" << static_cast<unsigned>(masterEnable)
                      << " soundCntL=0x" << std::hex << soundCntL
                      << " soundCntH=0x" << soundCntH
                      << " soundCntX=0x" << soundCntX
                      << " soundBias=0x" << soundBias
                      << std::dec
                      << " fifo_events=" << lastTickStats_.fifoEventCount
                      << " reg_events=" << lastTickStats_.regEventCount
                      << " frames=" << lastTickStats_.generatedFrames
                      << " peakL=" << lastTickStats_.peakLeft
                      << " peakR=" << lastTickStats_.peakRight
                      << " fifoA=" << currentSampleA_
                      << " fifoB=" << currentSampleB_
                      << "\n";
        }
    }

    currentSampleA_ = static_cast<int>(static_cast<int8_t>(memory.audioFifoLastSample(0)));
    currentSampleB_ = static_cast<int>(static_cast<int8_t>(memory.audioFifoLastSample(1)));
    memory.clearAudioFifoEvents();
    memory.clearAudioRegisterWriteEvents();
}

std::vector<int16_t> Apu::takeSamples() {
    std::vector<int16_t> out;
    out.swap(sampleBuffer_);
    sampleBuffer_.reserve(out.capacity());
    return out;
}

} // namespace gb::gba