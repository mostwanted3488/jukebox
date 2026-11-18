#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Engine/Engine.h"

namespace ParamIDs
{
    inline constexpr const char* disc = "disc";
    inline constexpr const char* volume = "volume";
    inline constexpr const char* space = "space";
    inline constexpr const char* tone = "tone";
    inline constexpr const char* attack = "attack";
    inline constexpr const char* release = "release";
    inline constexpr const char* lever = "lever";
    inline constexpr const char* repeater = "repeater";
}

// Knob position (0..100) <-> seconds, same curves as the preview.
inline float attackSeconds (float k)  { return k <= 0.0f ? 0.0f : 0.002f * std::pow (1000.0f, k / 100.0f); }
inline float releaseSeconds (float k) { return 0.02f * std::pow (250.0f, k / 100.0f); }
inline float releaseKnobFor (float s) { return 100.0f * std::log (s / 0.02f) / std::log (250.0f); }
inline juce::String formatTime (float s)
{
    if (s <= 0.0f) return "0 MS";
    if (s < 1.0f) return juce::String (juce::roundToInt (s * 1000.0f)) + " MS";
    return juce::String (s, 2) + " S";
}

class JukeboxProcessor final : public juce::AudioProcessor
{
public:
    JukeboxProcessor();
    ~JukeboxProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "JUKEBOX"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    jb::Engine engine;

    // UI helpers -------------------------------------------------------------
    int  getOctaveBase() const;
    void setOctaveBase (int base);
    void setDiscFromUI (int disc);          // -1 = eject; also sets RELEASE to the disc's default
    bool popNoteEvent (int& note);          // note-ons for the flying notes
    void copyScope (float* dest, int n) const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::vector<float> bus;
    int maxBlock = 512;

    juce::AbstractFifo noteFifo { 256 };
    std::array<int, 256> noteBuf {};

    static constexpr int kScope = 1024;
    std::array<float, kScope> scope {};
    std::atomic<int> scopePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JukeboxProcessor)
};
