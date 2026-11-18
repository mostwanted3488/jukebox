#include "Engine.h"

namespace jb
{
static constexpr float kGateFloor = 0.05f, kDuty = 0.5f;

void Engine::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    for (auto& v : voices) v.kill();

    // Reverb: same noise impulse as the preview (4.2 s, pow 2.6 decay), Web Audio normalisation.
    const int len = (int) (sr * 4.2);
    juce::AudioBuffer<float> ir (2, len);
    juce::Random r (0x5EED);
    double power = 0.0;
    for (int c = 0; c < 2; ++c)
    {
        auto* d = ir.getWritePointer (c);
        for (int i = 0; i < len; ++i)
        {
            const double env = std::pow (1.0 - (double) i / len, 2.6) * (i < 400 ? i / 400.0 : 1.0);
            d[i] = (float) ((r.nextFloat() * 2.0f - 1.0f) * env);
            power += (double) d[i] * d[i];
        }
    }
    power = std::sqrt (power / (2.0 * len));
    const float scale = (float) (0.00125 / std::max (power, 0.000125) * (44100.0 / sr));  // Chromium's convolver calibration
    ir.applyGain (scale);
    reverb.reset();
    reverb.prepare ({ sr, (juce::uint32) maxBlock, 2 });
    reverb.loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::yes,
                                juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);

    revBuf.setSize (2, maxBlock);
    dry.assign ((size_t) maxBlock, 0.0f);
    delayLine.assign ((size_t) (sr * 3.0) + 4, 0.0f); dWrite = 0;
    dlyLP.setLP (2600, 1, sr); dlyLP.clear();
    for (auto& f : toneLP) { f.clear(); f.setLP (200, .4, sr); }
    for (auto& f : dcHP)   { f.clear(); f.setHP (20, .7, sr); }
    toneCur = 200.0; toneMode = 0;
    gateGain = 1.0f; freePhase = 0.0;
    compEnvDb = -120.0f; compGainDb = 0.0f;
    if (disc >= 0) { const auto& d = discs()[(size_t) disc]; revTarget = revSend = d.rev; dlyTarget = dlySend = d.dly; toneCur = toneMax(); toneMode = 2; }
}

void Engine::startTone (double from, double to, double seconds)
{
    toneCur = from; toneGoal = to;
    toneRate = std::pow (to / from, 1.0 / std::max (1.0, seconds * sr));
    toneMode = 1;
}

void Engine::setDisc (int newDisc)
{
    if (newDisc == disc) return;
    releaseAll();
    disc = newDisc;
    if (disc >= 0)
    {
        const auto& d = discs()[(size_t) disc];
        revTarget = d.rev; dlyTarget = d.dly;
        startTone (250.0, toneMax(), 1.4);          // "spin-up": filter opens like the record starting
    }
    else
    {
        dlyTarget = 0.0f;
        startTone (std::max (200.0, toneCur), 180.0, 0.8);
    }
}

void Engine::noteOn (int midi, float vel)
{
    if (disc < 0) return;
    noteOff (midi);   // retrigger: release the previous instance of this note

    Voice* v = nullptr;
    for (auto& c : voices) if (! c.active()) { v = &c; break; }
    if (v == nullptr)  // steal: oldest released first, then oldest
    {
        for (auto& c : voices) if (c.released && (v == nullptr || c.age < v->age)) v = &c;
        if (v == nullptr) for (auto& c : voices) if (v == nullptr || c.age < v->age) v = &c;
    }

    const auto& d = discs()[(size_t) disc];
    v->inst = makeInstrument (v->storage, disc, midi, vel * d.level, sr, rng);
    v->note = midi; v->t = 0.0; v->released = false; v->endT = 1.0e9; v->age = ++ageCounter;
    v->attack = attackS;
    v->vg.reset (1.0f);
    if (attackS > .004f) { v->vg.set (0, 0); v->vg.lin (1, attackS); }
    else v->vg.set (1, 0);
}

void Engine::noteOff (int midi)
{
    for (auto& v : voices)
        if (v.active() && v.note == midi && ! v.released)
        {
            const double t2 = v.t, rl = releaseS;
            const float lvl = v.attack > .004 ? (float) std::clamp (t2 / v.attack, 0.0, 1.0) : 1.0f;
            v.vg.cancel (t2); v.vg.set (lvl, t2); v.vg.target (0, t2, rl / 4.0);
            v.endT = t2 + rl * 1.3 + .05;
            v.released = true;
        }
}

void Engine::releaseAll()
{
    for (auto& v : voices) if (v.active() && ! v.released) noteOff (v.note);
}

void Engine::renderVoices (float* bus, int n)
{
    const double dt = 1.0 / sr;
    int count = 0;
    for (auto& v : voices)
    {
        if (! v.active()) continue;
        ++count;
        double t = v.t;
        for (int i = 0; i < n; ++i, t += dt)
            bus[i] += v.inst->tick (t) * v.vg.at (t);
        v.t = t;
        if (t > v.endT || (v.inst->stopAt > 0.0 && t > v.inst->stopAt)) v.kill();
    }
    activeVoices.store (count);
}

