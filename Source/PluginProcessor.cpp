#include "PluginProcessor.h"
#include "PluginEditor.h"

JukeboxProcessor::JukeboxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "JUKEBOX", createLayout())
{
    apvts.state.setProperty ("octave", 48, nullptr);
}

juce::AudioProcessorValueTreeState::ParameterLayout JukeboxProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    StringArray discNames { "None" };
    for (const auto& d : jb::discs()) discNames.add (d.name);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::disc, 1 }, "Disc", discNames, 0));

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)); });
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::volume, 1 }, "Volume", NormalisableRange<float> (0, 100), 75.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::space,  1 }, "Space",  NormalisableRange<float> (0, 100), 55.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::tone,   1 }, "Tone",   NormalisableRange<float> (0, 100), 65.0f, pct));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::attack, 1 }, "Attack", NormalisableRange<float> (0, 100), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatTime (attackSeconds (v)); })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamIDs::release, 1 }, "Release", NormalisableRange<float> (0, 100), 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatTime (releaseSeconds (v)); })));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::lever, 1 }, "Redstone Lever", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::repeater, 1 }, "Repeater", StringArray { "1/4", "1/8", "1/16", "1/8T" }, 1));
    return l;
}

bool JukeboxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void JukeboxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (32, samplesPerBlock);
    bus.assign ((size_t) maxBlock, 0.0f);
    const int d = (int) apvts.getRawParameterValue (ParamIDs::disc)->load() - 1;
    engine.setDisc (d);
    engine.prepare (sampleRate, maxBlock);
    keyboardState.reset();
}

void JukeboxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();

    // on-screen keyboard -> MIDI (and incoming MIDI -> keyboard display)
    keyboardState.processNextMidiBuffer (midi, 0, total, true);

    // parameters
    engine.setDisc ((int) apvts.getRawParameterValue (ParamIDs::disc)->load() - 1);
    engine.volume   = apvts.getRawParameterValue (ParamIDs::volume)->load();
    engine.space    = apvts.getRawParameterValue (ParamIDs::space)->load();
    engine.tone     = apvts.getRawParameterValue (ParamIDs::tone)->load();
    engine.attackS  = attackSeconds  (apvts.getRawParameterValue (ParamIDs::attack)->load());
    engine.releaseS = releaseSeconds (apvts.getRawParameterValue (ParamIDs::release)->load());
    engine.lever    = apvts.getRawParameterValue (ParamIDs::lever)->load() > 0.5f;
    static const double pulses[] = { 1.0, 0.5, 0.25, 1.0 / 3.0 };
    engine.beatsPerPulse = pulses[juce::jlimit (0, 3, (int) apvts.getRawParameterValue (ParamIDs::repeater)->load())];

    // host tempo / position (the redstone clock and the delay follow the DAW)
    engine.playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) engine.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) engine.ppq = *ppq;
            engine.playing = pos->getIsPlaying() && pos->getPpqPosition().hasValue();
        }

    // render in chunks no larger than the prepared block size, sample-accurate MIDI
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::vector<float> tmpR;
    auto it = midi.begin();
    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);
        std::fill (bus.begin(), bus.begin() + n, 0.0f);
        int cur = 0;
        for (; it != midi.end(); ++it)
        {
            const auto meta = *it;
            const int pos = meta.samplePosition - start;
            if (pos >= n) break;
            const int at = juce::jlimit (cur, n, pos);
            engine.renderVoices (bus.data() + cur, at - cur); cur = at;
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
            {
                engine.noteOn (msg.getNoteNumber(), 0.3f + 0.7f * msg.getFloatVelocity());
                if (engine.getDisc() >= 0) { const auto w = noteFifo.write (1); if (w.blockSize1 > 0) noteBuf[(size_t) w.startIndex1] = msg.getNoteNumber(); }
            }
            else if (msg.isNoteOff()) engine.noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.releaseAll();
        }
        engine.renderVoices (bus.data() + cur, n - cur);

        if (R != nullptr) engine.processFx (bus.data(), L + start, R + start, n);
        else
        {
            tmpR.resize ((size_t) n);
            engine.processFx (bus.data(), L + start, tmpR.data(), n);
            for (int i = 0; i < n; ++i) L[start + i] = 0.5f * (L[start + i] + tmpR[(size_t) i]);
        }
        engine.ppq += n * engine.bpm / 60.0 / getSampleRate();
    }

    // scope for the level meter
    int p = scopePos.load();
    for (int i = 0; i < total; ++i) { scope[(size_t) p] = L[i]; p = (p + 1) % kScope; }
    scopePos.store (p);
}

// ----------------------------------------------------------------------------- UI helpers
int JukeboxProcessor::getOctaveBase() const { return (int) apvts.state.getProperty ("octave", 48); }
void JukeboxProcessor::setOctaveBase (int base) { apvts.state.setProperty ("octave", juce::jlimit (24, 84, base), nullptr); }

void JukeboxProcessor::setDiscFromUI (int disc)
{
    auto* p = apvts.getParameter (ParamIDs::disc);
    if (disc >= 0)
        if (auto* rel = apvts.getParameter (ParamIDs::release))
            rel->setValueNotifyingHost (rel->convertTo0to1 (releaseKnobFor (jb::discs()[(size_t) disc].release)));
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) (disc + 1)));
    p->endChangeGesture();
}

bool JukeboxProcessor::popNoteEvent (int& note)
{
    const auto r = noteFifo.read (1);
    if (r.blockSize1 == 0) return false;
    note = noteBuf[(size_t) r.startIndex1];
    return true;
}

void JukeboxProcessor::copyScope (float* dest, int n) const
{
    int p = scopePos.load();
    for (int i = n - 1; i >= 0; --i) { p = (p - 1 + kScope) % kScope; dest[i] = scope[(size_t) p]; }
}

// ----------------------------------------------------------------------------- state (saved in the DAW project)
void JukeboxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void JukeboxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* JukeboxProcessor::createEditor() { return new JukeboxEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new JukeboxProcessor(); }
