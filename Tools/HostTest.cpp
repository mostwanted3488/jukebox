// Loads the built JUKEBOX.vst3 bundle through a real VST3 host (like a DAW does),
// inserts a disc via its parameter, plays MIDI and checks the audio + editor.
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>

using namespace juce;

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc < 2) { std::printf ("usage: HostTest /path/to/JUKEBOX.vst3\n"); return 2; }

    VST3PluginFormat fmt;
    OwnedArray<PluginDescription> found;
    fmt.findAllTypesForFile (found, String (argv[1]));
    if (found.isEmpty()) { std::printf ("FAIL: no plugin found in bundle\n"); return 1; }
    const auto& desc = *found[0];
    std::printf ("found: %s by %s, instrument=%d, category=%s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), (int) desc.isInstrument, desc.category.toRawUTF8());

    String err;
    auto inst = fmt.createInstanceFromDescription (desc, 44100.0, 512, err);
    if (inst == nullptr) { std::printf ("FAIL: %s\n", err.toRawUTF8()); return 1; }
    inst->enableAllBuses();
    inst->prepareToPlay (44100.0, 512);

    int failures = 0;
    std::printf ("parameters:");
    for (int i = 0; i < std::min (9, inst->getParameters().size()); ++i) std::printf (" [%s]", inst->getParameters()[i]->getName (32).toRawUTF8());
    std::printf ("\n");

    // choose disc #2 (CRADLE) through the host-visible parameter
    for (auto* p : inst->getParameters())
        if (p->getName (32) == "Disc") { p->setValueNotifyingHost (2.0f / 10.0f); std::printf ("Disc -> %s\n", p->getCurrentValueAsText().toRawUTF8()); }

    AudioBuffer<float> buf (2, 512); MidiBuffer midi; float peak = 0;
    for (int b = 0; b < 200; ++b)
    {
        buf.clear(); midi.clear();
        if (b == 1) midi.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        if (b == 80) midi.addEvent (MidiMessage::noteOff (1, 60), 0);
        inst->processBlock (buf, midi);
        peak = std::max (peak, buf.getMagnitude (0, 512));
    }
    std::printf ("%s: peak %.1f dBFS after a MIDI note through the VST3 host\n", peak > 1e-3f && peak < 1.0f ? "ok  " : "FAIL", 20 * std::log10 (peak + 1e-9f));
    if (! (peak > 1e-3f && peak < 1.0f)) ++failures;

    MemoryBlock state; inst->getStateInformation (state);
    std::printf ("%s: state chunk %d bytes\n", state.getSize() > 50 ? "ok  " : "FAIL", (int) state.getSize());
    if (state.getSize() <= 50) ++failures;

    if (auto* ed = inst->createEditorIfNeeded())
    {
        std::printf ("ok  : editor opened %dx%d\n", ed->getWidth(), ed->getHeight());
        delete ed;
    }
    else { std::printf ("FAIL: no editor\n"); ++failures; }

    inst->releaseResources();
    inst.reset();
    std::printf ("%d failure(s)\n", failures);
    return failures;
}
