#include "PluginEditor.h"

namespace ui
{
// ============================================================================= keyboard
int Keys::noteAt (juce::Point<float> p) const
{
    const int base = proc.getOctaveBase();
    std::vector<int> whites; for (int m = base; m <= base + 24; ++m) if (! isBlack (m)) whites.push_back (m);
    const float ww = getWidth() / (float) whites.size();
    if (p.y < getHeight() * .6f)
        for (int m = base; m <= base + 24; ++m)
            if (isBlack (m))
            {
                const int wi = (int) std::count_if (whites.begin(), whites.end(), [m] (int w) { return w < m; });
                if (juce::Rectangle<float> (wi * ww - ww * .32f, 0, ww * .64f, getHeight() * .6f).contains (p)) return m;
            }
    const int idx = juce::jlimit (0, (int) whites.size() - 1, (int) (p.x / ww));
    return whites[(size_t) idx];
}

void Keys::paint (juce::Graphics& g)
{
    const int base = proc.getOctaveBase();
    std::vector<int> whites; for (int m = base; m <= base + 24; ++m) if (! isBlack (m)) whites.push_back (m);
    const float ww = getWidth() / (float) whites.size(); const float h = (float) getHeight();
    auto down = [&] (int m) { return m == mouseNote || proc.keyboardState.isNoteOnForChannels (0xffff, m); };

    for (size_t i = 0; i < whites.size(); ++i)
    {
        const int m = whites[i];
        const auto r = juce::Rectangle<float> (i * ww, 0, ww, h).toNearestInt();
        g.setColour (down (m) ? mix (tint, juce::Colour (0xffe9e4d8), .55f) : juce::Colour (0xffe9e4d8)); g.fillRect (r);
        g.setColour (juce::Colour (0xffc9c2b4)); g.fillRect (r.getX(), r.getBottom() - (down (m) ? 3 : 8), r.getWidth(), down (m) ? 3 : 8);
        g.setColour (juce::Colour (0xff0e0e10)); g.drawRect (r.getX(), r.getY() - 2, r.getWidth() + 2, r.getHeight() + 2, 2);
        if (m % 12 == 0)
        {
            g.setColour (juce::Colour (0xff6d675d)); g.setFont (font (16.0f));
            g.drawText ("C" + juce::String (m / 12 - 1), r.withTrimmedBottom (12), juce::Justification::centredBottom);
        }
    }
    for (int m = base; m <= base + 24; ++m)
        if (isBlack (m))
        {
            const int wi = (int) std::count_if (whites.begin(), whites.end(), [m] (int w) { return w < m; });
            const auto r = juce::Rectangle<float> (wi * ww - ww * .32f, 0, ww * .64f, h * .6f).toNearestInt();
            g.setColour (down (m) ? mix (tint, juce::Colour (0xff1f1d1b), .7f) : juce::Colour (0xff1f1d1b)); g.fillRect (r);
            g.setColour (juce::Colours::black); g.fillRect (r.getX(), r.getBottom() - 6, r.getWidth(), 6);
            g.setColour (juce::Colour (0xff3a3632)); g.fillRect (r.getX() + 2, r.getY(), 2, r.getHeight() - 6);
            g.setColour (juce::Colour (0xff0e0e10)); g.drawRect (r, 2);
        }
}

void Keys::mouseDown (const juce::MouseEvent& e)
{
    if (canPlay && ! canPlay()) return;
    mouseNote = noteAt (e.position);
    proc.keyboardState.noteOn (1, mouseNote, .85f);
}
void Keys::mouseDrag (const juce::MouseEvent& e)
{
    if (mouseNote < 0) return;
    const int m = noteAt (e.position);
    if (m != mouseNote) { proc.keyboardState.noteOff (1, mouseNote, 0); mouseNote = m; proc.keyboardState.noteOn (1, m, .85f); }
}
void Keys::mouseUp (const juce::MouseEvent&)
{
    if (mouseNote >= 0) proc.keyboardState.noteOff (1, mouseNote, 0);
    mouseNote = -1;
}

// ============================================================================= content
Content::Content (JukeboxProcessor& p) : proc (p), scene (p), keys (p)
{
    setLookAndFeel (&laf);
    setSize (W, H);
    addAndMakeVisible (scene); scene.setBounds (0, 0, 480, H);
    scene.showToast = [this] (const juce::String& s) { toast (s); };

    for (auto* s : { &volume, &space, &tone, &attack, &release }) setupSlider (*s);
    volume.setBounds (750, 196, 84, 24); space.setBounds (846, 196, 84, 24); tone.setBounds (942, 196, 84, 24);
    attack.setBounds (520, 286, 96, 24); release.setBounds (640, 286, 96, 24);
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    aVol   = std::make_unique<SA> (proc.apvts, ParamIDs::volume,  volume);
    aSpace = std::make_unique<SA> (proc.apvts, ParamIDs::space,   space);
    aTone  = std::make_unique<SA> (proc.apvts, ParamIDs::tone,    tone);
    aAtk   = std::make_unique<SA> (proc.apvts, ParamIDs::attack,  attack);
    aRel   = std::make_unique<SA> (proc.apvts, ParamIDs::release, release);

    addAndMakeVisible (repeater); repeater.setBounds (960, 274, 124, 38);
    repeater.onClick = [this]
    {
        if (auto* r = proc.apvts.getParameter (ParamIDs::repeater))
        {
            const int cur = (int) proc.apvts.getRawParameterValue (ParamIDs::repeater)->load();
            r->beginChangeGesture(); r->setValueNotifyingHost (r->convertTo0to1 ((float) ((cur + 1) % 4))); r->endChangeGesture();
        }
    };
    addAndMakeVisible (octDown); octDown.setBounds (614, 360, 34, 32); octDown.onClick = [this] { shiftOctave (-12); };
    addAndMakeVisible (octUp);   octUp.setBounds (728, 360, 34, 32);   octUp.onClick   = [this] { shiftOctave (12); };

    addAndMakeVisible (keys); keys.setBounds (516, 404, 568, 148);
    keys.canPlay = [this]
    {
        if (proc.engine.getDisc() >= 0) return true;
        scene.nope(); toast ("insert a disc into the jukebox"); return false;
    };
    notes.setCanvasSize (W, H);
    int stale; while (proc.popNoteEvent (stale)) {}   // notes played while the window was closed
}

Content::~Content() { setLookAndFeel (nullptr); }

void Content::setupSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (s);
}

