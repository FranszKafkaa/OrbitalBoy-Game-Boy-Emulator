#include "gb/core/apu.hpp"

#include <algorithm>

namespace gb {

namespace {
constexpr std::array<std::array<int, 8>, 4> DutyPatterns = {{
    {{0, 0, 0, 0, 0, 0, 0, 1}},
    {{1, 0, 0, 0, 0, 0, 0, 1}},
    {{1, 0, 0, 0, 0, 1, 1, 1}},
    {{0, 1, 1, 1, 1, 1, 1, 0}},
}};

u8 envPeriodOr8(u8 p) {
    return p == 0 ? 8 : p;
}
} // namespace

bool APU::masterEnabled() const {
    return (nr52_ & 0x80) != 0;
}

u16 APU::squareFrequency(const SquareChannel& ch) const {
    return static_cast<u16>(((ch.nrx4 & 0x07) << 8) | ch.nrx3);
}

u16 APU::waveFrequency() const {
    return static_cast<u16>(((ch3_.nr34 & 0x07) << 8) | ch3_.nr33);
}

void APU::tick(u32 cycles) {
    if (!masterEnabled()) {
        return;
    }

    const double cyclesPerSample = static_cast<double>(CpuHz) / static_cast<double>(SampleRate);

    for (u32 i = 0; i < cycles; ++i) {
        clockSquare(ch1_);
        clockSquare(ch2_);
        clockWave();
        clockNoise();

        ++frameSeqCycles_;
        if (frameSeqCycles_ >= FrameSequencerPeriod) {
            frameSeqCycles_ -= FrameSequencerPeriod;
            frameSequencerStep();
        }

        sampleCyclesAccum_ += 1.0;
        if (sampleCyclesAccum_ < cyclesPerSample) {
            continue;
        }
        sampleCyclesAccum_ -= cyclesPerSample;

        const float s1 = static_cast<float>(sampleSquare(ch1_)) / 15.0f;
        const float s2 = static_cast<float>(sampleSquare(ch2_)) / 15.0f;
        const float s3 = static_cast<float>(sampleWave()) / 15.0f;
        const float s4 = static_cast<float>(sampleNoise()) / 15.0f;

        float left = 0.0f;
        float right = 0.0f;

        int leftCount = 0;
        int rightCount = 0;

        if (nr51_ & 0x10) {
            left += s1;
            ++leftCount;
        }
        if (nr51_ & 0x20) {
            left += s2;
            ++leftCount;
        }
        if (nr51_ & 0x40) {
            left += s3;
            ++leftCount;
        }
        if (nr51_ & 0x80) {
            left += s4;
            ++leftCount;
        }
        if (nr51_ & 0x01) {
            right += s1;
            ++rightCount;
        }
        if (nr51_ & 0x02) {
            right += s2;
            ++rightCount;
        }
        if (nr51_ & 0x04) {
            right += s3;
            ++rightCount;
        }
        if (nr51_ & 0x08) {
            right += s4;
            ++rightCount;
        }

        if (leftCount > 0) {
            left /= static_cast<float>(leftCount);
        }
        if (rightCount > 0) {
            right /= static_cast<float>(rightCount);
        }

        const float lvol = static_cast<float>(((nr50_ >> 4) & 0x07) + 1) / 8.0f;
        const float rvol = static_cast<float>((nr50_ & 0x07) + 1) / 8.0f;
        left *= lvol;
        right *= rvol;

        constexpr float hpA = 0.995f;
        const float hpL = (left - hpPrevInL_) + hpA * hpPrevOutL_;
        const float hpR = (right - hpPrevInR_) + hpA * hpPrevOutR_;
        hpPrevInL_ = left;
        hpPrevOutL_ = hpL;
        hpPrevInR_ = right;
        hpPrevOutR_ = hpR;

        constexpr float masterGain = 0.25f;
        const float outL = std::clamp(hpL * masterGain, -1.0f, 1.0f);
        const float outR = std::clamp(hpR * masterGain, -1.0f, 1.0f);

        samples_.push_back(static_cast<int16_t>(outL * 32767.0f));
        samples_.push_back(static_cast<int16_t>(outR * 32767.0f));
    }
}

u8 APU::read(u16 address) const {
    switch (address) {
    case 0xFF10: return static_cast<u8>(nr10_ | 0x80);
    case 0xFF11: return static_cast<u8>(ch1_.nrx1 | 0x3F);
    case 0xFF12: return ch1_.nrx2;
    case 0xFF13: return 0xFF;
    case 0xFF14: return static_cast<u8>(ch1_.nrx4 | 0xBF);

    case 0xFF16: return static_cast<u8>(ch2_.nrx1 | 0x3F);
    case 0xFF17: return ch2_.nrx2;
    case 0xFF18: return 0xFF;
    case 0xFF19: return static_cast<u8>(ch2_.nrx4 | 0xBF);

    case 0xFF1A: return static_cast<u8>(ch3_.nr30 | 0x7F);
    case 0xFF1B: return 0xFF;
    case 0xFF1C: return static_cast<u8>(ch3_.nr32 | 0x9F);
    case 0xFF1D: return 0xFF;
    case 0xFF1E: return static_cast<u8>(ch3_.nr34 | 0xBF);
    case 0xFF20: return 0xFF;
    case 0xFF21: return ch4_.nr42;
    case 0xFF22: return ch4_.nr43;
    case 0xFF23: return static_cast<u8>(ch4_.nr44 | 0xBF);

    case 0xFF24: return nr50_;
    case 0xFF25: return nr51_;
    case 0xFF26: {
        u8 status = static_cast<u8>(nr52_ & 0xF0);
        if (ch1_.enabled) status |= 0x01;
        if (ch2_.enabled) status |= 0x02;
        if (ch3_.enabled) status |= 0x04;
        if (ch4_.enabled) status |= 0x08;
        return status;
    }

    default:
        if (address >= 0xFF30 && address <= 0xFF3F) {
            return waveRam_[address - 0xFF30];
        }
        return 0xFF;
    }
}

void APU::write(u16 address, u8 value) {
    if (address == 0xFF26) {
        nr52_ = static_cast<u8>((nr52_ & 0x7F) | (value & 0x80));
        if (!masterEnabled()) {
            resetAll();
        }
        return;
    }

    if (!masterEnabled()) {
        if (address >= 0xFF30 && address <= 0xFF3F) {
            waveRam_[address - 0xFF30] = value;
        }
        return;
    }

    switch (address) {
    case 0xFF10:
        nr10_ = static_cast<u8>(value | 0x80);
        sweepPeriod_ = static_cast<u8>((value >> 4) & 0x07);
        sweepNegate_ = (value & 0x08) != 0;
        sweepShift_ = static_cast<u8>(value & 0x07);
        break;

    case 0xFF11:
        ch1_.nrx1 = value;
        ch1_.lengthCounter = static_cast<u8>(64 - (value & 0x3F));
        break;
    case 0xFF12:
        ch1_.nrx2 = value;
        ch1_.dacEnabled = (value & 0xF8) != 0;
        if (!ch1_.dacEnabled) {
            ch1_.enabled = false;
        }
        break;
    case 0xFF13:
        ch1_.nrx3 = value;
        break;
    case 0xFF14:
        ch1_.nrx4 = value;
        ch1_.lengthEnabled = (value & 0x40) != 0;
        if (value & 0x80) {
            triggerSquare(ch1_, true);
        }
        break;

    case 0xFF16:
        ch2_.nrx1 = value;
        ch2_.lengthCounter = static_cast<u8>(64 - (value & 0x3F));
        break;
    case 0xFF17:
        ch2_.nrx2 = value;
        ch2_.dacEnabled = (value & 0xF8) != 0;
        if (!ch2_.dacEnabled) {
            ch2_.enabled = false;
        }
        break;
    case 0xFF18:
        ch2_.nrx3 = value;
        break;
    case 0xFF19:
        ch2_.nrx4 = value;
        ch2_.lengthEnabled = (value & 0x40) != 0;
        if (value & 0x80) {
            triggerSquare(ch2_, false);
        }
        break;

    case 0xFF1A:
        ch3_.nr30 = value;
        ch3_.dacEnabled = (value & 0x80) != 0;
        if (!ch3_.dacEnabled) {
            ch3_.enabled = false;
        }
        break;
    case 0xFF1B:
        ch3_.nr31 = value;
        ch3_.lengthCounter = static_cast<u16>(256 - value);
        break;
    case 0xFF1C:
        ch3_.nr32 = value;
        break;
    case 0xFF1D:
        ch3_.nr33 = value;
        break;
    case 0xFF1E:
        ch3_.nr34 = value;
        ch3_.lengthEnabled = (value & 0x40) != 0;
        if (value & 0x80) {
            triggerWave();
        }
        break;
    case 0xFF20:
        ch4_.nr41 = value;
        ch4_.lengthCounter = static_cast<u8>(64 - (value & 0x3F));
        break;
    case 0xFF21:
        ch4_.nr42 = value;
        ch4_.dacEnabled = (value & 0xF8) != 0;
        if (!ch4_.dacEnabled) {
            ch4_.enabled = false;
        }
        break;
    case 0xFF22:
        ch4_.nr43 = value;
        break;
    case 0xFF23:
        ch4_.nr44 = value;
        ch4_.lengthEnabled = (value & 0x40) != 0;
        if (value & 0x80) {
            triggerNoise();
        }
        break;

    case 0xFF24: nr50_ = value; break;
    case 0xFF25: nr51_ = value; break;

    default:
        if (address >= 0xFF30 && address <= 0xFF3F) {
            waveRam_[address - 0xFF30] = value;
        }
        break;
    }
}

std::vector<int16_t> APU::takeSamples() {
    std::vector<int16_t> out;
    out.swap(samples_);
    return out;
}

APU::State APU::state() const {
    State s{};
    s.ch1 = SquareChannelState{
        ch1_.nrx1, ch1_.nrx2, ch1_.nrx3, ch1_.nrx4,
        ch1_.enabled, ch1_.dacEnabled, ch1_.dutyStep, ch1_.timerCycles,
        ch1_.lengthCounter, ch1_.lengthEnabled, ch1_.currentVolume,
        ch1_.envelopePeriod, ch1_.envelopeTimer, ch1_.envelopeIncrease,
    };
    s.ch2 = SquareChannelState{
        ch2_.nrx1, ch2_.nrx2, ch2_.nrx3, ch2_.nrx4,
        ch2_.enabled, ch2_.dacEnabled, ch2_.dutyStep, ch2_.timerCycles,
        ch2_.lengthCounter, ch2_.lengthEnabled, ch2_.currentVolume,
        ch2_.envelopePeriod, ch2_.envelopeTimer, ch2_.envelopeIncrease,
    };
    s.ch3 = WaveChannelState{
        ch3_.nr30, ch3_.nr31, ch3_.nr32, ch3_.nr33, ch3_.nr34,
        ch3_.enabled, ch3_.dacEnabled, ch3_.waveStep, ch3_.timerCycles,
        ch3_.lengthCounter, ch3_.lengthEnabled,
    };
    s.ch4 = NoiseChannelState{
        ch4_.nr41, ch4_.nr42, ch4_.nr43, ch4_.nr44,
        ch4_.enabled, ch4_.dacEnabled, ch4_.lfsr, ch4_.timerCycles,
        ch4_.lengthCounter, ch4_.lengthEnabled, ch4_.currentVolume,
        ch4_.envelopePeriod, ch4_.envelopeTimer, ch4_.envelopeIncrease,
    };
    s.nr10 = nr10_;
    s.sweepEnabled = sweepEnabled_;
    s.sweepPeriod = sweepPeriod_;
    s.sweepTimer = sweepTimer_;
    s.sweepNegate = sweepNegate_;
    s.sweepShift = sweepShift_;
    s.sweepShadowFreq = sweepShadowFreq_;
    s.waveRam = waveRam_;
    s.nr50 = nr50_;
    s.nr51 = nr51_;
    s.nr52 = nr52_;
    s.frameSeqCycles = frameSeqCycles_;
    s.frameSeqStep = frameSeqStep_;
    s.sampleCyclesAccum = sampleCyclesAccum_;
    s.hpPrevInL = hpPrevInL_;
    s.hpPrevOutL = hpPrevOutL_;
    s.hpPrevInR = hpPrevInR_;
    s.hpPrevOutR = hpPrevOutR_;
    return s;
}

void APU::loadState(const State& s) {
    ch1_.nrx1 = s.ch1.nrx1;
    ch1_.nrx2 = s.ch1.nrx2;
    ch1_.nrx3 = s.ch1.nrx3;
    ch1_.nrx4 = s.ch1.nrx4;
    ch1_.enabled = s.ch1.enabled;
    ch1_.dacEnabled = s.ch1.dacEnabled;
    ch1_.dutyStep = s.ch1.dutyStep;
    ch1_.timerCycles = s.ch1.timerCycles;
    ch1_.lengthCounter = s.ch1.lengthCounter;
    ch1_.lengthEnabled = s.ch1.lengthEnabled;
    ch1_.currentVolume = s.ch1.currentVolume;
    ch1_.envelopePeriod = s.ch1.envelopePeriod;
    ch1_.envelopeTimer = s.ch1.envelopeTimer;
    ch1_.envelopeIncrease = s.ch1.envelopeIncrease;

    ch2_.nrx1 = s.ch2.nrx1;
    ch2_.nrx2 = s.ch2.nrx2;
    ch2_.nrx3 = s.ch2.nrx3;
    ch2_.nrx4 = s.ch2.nrx4;
    ch2_.enabled = s.ch2.enabled;
    ch2_.dacEnabled = s.ch2.dacEnabled;
    ch2_.dutyStep = s.ch2.dutyStep;
    ch2_.timerCycles = s.ch2.timerCycles;
    ch2_.lengthCounter = s.ch2.lengthCounter;
    ch2_.lengthEnabled = s.ch2.lengthEnabled;
    ch2_.currentVolume = s.ch2.currentVolume;
    ch2_.envelopePeriod = s.ch2.envelopePeriod;
    ch2_.envelopeTimer = s.ch2.envelopeTimer;
    ch2_.envelopeIncrease = s.ch2.envelopeIncrease;

    ch3_.nr30 = s.ch3.nr30;
    ch3_.nr31 = s.ch3.nr31;
    ch3_.nr32 = s.ch3.nr32;
    ch3_.nr33 = s.ch3.nr33;
    ch3_.nr34 = s.ch3.nr34;
    ch3_.enabled = s.ch3.enabled;
    ch3_.dacEnabled = s.ch3.dacEnabled;
    ch3_.waveStep = s.ch3.waveStep;
    ch3_.timerCycles = s.ch3.timerCycles;
    ch3_.lengthCounter = s.ch3.lengthCounter;
    ch3_.lengthEnabled = s.ch3.lengthEnabled;

    ch4_.nr41 = s.ch4.nr41;
    ch4_.nr42 = s.ch4.nr42;
    ch4_.nr43 = s.ch4.nr43;
    ch4_.nr44 = s.ch4.nr44;
    ch4_.enabled = s.ch4.enabled;
    ch4_.dacEnabled = s.ch4.dacEnabled;
    ch4_.lfsr = s.ch4.lfsr;
    if (ch4_.lfsr == 0) {
        ch4_.lfsr = 0x7FFF;
    }
    ch4_.timerCycles = s.ch4.timerCycles;
    ch4_.lengthCounter = s.ch4.lengthCounter;
    ch4_.lengthEnabled = s.ch4.lengthEnabled;
    ch4_.currentVolume = s.ch4.currentVolume;
    ch4_.envelopePeriod = s.ch4.envelopePeriod;
    ch4_.envelopeTimer = s.ch4.envelopeTimer;
    ch4_.envelopeIncrease = s.ch4.envelopeIncrease;

    nr10_ = s.nr10;
    sweepEnabled_ = s.sweepEnabled;
    sweepPeriod_ = s.sweepPeriod;
    sweepTimer_ = s.sweepTimer;
    sweepNegate_ = s.sweepNegate;
    sweepShift_ = s.sweepShift;
    sweepShadowFreq_ = s.sweepShadowFreq;
    waveRam_ = s.waveRam;
    nr50_ = s.nr50;
    nr51_ = s.nr51;
    nr52_ = s.nr52;
    frameSeqCycles_ = s.frameSeqCycles;
    frameSeqStep_ = s.frameSeqStep;
    sampleCyclesAccum_ = s.sampleCyclesAccum;
    hpPrevInL_ = s.hpPrevInL;
    hpPrevOutL_ = s.hpPrevOutL;
    hpPrevInR_ = s.hpPrevInR;
    hpPrevOutR_ = s.hpPrevOutR;
    samples_.clear();
}

void APU::triggerSquare(SquareChannel& ch, bool withSweep) {
    ch.enabled = ch.dacEnabled;
    if (ch.lengthCounter == 0) {
        ch.lengthCounter = 64;
    }
    ch.timerCycles = 0;
    ch.dutyStep = 0;

    ch.currentVolume = static_cast<u8>((ch.nrx2 >> 4) & 0x0F);
    ch.envelopeIncrease = (ch.nrx2 & 0x08) != 0;
    ch.envelopePeriod = static_cast<u8>(ch.nrx2 & 0x07);
    ch.envelopeTimer = envPeriodOr8(ch.envelopePeriod);

    if (withSweep) {
        sweepShadowFreq_ = squareFrequency(ch1_);
        sweepTimer_ = sweepPeriod_ == 0 ? 8 : sweepPeriod_;
        sweepEnabled_ = (sweepPeriod_ != 0) || (sweepShift_ != 0);
        if (sweepShift_ != 0) {
            applySweep(false);
        }
    }
}

void APU::triggerWave() {
    ch3_.enabled = ch3_.dacEnabled;
    if (ch3_.lengthCounter == 0) {
        ch3_.lengthCounter = 256;
    }
    ch3_.waveStep = 0;
    ch3_.timerCycles = 0;
}

void APU::triggerNoise() {
    ch4_.enabled = ch4_.dacEnabled;
    if (ch4_.lengthCounter == 0) {
        ch4_.lengthCounter = 64;
    }
    ch4_.timerCycles = 0;
    ch4_.lfsr = 0x7FFF;
    ch4_.currentVolume = static_cast<u8>((ch4_.nr42 >> 4) & 0x0F);
    ch4_.envelopeIncrease = (ch4_.nr42 & 0x08) != 0;
    ch4_.envelopePeriod = static_cast<u8>(ch4_.nr42 & 0x07);
    ch4_.envelopeTimer = envPeriodOr8(ch4_.envelopePeriod);
}

void APU::clockSquare(SquareChannel& ch) {
    if (!ch.enabled) {
        return;
    }

    const u16 freq = squareFrequency(ch);
    const u32 period = static_cast<u32>((2048 - freq) * 4);
    if (period == 0) {
        return;
    }

    ++ch.timerCycles;
    if (ch.timerCycles >= period) {
        ch.timerCycles = 0;
        ch.dutyStep = static_cast<u8>((ch.dutyStep + 1) & 7);
    }
}

int APU::sampleSquare(const SquareChannel& ch) const {
    if (!ch.enabled || !ch.dacEnabled || ch.currentVolume == 0) {
        return 0;
    }

    const int duty = (ch.nrx1 >> 6) & 0x03;
    const int wave = DutyPatterns[duty][ch.dutyStep] ? 1 : -1;
    return wave * static_cast<int>(ch.currentVolume);
}

void APU::clockWave() {
    if (!ch3_.enabled || !ch3_.dacEnabled) {
        return;
    }

    const u16 freq = waveFrequency();
    const u32 period = static_cast<u32>((2048 - freq) * 2);
    if (period == 0) {
        return;
    }

    ++ch3_.timerCycles;
    if (ch3_.timerCycles >= period) {
        ch3_.timerCycles = 0;
        ch3_.waveStep = static_cast<u8>((ch3_.waveStep + 1) & 31);
    }
}

int APU::sampleWave() const {
    if (!ch3_.enabled || !ch3_.dacEnabled) {
        return 0;
    }

    const int outputLevel = (ch3_.nr32 >> 5) & 0x03;
    if (outputLevel == 0) {
        return 0;
    }

    const u8 sampleByte = waveRam_[static_cast<std::size_t>(ch3_.waveStep / 2)];
    const u8 nibble = (ch3_.waveStep & 1) ? static_cast<u8>(sampleByte & 0x0F)
                                          : static_cast<u8>((sampleByte >> 4) & 0x0F);

    int sample = static_cast<int>(nibble) - 8;
    switch (outputLevel) {
    case 1: break;
    case 2: sample >>= 1; break;
    case 3: sample >>= 2; break;
    default: sample = 0; break;
    }

    return sample;
}

void APU::clockNoise() {
    if (!ch4_.enabled || !ch4_.dacEnabled) {
        return;
    }

    const u8 divisorCode = static_cast<u8>(ch4_.nr43 & 0x07);
    const u8 clockShift = static_cast<u8>((ch4_.nr43 >> 4) & 0x0F);
    const u32 divisor = divisorCode == 0 ? 8u : static_cast<u32>(divisorCode) * 16u;
    const u32 period = divisor << clockShift;
    if (period == 0) {
        return;
    }

    ++ch4_.timerCycles;
    if (ch4_.timerCycles < period) {
        return;
    }
    ch4_.timerCycles = 0;

    const u16 xorBit = static_cast<u16>((ch4_.lfsr & 0x01) ^ ((ch4_.lfsr >> 1) & 0x01));
    ch4_.lfsr = static_cast<u16>((ch4_.lfsr >> 1) | (xorBit << 14));
    if (ch4_.nr43 & 0x08) {
        ch4_.lfsr = static_cast<u16>((ch4_.lfsr & ~(1u << 6)) | (xorBit << 6));
    }
}

int APU::sampleNoise() const {
    if (!ch4_.enabled || !ch4_.dacEnabled || ch4_.currentVolume == 0) {
        return 0;
    }
    const int wave = (ch4_.lfsr & 0x01) == 0 ? 1 : -1;
    return wave * static_cast<int>(ch4_.currentVolume);
}

void APU::frameSequencerStep() {
    frameSeqStep_ = static_cast<u8>((frameSeqStep_ + 1) & 0x07);

    if ((frameSeqStep_ & 1) == 0) {
        clockLength();
    }

    if (frameSeqStep_ == 2 || frameSeqStep_ == 6) {
        clockSweep();
    }

    if (frameSeqStep_ == 7) {
        clockEnvelope();
    }
}

void APU::clockLength() {
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

void APU::clockEnvelope() {
    for (SquareChannel* ch : {&ch1_, &ch2_}) {
        if (!ch->enabled || ch->envelopePeriod == 0) {
            continue;
        }

        if (ch->envelopeTimer > 0) {
            --ch->envelopeTimer;
        }
        if (ch->envelopeTimer != 0) {
            continue;
        }

        ch->envelopeTimer = envPeriodOr8(ch->envelopePeriod);

        if (ch->envelopeIncrease) {
            if (ch->currentVolume < 15) {
                ++ch->currentVolume;
            }
        } else {
            if (ch->currentVolume > 0) {
                --ch->currentVolume;
            }
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
    } else {
        if (ch4_.currentVolume > 0) {
            --ch4_.currentVolume;
        }
    }
}

void APU::clockSweep() {
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

bool APU::applySweep(bool writeBack) {
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
        ch1_.nrx3 = static_cast<u8>(next & 0xFF);
        ch1_.nrx4 = static_cast<u8>((ch1_.nrx4 & 0xF8) | ((next >> 8) & 0x07));
    }

    return true;
}

void APU::resetAll() {
    ch1_ = {};
    ch2_ = {};
    ch3_ = {};
    ch4_ = {};

    nr10_ = 0x80;
    nr50_ = 0;
    nr51_ = 0;

    sweepEnabled_ = false;
    sweepPeriod_ = 0;
    sweepTimer_ = 0;
    sweepNegate_ = false;
    sweepShift_ = 0;
    sweepShadowFreq_ = 0;

    frameSeqCycles_ = 0;
    frameSeqStep_ = 0;

    sampleCyclesAccum_ = 0.0;
    samples_.clear();

    hpPrevInL_ = 0.0f;
    hpPrevOutL_ = 0.0f;
    hpPrevInR_ = 0.0f;
    hpPrevOutR_ = 0.0f;
}

} // namespace gb
