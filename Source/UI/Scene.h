#pragma once
// Left half of the UI: title, jukebox (with pixel-exact slot glow), lever + redstone dust,
// the disc shelf, drag & drop, and the insert / eject animations.
#include "Pixel.h"
#include "../PluginProcessor.h"

namespace ui
{
class Scene final : public juce::Component
{
public:
    explicit Scene (JukeboxProcessor&);

    void tick (double nowMs);                       // called by the editor's timer
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; }

    void bump();                                    // jukebox reacts to a note
    void nope();                                    // shake (no disc inserted)
    void toggleLever();
    juce::Point<float> noteSpawnPoint (juce::Random&) const;
    juce::Colour tint() const;
    int shownDisc() const { return shown; }

    std::function<void (const juce::String&)> showToast;

private:
    struct Flight
    {
        bool active = false, inserting = false, user = false;
        int disc = -1, thenInsert = -1;
        double t0 = 0;
        juce::Rectangle<float> from;
    };
    struct DustRect { juce::Rectangle<int> r; int kind; };

    JukeboxProcessor& proc;
    juce::Image juke;
    std::array<juce::Image, jb::kNumDiscs> discImgs;
    std::vector<juce::uint8> slotMask, rimMask;   // 300x300 alpha masks built from the jukebox texture
    juce::Image slotGlow, rimGlow; juce::Colour glowTint;

    const juce::Rectangle<float> stage { 90.0f, 110.0f, 300.0f, 300.0f };
    const juce::Rectangle<int> shelf { 30, 500, 420, 160 };
    std::array<juce::Rectangle<float>, jb::kNumDiscs> slots;

    int shown = -1, hover = -1;
    Flight flight;
    double now = 0, bumpT = -1e9, nopeT = -1e9;

    // drag & drop
    int dragDisc = -1; bool dragMoved = false; juce::Point<float> dragStart, dragOffset, dragPos; bool overStage = false;

    // redstone
    std::vector<DustRect> dust; std::vector<juce::Rectangle<int>> sparks; std::vector<bool> sparkOn;
    juce::Path baseTop, baseLeft, baseRight;
    struct Px { juce::Rectangle<int> r; juce::Colour c; int clip; };
    std::vector<Px> basePixels, stickPixels;
    bool lastLit = false; float leverAngle = -40.0f;

    void buildMasks();
    void buildRedstone();
    void rebuildGlow (juce::Colour);
    void startInsert (int disc, juce::Rectangle<float> from, bool user);
    void startEject (int thenInsert, juce::Rectangle<float> thenFrom, bool user);
    void finishFlight();
    void syncWithParameter();
    int slotAt (juce::Point<float>) const;
    bool leverHit (juce::Point<float>) const;
    bool overJukebox (juce::Point<float>) const;
    juce::Point<float> slotPoint() const { return { stage.getX() + 300 * .505f, stage.getY() + 300 * .2f }; }
    void drawDisc (juce::Graphics&, int disc, juce::Point<float> centre, float size, float rotDeg, float sx, float sy, float alpha);
    void drawRedstone (juce::Graphics&, bool on, bool lit);
    bool leverOn() const;
};
} // namespace ui