void Content::shiftOctave (int d)
{
    const int nb = proc.getOctaveBase() + d;
    if (nb < 24 || nb > 84) return;
    proc.keyboardState.allNotesOff (1);
    proc.setOctaveBase (nb);
}

void Content::toast (const juce::String& s) { toastText = s; toastT = juce::Time::getMillisecondCounterHiRes(); }

void Content::tick()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    scene.tick (now);
    const auto tc = scene.tint();
    laf.tint = tc; keys.tint = tc;

    int n, spawned = 0;
    while (proc.popNoteEvent (n))
    {
        if (scene.shownDisc() < 0 || ++spawned > 6) continue;   // drain, but never burst
        const auto sp = scene.noteSpawnPoint (rnd);
        notes.spawn (sp.x, sp.y, tc, n);
        scene.bump();
    }
    notes.render (now);

    // 12-band meter (same mapping as the preview's analyser)
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    proc.copyScope (fftData.data(), 256);
    for (int i = 0; i < 256; ++i) fftData[(size_t) i] *= .5f - .5f * std::cos (juce::MathConstants<float>::twoPi * i / 255.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int i = 0; i < 12; ++i)
    {
        const int bin = (int) std::floor (std::pow (i / 12.0, 1.6) * 70.0) + 1;
        const float db = 20.0f * std::log10 (fftData[(size_t) bin] / 256.0f + 1.0e-9f);
        const float v = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 70.0f);
        meter[(size_t) i] = .8f * meter[(size_t) i] + .2f * v;
    }

    repeater.setButtonText ("REPEATER " + juce::StringArray { "1/4", "1/8", "1/16", "1/8T" }[(int) proc.apvts.getRawParameterValue (ParamIDs::repeater)->load()]);
    repaint();
}

