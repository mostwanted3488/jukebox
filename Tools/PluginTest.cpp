// End-to-end test of the real plugin code: MIDI in -> audio out, redstone gate, state save/restore,
// and screenshots of the editor in several states.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

using namespace juce;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

static void setParam (AudioProcessor& p, const char* id, float plainValue)
{
    auto& jp = static_cast<JukeboxProcessor&> (p);
    auto* prm = jp.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

struct Stats { float peak = 0, rms = 0; bool nan = false; };
static Stats render (AudioProcessor& p, double seconds, std::function<void (MidiBuffer&, int)> midiAt, AudioBuffer<float>* keep = nullptr)
{
    const int bs = 512, total = (int) (seconds * 44100);
    AudioBuffer<float> buf (2, bs); MidiBuffer midi; Stats s; double sum = 0; int count = 0;
    if (keep) keep->setSize (2, total);
    for (int pos = 0; pos < total; pos += bs)
    {
        buf.clear(); midi.clear();
        midiAt (midi, pos);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const float x = buf.getSample (c, i);
                if (std::isnan (x) || std::isinf (x)) s.nan = true;
                s.peak = std::max (s.peak, std::abs (x)); sum += x * x; ++count;
                if (keep && pos + i < total) keep->setSample (c, pos + i, x);
            }
    }
    s.rms = (float) std::sqrt (sum / std::max (1, count));
    return s;
}

static void pump (AudioProcessorEditor& ed, int ms)   // advance the UI in real time
{
    const double end = Time::getMillisecondCounterHiRes() + ms;
    while (Time::getMillisecondCounterHiRes() < end) { static_cast<JukeboxEditor&> (ed).tickForTest(); Thread::sleep (16); }
}

