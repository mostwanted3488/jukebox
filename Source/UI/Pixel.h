#pragma once
// Shared look: Minecraft font, colours, pixel-style sliders and buttons.
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace ui
{
inline juce::Typeface::Ptr minecraft()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::Minecraft_ttf, BinaryData::Minecraft_ttfSize);
    return tf;
}
// 0.8: JUCE's point height renders this font ~25% larger than the browser's CSS px
inline juce::Font font (float px) { return juce::Font (juce::FontOptions (minecraft()).withPointHeight (px * 0.8f)); }

namespace col
{
    const juce::Colour text { 0xffd9d6cf }, dim { 0xff8f8c86 }, label { 0xffa7a39b }, title { 0xffefe9dc };
    const juce::Colour card { 0xcc1a1a1d }, cardEdge { 0xff0e0e10 }, cardHi { 0xff2a2a30 };
    const juce::Colour neutral { 0xff6b6f78 };
}

inline juce::Colour mix (juce::Colour a, juce::Colour b, float amountOfA) { return b.interpolatedWith (a, amountOfA); }

inline void drawCard (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (col::card);     g.fillRect (r);
    g.setColour (col::cardEdge); g.drawRect (r, 4);
    g.setColour (col::cardHi);   g.fillRect (r.getX() + 4, r.getY() + 4, r.getWidth() - 8, 3);
}

inline void bevel (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill, bool pressed)
{
    g.setColour (fill); g.fillRect (r);
    const auto lo = juce::Colour (0xff1c1916), hi = juce::Colour (0xff3f3831);
    g.setColour (pressed ? lo : hi); g.fillRect (r.getX() + 3, r.getY() + 3, r.getWidth() - 6, 3); g.fillRect (r.getX() + 3, r.getY() + 3, 3, r.getHeight() - 6);
    g.setColour (pressed ? hi : lo); g.fillRect (r.getX() + 3, r.getBottom() - 6, r.getWidth() - 6, 3); g.fillRect (r.getRight() - 6, r.getY() + 3, 3, r.getHeight() - 6);
    g.setColour (juce::Colour (0xff121110)); g.drawRect (r, 3);
}

struct PixelLAF final : juce::LookAndFeel_V4
{
    juce::Colour tint = col::neutral;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override { return minecraft(); }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        const int cy = y + h / 2;
        juce::Rectangle<int> track (x, cy - 6, w, 12);
        g.setColour (juce::Colour (0xff0e0e10)); g.fillRect (track);
        g.setColour (juce::Colour (0xff3b352f)); g.drawRect (track, 3);
        juce::Rectangle<int> thumb ((int) pos - 6, cy - 9, 12, 18);
        g.setColour (juce::Colour (0xffc9c2b4)); g.fillRect (thumb);
        g.setColour (juce::Colour (0xff8b8477)); g.fillRect (thumb.getX() + 3, thumb.getBottom() - 6, thumb.getWidth() - 6, 3); g.fillRect (thumb.getRight() - 6, thumb.getY() + 3, 3, thumb.getHeight() - 6);
        g.setColour (juce::Colour (0xff121110)); g.drawRect (thumb, 3);
    }
    int getSliderThumbRadius (juce::Slider&) override { return 6; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        bevel (g, b.getLocalBounds(), over ? juce::Colour (0xff36312c) : juce::Colour (0xff2c2824), down || b.getToggleState());
    }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (font (16.0f));
        g.setColour (b.getToggleState() ? tint : juce::Colour (0xffe6e0d4));
        g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, 1), juce::Justification::centred);
    }
};
} // namespace ui