void Content::paint (juce::Graphics& g)
{
    const auto tc = scene.tint();
    // background
    {
        juce::ColourGradient bg (mix (tc, juce::Colour (0xff202228), .2f), W * .28f, H * .45f, juce::Colour (0xff141518), W * .28f + W * .6f, H * .45f, true);
        g.setGradientFill (bg); g.fillAll();
    }

    auto label = [&] (const juce::String& s, int x, int y, int w, juce::Colour c, float size = 16.0f, juce::Justification j = juce::Justification::centredLeft)
    {
        g.setColour (c); g.setFont (font (size)); g.drawText (s, x, y, w, (int) size + 8, j);
    };

    // card 1: disc + VOLUME / SPACE / TONE + meter
    drawCard (g, { 500, 150, 600, 90 });
    const int d = scene.shownDisc();
    if (d >= 0)
    {
        label (juce::String::fromUTF8 ("\xe2\x99\xaa ") + jb::discs()[(size_t) d].name, 520, 162, 230, tc, 24.0f);
        g.setColour (col::dim); g.setFont (font (16.0f));
        g.drawFittedText (jb::discs()[(size_t) d].desc, 520, 194, 222, 40, juce::Justification::topLeft, 2, 1.0f);
    }
    else
    {
        label ("- NO DISC -", 520, 162, 230, col::neutral, 24.0f);
        label ("the jukebox is waiting", 520, 194, 230, col::dim);
    }
    label ("VOLUME", 750, 166, 84, col::label, 16.0f, juce::Justification::centred);
    label ("SPACE",  846, 166, 84, col::label, 16.0f, juce::Justification::centred);
    label ("TONE",   942, 166, 84, col::label, 16.0f, juce::Justification::centred);
    for (int i = 0; i < 12; ++i)
    {
        const int h = std::max (2, juce::roundToInt (meter[(size_t) i] * 13.0f) * 2);
        g.setColour (tc); g.fillRect (1038 + i * 4, 212 - h, 3, h);
    }

    // card 2: envelope + redstone
    drawCard (g, { 500, 254, 600, 80 });
    label ("ATTACK",  520, 262, 96, col::label, 16.0f, juce::Justification::centred);
    label ("RELEASE", 640, 262, 96, col::label, 16.0f, juce::Justification::centred);
    label (formatTime (attackSeconds  (proc.apvts.getRawParameterValue (ParamIDs::attack)->load())),  520, 308, 96, tc, 16.0f, juce::Justification::centred);
    label (formatTime (releaseSeconds (proc.apvts.getRawParameterValue (ParamIDs::release)->load())), 640, 308, 96, tc, 16.0f, juce::Justification::centred);
    const bool lever = proc.apvts.getRawParameterValue (ParamIDs::lever)->load() > 0.5f;
    label ("REDSTONE", 780, 268, 110, juce::Colour (0xffcfc8ba));
    label (lever ? "ON" : "OFF", 884, 268, 60, lever ? juce::Colour (0xffff3b22) : juce::Colour (0xff6e1a12));
    label ("pulse gate - lever or L key", 780, 294, 180, juce::Colour (0xff6f6b65));

    // card 3: keyboard
    drawCard (g, { 500, 348, 600, 220 });
    label ("KEYBOARD", 516, 364, 100, juce::Colour (0xffcfc8ba));
    const int base = proc.getOctaveBase();
    label ("C" + juce::String (base / 12 - 1) + "-C" + juce::String (base / 12 + 1), 648, 364, 80, col::label, 16.0f, juce::Justification::centred);
    label ("play from your piano roll or click the keys", 772, 364, 320, col::dim);

    // signature
    label ("@buyorbealoser", 500, 582, 600, mix (tc, col::dim, .35f), 16.0f, juce::Justification::centredRight);
}

void Content::paintOverChildren (juce::Graphics& g)
{
    // PS1 voxel notes (low-res canvas, hard pixels)
    if (notes.busy())
    {
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        const auto& img = notes.image();
        g.drawImage (img, 0, 0, img.getWidth() * notes.pixel(), img.getHeight() * notes.pixel(), 0, 0, img.getWidth(), img.getHeight());
    }
    // toast
    const double age = juce::Time::getMillisecondCounterHiRes() - toastT;
    if (age < 1800.0)
    {
        g.setFont (font (16.0f));
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), toastText) + 32;
        juce::Rectangle<int> r ((W - w) / 2, H - 52, w, 34);
        const float a = (float) juce::jlimit (0.0, 1.0, std::min (age / 200.0, (1800.0 - age) / 300.0));
        g.setColour (juce::Colour (0xff0e0e10).withAlpha (a)); g.fillRect (r);
        g.setColour (scene.tint().withAlpha (a)); g.drawRect (r, 3);
        g.setColour (col::text.withAlpha (a)); g.drawText (toastText, r, juce::Justification::centred);
    }
}
} // namespace ui

// ============================================================================= editor
JukeboxEditor::JukeboxEditor (JukeboxProcessor& p) : AudioProcessorEditor (p), content (p)
{
    addAndMakeVisible (content);
    setResizable (true, true);
    setResizeLimits (ui::Content::W / 2, ui::Content::H / 2, ui::Content::W * 3 / 2, ui::Content::H * 3 / 2);
    getConstrainer()->setFixedAspectRatio ((double) ui::Content::W / ui::Content::H);
    setSize (ui::Content::W * 9 / 10, ui::Content::H * 9 / 10);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

void JukeboxEditor::resized()
{
    const float s = getWidth() / (float) ui::Content::W;
    content.setTransform (juce::AffineTransform::scale (s));
}

bool JukeboxEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == 'L' || k.getKeyCode() == 'l') { content.toggleLever(); return true; }
    if (k == juce::KeyPress::upKey)   { content.shiftOctave (12);  return true; }
    if (k == juce::KeyPress::downKey) { content.shiftOctave (-12); return true; }
    return false;
}