void Engine::processFx (const float* bus, float* L, float* R, int n)
{
    const float smooth3 = (float) (1.0 - std::exp (-1.0 / (.3 * sr)));
    const double delaySamples = std::clamp (60.0 / std::max (20.0, bpm) / 4.0 * (disc >= 0 ? discs()[(size_t) disc].dlyT : 3) * sr,
                                            1.0, (double) delayLine.size() - 2.0);
    const float spaceAmt = space / 55.0f;
    const int dlen = (int) delayLine.size();

    // 1) delay (feeds both the dry path and the reverb send, like the preview graph)
    auto* rl = revBuf.getWritePointer (0);
    auto* rr = revBuf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        revSend += (revTarget * spaceAmt - revSend) * smooth3;
        dlySend += (dlyTarget - dlySend) * smooth3;

        double rp = dWrite - delaySamples; while (rp < 0) rp += dlen;
        const int i0 = (int) rp; const float fr = (float) (rp - i0);
        const float d = delayLine[(size_t) i0] + (delayLine[(size_t) ((i0 + 1) % dlen)] - delayLine[(size_t) i0]) * fr;
        const float y = dlyLP.process (d);
        delayLine[(size_t) dWrite] = dlySend * bus[i] + .38f * y;
        dWrite = (dWrite + 1) % dlen;

        dry[(size_t) i] = .8f * bus[i] + y;
        rl[i] = rr[i] = revSend * (bus[i] + y);
    }

    // 2) convolution reverb (stereo impulse)
    {
        juce::dsp::AudioBlock<float> block (revBuf.getArrayOfWritePointers(), 2, (size_t) n);
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // 3) tone filter, DC blocker, redstone gate, compressor, master
    const double toneTarget = toneMax();
    const float gateRise = (float) (1.0 - std::exp (-1.0 / (.003 * sr)));
    const float gateFall = (float) (1.0 - std::exp (-1.0 / (.008 * sr)));
    const float toneFollow = (float) (1.0 - std::exp (-16.0 / (.1 * sr)));
    const float att = (float) std::exp (-1.0 / (.01 * sr)), rel = (float) std::exp (-1.0 / (.3 * sr));
    const float masterTarget = std::pow (volume / 100.0f, 1.6f);
    const double beatsPerSample = bpm / 60.0 / sr;
    if (lever && ! lastLever) freePhase = 0.0;
    lastLever = lever;
    bool litNow = false;

    for (int i = 0; i < n; ++i)
    {
        // tone (spin-up / spin-down / follow knob)
        if (toneMode == 1)
        {
            toneCur *= toneRate;
            if ((toneRate >= 1.0 && toneCur >= toneGoal) || (toneRate < 1.0 && toneCur <= toneGoal)) { toneCur = toneGoal; toneMode = disc >= 0 ? 2 : 0; }
        }
        if ((toneCnt++ & 15) == 0)
        {
            if (toneMode == 2) toneCur += (toneTarget - toneCur) * toneFollow;
            for (auto& f : toneLP) f.setLP (toneCur, .4, sr);
        }

        // redstone clock: locked to the host's beat grid when playing, free-running otherwise
        double phase;
        if (playing) { const double beat = ppq + i * beatsPerSample; phase = beat / beatsPerPulse; phase -= std::floor (phase); }
        else { phase = freePhase; freePhase += beatsPerSample / beatsPerPulse; freePhase -= std::floor (freePhase); }
        const bool lit = lever && phase < kDuty;
        litNow = lit;
        const float gTarget = lever ? (lit ? 1.0f : kGateFloor) : 1.0f;
        gateGain += (gTarget - gateGain) * (gTarget > gateGain ? gateRise : gateFall);

        float l = dry[(size_t) i] + rl[i], r = dry[(size_t) i] + rr[i];
        l = dcHP[0].process (toneLP[0].process (l)) * gateGain;
        r = dcHP[1].process (toneLP[1].process (r)) * gateGain;

        // compressor: threshold -16 dB, ratio 3, 30 dB soft knee, auto make-up (Chromium style)
        const float peak = std::max (std::abs (l), std::abs (r));
        const float xDb = 20.0f * std::log10 (std::max (peak, 1.0e-6f));
        compEnvDb = xDb > compEnvDb ? xDb + att * (compEnvDb - xDb) : xDb + rel * (compEnvDb - xDb);
        auto curve = [] (float x)
        {
            const float T = -16.0f, Rr = 3.0f, W = 30.0f;
            if (2.0f * (x - T) < -W) return x;
            if (2.0f * std::abs (x - T) <= W) return x + (1.0f / Rr - 1.0f) * (x - T + W / 2.0f) * (x - T + W / 2.0f) / (2.0f * W);
            return T + (x - T) / Rr;
        };
        static const float makeupDb = -(curve (0.0f)) * 0.6f;
        compGainDb = curve (compEnvDb) - compEnvDb;
        const float gain = std::pow (10.0f, (compGainDb + makeupDb) / 20.0f);

        masterGain += (masterTarget - masterGain) * .001f;
        l *= gain * masterGain; r *= gain * masterGain;

        // safety soft-clip above -2 dBFS
        auto clip = [] (float x) { const float a = std::abs (x); return a < .8f ? x : std::copysign (.8f + .2f * std::tanh ((a - .8f) / .2f), x); };
        L[i] = clip (l); R[i] = clip (r);
    }
    gateLit.store (litNow);
}
} // namespace jb
