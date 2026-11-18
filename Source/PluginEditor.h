#pragma once
#include "PluginProcessor.h"
#include "UI/Pixel.h"
#include "UI/Scene.h"
#include "UI/VoxelNotes.h"
#include <juce_dsp/juce_dsp.h>

namespace ui
{
// On-screen keyboard: 2 octaves + 1 key, follows incoming MIDI too.
class Keys final : public juce::Component
{
public:
    Keys (JukeboxProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<bool()> canPlay;          // false -> no disc (editor shows a hint)
    juce::Colour tint;
private:
    JukeboxProcessor& proc;
    int mouseNote = -1;
    int noteAt (juce::Point<float>) const;
    static bool isBlack (int m) { const int k = m % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
};

// Everything at a fixed design size (1120 x 680); the editor scales it.
class Content final : public juce::Component
{
public:
    explicit Content (JukeboxProcessor&);
    ~Content() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override {}
    void tick();
    void toast (const juce::String&);
    void toggleLever() { scene.toggleLever(); }
    void shiftOctave (int d);

    static constexpr int W = 1120, H = 680;

private:
    JukeboxProcessor& proc;
    PixelLAF laf;
    Scene scene;
    Keys keys;
    VoxelNotes notes;
    juce::Random rnd;

    juce::Slider volume, space, tone, attack, release;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aVol, aSpace, aTone, aAtk, aRel;
    juce::TextButton repeater, octDown { "<" }, octUp { ">" };

    juce::dsp::FFT fft { 8 };
    std::array<float, 512> fftData {};
    std::array<float, 12> meter {};

    juce::String toastText; double toastT = -1e9;

    void setupSlider (juce::Slider&);
};
} // namespace ui

class JukeboxEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit JukeboxEditor (JukeboxProcessor&);
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void tickForTest() { content.tick(); }
private:
    void timerCallback() override { content.tick(); }
    ui::Content content;
};
