#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gb/core/types.hpp"

namespace gb {

class APU {
public:
    static constexpr int SampleRate = 48000;
    struct SquareChannelState {
        u8 nrx1 = 0;
        u8 nrx2 = 0;
        u8 nrx3 = 0;
        u8 nrx4 = 0;
        bool enabled = false;
        bool dacEnabled = false;
        u8 dutyStep = 0;
        u32 timerCycles = 0;
        u8 lengthCounter = 0;
        bool lengthEnabled = false;
        u8 currentVolume = 0;
        u8 envelopePeriod = 0;
        u8 envelopeTimer = 0;
        bool envelopeIncrease = false;
    };
    struct WaveChannelState {
        u8 nr30 = 0;
        u8 nr31 = 0;
        u8 nr32 = 0;
        u8 nr33 = 0;
        u8 nr34 = 0;
        bool enabled = false;
        bool dacEnabled = false;
        u8 waveStep = 0;
        u32 timerCycles = 0;
        u16 lengthCounter = 0;
        bool lengthEnabled = false;
    };
    struct State {
        SquareChannelState ch1{};
        SquareChannelState ch2{};
        WaveChannelState ch3{};
        u8 nr10 = 0x80;
        bool sweepEnabled = false;
        u8 sweepPeriod = 0;
        u8 sweepTimer = 0;
        bool sweepNegate = false;
        u8 sweepShift = 0;
        u16 sweepShadowFreq = 0;
        std::array<u8, 16> waveRam{};
        u8 nr50 = 0x77;
        u8 nr51 = 0xF3;
        u8 nr52 = 0xF1;
        u32 frameSeqCycles = 0;
        u8 frameSeqStep = 0;
        double sampleCyclesAccum = 0.0;
        float hpPrevInL = 0.0f;
        float hpPrevOutL = 0.0f;
        float hpPrevInR = 0.0f;
        float hpPrevOutR = 0.0f;
    };

    void tick(u32 cycles);

    u8 read(u16 address) const;
    void write(u16 address, u8 value);

    std::vector<int16_t> takeSamples();
    [[nodiscard]] State state() const;
    void loadState(const State& state);

private:
    struct SquareChannel {
        u8 nrx1 = 0;
        u8 nrx2 = 0;
        u8 nrx3 = 0;
        u8 nrx4 = 0;

        bool enabled = false;
        bool dacEnabled = false;

        u8 dutyStep = 0;
        u32 timerCycles = 0;

        u8 lengthCounter = 0;
        bool lengthEnabled = false;

        u8 currentVolume = 0;
        u8 envelopePeriod = 0;
        u8 envelopeTimer = 0;
        bool envelopeIncrease = false;
    };

    struct WaveChannel {
        u8 nr30 = 0;
        u8 nr31 = 0;
        u8 nr32 = 0;
        u8 nr33 = 0;
        u8 nr34 = 0;

        bool enabled = false;
        bool dacEnabled = false;

        u8 waveStep = 0;
        u32 timerCycles = 0;

        u16 lengthCounter = 0;
        bool lengthEnabled = false;
    };

    [[nodiscard]] bool masterEnabled() const;

    [[nodiscard]] u16 squareFrequency(const SquareChannel& ch) const;
    [[nodiscard]] u16 waveFrequency() const;

    void triggerSquare(SquareChannel& ch, bool withSweep);
    void triggerWave();

    void clockSquare(SquareChannel& ch);
    int sampleSquare(const SquareChannel& ch) const;

    void clockWave();
    int sampleWave() const;

    void frameSequencerStep();
    void clockLength();
    void clockEnvelope();
    void clockSweep();
    bool applySweep(bool writeBack);

    void resetAll();

    static constexpr u32 CpuHz = 4194304;
    static constexpr u32 FrameSequencerPeriod = 8192; // 512 Hz

    SquareChannel ch1_{};
    SquareChannel ch2_{};
    WaveChannel ch3_{};

    u8 nr10_ = 0x80;

    bool sweepEnabled_ = false;
    u8 sweepPeriod_ = 0;
    u8 sweepTimer_ = 0;
    bool sweepNegate_ = false;
    u8 sweepShift_ = 0;
    u16 sweepShadowFreq_ = 0;

    std::array<u8, 16> waveRam_{};

    u8 nr50_ = 0x77;
    u8 nr51_ = 0xF3;
    u8 nr52_ = 0xF1;

    u32 frameSeqCycles_ = 0;
    u8 frameSeqStep_ = 0;

    double sampleCyclesAccum_ = 0.0;
    std::vector<int16_t> samples_{};

    float hpPrevInL_ = 0.0f;
    float hpPrevOutL_ = 0.0f;
    float hpPrevInR_ = 0.0f;
    float hpPrevOutR_ = 0.0f;
};

} // namespace gb
