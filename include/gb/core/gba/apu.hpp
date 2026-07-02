#pragma once

#include <cstdint>
#include <array>
#include <vector>

#include "gb/core/gba/memory.hpp"
#include "gb/core/types.hpp"

namespace gb::gba {

/// GBA audio processing unit.
/// Mixes DMA FIFO A/B samples based on SOUNDCNT_H routing and volume,
/// and outputs GBA-style 32.768 kHz stereo audio by default.
class Apu {
public:
    struct TickStats {
        std::uint32_t fifoEventCount = 0;
        std::uint32_t regEventCount = 0;
        std::uint32_t generatedFrames = 0;
        std::int16_t peakLeft = 0;
        std::int16_t peakRight = 0;
        u16 soundCntL = 0;
        u16 soundCntH = 0;
        u16 soundCntX = 0;
        u16 soundBias = 0;
        bool masterEnable = false;
    };

    static constexpr int SampleRate = 32768;
    static constexpr int CpuFrequency = 16777216;

    /// Advance the APU by the given number of CPU cycles.
    void tick(int cpuCycles, Memory& memory);

    /// Drain and return accumulated stereo samples (interleaved L, R).
    std::vector<int16_t> takeSamples();

    [[nodiscard]] const TickStats& lastTickStats() const;

    void reset();

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
        u8 cntL = 0;
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

    struct NoiseChannel {
        u8 nr41 = 0;
        u8 nr42 = 0;
        u8 nr43 = 0;
        u8 nr44 = 0;
        bool enabled = false;
        bool dacEnabled = false;
        u16 lfsr = 0x7FFF;
        u32 timerCycles = 0;
        u8 lengthCounter = 0;
        bool lengthEnabled = false;
        u8 currentVolume = 0;
        u8 envelopePeriod = 0;
        u8 envelopeTimer = 0;
        bool envelopeIncrease = false;
    };

    void applyAudioRegisterWrite(const Memory::AudioRegisterWriteEvent& event);
    void stepPsg(int cycles);
    void clockLength();
    void clockEnvelope();
    void clockSweep();
    void frameSequencerStep();
    bool applySweep(bool writeBack);
    void triggerSquare(SquareChannel& channel, bool withSweep);
    void triggerWave();
    void triggerNoise();
    int sampleSquare(const SquareChannel& channel) const;
    int sampleWave() const;
    int sampleNoise() const;
    int mixPsgLeft(u16 soundCntL, u16 soundCntH) const;
    int mixPsgRight(u16 soundCntL, u16 soundCntH) const;

    static constexpr int PsgCpuDivider = 4;
    static constexpr int PsgCyclesPerSample = (CpuFrequency / SampleRate) / PsgCpuDivider;

    int cycleAccumulator_ = 0;
    std::vector<int16_t> sampleBuffer_;
    TickStats lastTickStats_{};

    int currentSampleA_ = 0;
    int currentSampleB_ = 0;
    u16 soundCntL_ = 0;
    u16 soundCntH_ = 0;
    u16 soundCntX_ = 0;
    u16 soundBias_ = 0x0200U;

    SquareChannel ch1_{};
    SquareChannel ch2_{};
    WaveChannel ch3_{};
    NoiseChannel ch4_{};
    u8 nr10_ = 0;
    bool sweepEnabled_ = false;
    u8 sweepPeriod_ = 0;
    u8 sweepTimer_ = 0;
    bool sweepNegate_ = false;
    u8 sweepShift_ = 0;
    u16 sweepShadowFreq_ = 0;
    std::array<u8, 16> waveRam_{};
    int psgFrameSeqCycles_ = 0;
    u8 psgFrameSeqStep_ = 0;
    float hpPrevInL_ = 0.0f;
    float hpPrevOutL_ = 0.0f;
    float hpPrevInR_ = 0.0f;
    float hpPrevOutR_ = 0.0f;
    float lpPrevOutL_ = 0.0f;
    float lpPrevOutR_ = 0.0f;
};

} // namespace gb::gba