static void snapshot (Component& c, const File& f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile(); FileOutputStream os (f);
    PNGImageFormat().writeImageToStream (img, os);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const File out (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    std::unique_ptr<AudioProcessor> plugin (createPluginFilter());
    auto& jp = static_cast<JukeboxProcessor&> (*plugin);
    plugin->setPlayConfigDetails (0, 2, 44100, 512);
    plugin->prepareToPlay (44100, 512);

    // 1) silence without a disc
    auto chord = [] (MidiBuffer& m, int pos) { if (pos == 0) for (int n : { 48, 55, 60, 64, 67 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 10);
                                               if (pos == 44100) for (int n : { 48, 55, 60, 64, 67 }) m.addEvent (MidiMessage::noteOff (1, n), 0); };
    auto s0 = render (*plugin, 2.0, chord);
    CHECK (s0.peak < 1.0e-4f, "no disc -> silent");

    // 2) every disc plays, no NaN, peaks below 0 dBFS
    for (int d = 0; d < jb::kNumDiscs; ++d)
    {
        jp.setDiscFromUI (d);
        AudioBuffer<float> keep;
        auto s = render (*plugin, 4.0, chord, &keep);
        char msg[160]; std::snprintf (msg, sizeof msg, "%-9s plays  peak %5.1f dBFS  rms %5.1f dB", jb::discs()[(size_t) d].name,
                                      20 * std::log10 (s.peak + 1e-9f), 20 * std::log10 (s.rms + 1e-9f));
        CHECK (! s.nan && s.peak > 1.0e-3f && s.peak < 1.0f, msg);
    }

    // 3) release follows the disc default when inserted from the UI
    jp.setDiscFromUI (4); // VOID
    const float rel = releaseSeconds (jp.apvts.getRawParameterValue (ParamIDs::release)->load());
    CHECK (std::abs (rel - 1.8f) < .02f, "inserting VOID sets RELEASE to 1.8 s");

    // 4) redstone gate: with the lever on, the held chord is chopped
    jp.setDiscFromUI (2); // EMBER (sustained pad)
    setParam (*plugin, ParamIDs::lever, 1.0f);
    setParam (*plugin, ParamIDs::repeater, 1.0f);   // 1/8 @ 120 BPM -> 4 pulses / s
    AudioBuffer<float> gated;
    auto held = [] (MidiBuffer& m, int pos) { if (pos == 0) for (int n : { 48, 60, 64 }) m.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0); };
    render (*plugin, 3.0, held, &gated);
    float lo = 1e9f, hi = 0;
    for (int w = 1; w < 20; ++w)  // 50 ms windows after the attack
    {
        double e = 0; for (int i = 0; i < 2205; ++i) e += std::pow (gated.getSample (0, (int) (1.0 * 44100) + w * 2205 + i), 2);
        const float r = (float) std::sqrt (e / 2205); lo = std::min (lo, r); hi = std::max (hi, r);
    }
    char gmsg[120]; std::snprintf (gmsg, sizeof gmsg, "redstone gate chops the sound (%.1f dB between pulses)", 20 * std::log10 (hi / std::max (lo, 1e-9f)));
    CHECK (hi / std::max (lo, 1e-9f) > 4.0f, gmsg);
    setParam (*plugin, ParamIDs::lever, 0.0f);
    render (*plugin, 3.0, [] (MidiBuffer& m, int pos) { if (pos == 0) m.addEvent (MidiMessage::allNotesOff (1), 0); });

    // 5) state save / restore
    setParam (*plugin, ParamIDs::volume, 42); setParam (*plugin, ParamIDs::attack, 33); setParam (*plugin, ParamIDs::lever, 1);
    setParam (*plugin, ParamIDs::repeater, 2); jp.setOctaveBase (60); jp.setDiscFromUI (7);
    MemoryBlock state; plugin->getStateInformation (state);
    std::unique_ptr<AudioProcessor> other (createPluginFilter());
    auto& op = static_cast<JukeboxProcessor&> (*other);
    other->setStateInformation (state.getData(), (int) state.getSize());
    auto v = [&] (JukeboxProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    CHECK (std::abs (v (op, ParamIDs::volume) - 42) < .01f && std::abs (v (op, ParamIDs::attack) - 33) < .01f, "restore: volume + attack");
    CHECK (v (op, ParamIDs::lever) > .5f && (int) v (op, ParamIDs::repeater) == 2, "restore: lever + repeater");
    CHECK ((int) v (op, ParamIDs::disc) == 8 && op.getOctaveBase() == 60, "restore: disc (LAGOON) + keyboard octave");
    CHECK (std::abs (v (op, ParamIDs::release) - v (jp, ParamIDs::release)) < .01f, "restore: release");

    // 6) editor screenshots
    setParam (*plugin, ParamIDs::lever, 0); jp.setOctaveBase (48); setParam (*plugin, ParamIDs::volume, 75);
    jp.setDiscFromUI (-1);
    {
        std::unique_ptr<AudioProcessorEditor> ed (plugin->createEditor());
        ed->setSize (1120, 680);
        pump (*ed, 400);
        snapshot (*ed, out.getChildFile ("ui_empty.png"));

        jp.setDiscFromUI (1);                                      // CRADLE, animated in by the parameter sync
        pump (*ed, 350);
        snapshot (*ed, out.getChildFile ("ui_inserting.png"));
        pump (*ed, 900);

        setParam (*plugin, ParamIDs::lever, 1);
        for (int k = 0; k < 6; ++k)
        {
            render (*plugin, 0.12, [k] (MidiBuffer& m, int pos) { if (pos == 0) m.addEvent (MidiMessage::noteOn (1, 55 + k * 3, (uint8) 100), 0); });
            pump (*ed, 90);
        }
        snapshot (*ed, out.getChildFile ("ui_playing.png"));
        ed->setSize (560, 340);
        pump (*ed, 100);
        snapshot (*ed, out.getChildFile ("ui_small.png"));
        CHECK (true, "editor screenshots written");
    }

    std::printf ("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
