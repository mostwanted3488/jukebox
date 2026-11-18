#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Instruments.h"
#include <atomic>

namespace jb
{
// One playing note: the disc instrument + the ATTACK/RELEASE envelope around it.
struct Voice
{
    AnyInst storage;
    Inst* inst = nullptr;
    int note = -1;
    double t = 0.0, endT = 1.0e9, attack = 0.0;
    bool released = false;
    uint64_t age = 0;
    AutoParam vg { 1.0f };

    bool active() const { return inst != nullptr; }
    void kill() { inst = nullptr; note = -1; storage.emplace<std::monostate>(); }
};

class Engine
{
public:
    static constexpr int kMaxVoices = 24;

    void prepare (double sampleRate, int maxBlock);

    // --- control (audio thread)
    void setDisc (int disc);                 // -1 = no disc
    int  getDisc() const { return disc; }
    void noteOn (int midi, float velocity01);
    void noteOff (int midi);
    void releaseAll();

    // --- rendering
    void renderVoices (float* bus, int n);                       // mono instrument bus
    void processFx (const float* bus, float* L, float* R, int n); // FX + master

    // --- parameters (set once per block)
    float attackS = 0.0f, releaseS = 0.5f;
    float volume = 75.0f, space = 55.0f, tone = 65.0f;
    bool  lever = false;
    double beatsPerPulse = 0.5;
    double bpm = 120.0, ppq = 0.0; bool playing = false;

    std::atomic<bool> gateLit { false };
    std::atomic<int> activeVoices { 0 };

private:
    double sr = 44100.0;
    int disc = -1;
    std::array<Voice, kMaxVoices> voices;
    uint64_t ageCounter = 0;
    Rng rng { 0xC418u };

    // FX state
    juce::dsp::Convolution reverb { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> revBuf;
    std::vector<float> dry;
    std::vector<float> delayLine; int dWrite = 0;
    Biquad dlyLP;
    float revSend = 0.0f, dlySend = 0.0f, revTarget = 0.0f, dlyTarget = 0.0f;
    std::array<Biquad, 2> toneLP, dcHP;
    double toneCur = 200.0; int toneMode = 0; double toneRate = 1.0, toneGoal = 200.0; int toneCnt = 0;
    float gateGain = 1.0f; double freePhase = 0.0; bool lastLever = false;
    float compEnvDb = -120.0f, compGainDb = 0.0f;
    float masterGain = 0.75f;

    double toneMax() const { return 600.0 * std::pow (30.0, tone / 100.0); }
    void startTone (double from, double to, double seconds);
};
} // namespace jb
