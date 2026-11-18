// Offline parity test: renders every disc with the same patterns as the browser preview check
// and prints peak / RMS / tail levels, plus WAV files of the full chain for listening.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Engine/Engine.h"
#include <cstdio>

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (f), sr, (unsigned) b.getNumChannels(), 16, {}, 0));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int main (int argc, char** argv)
{
    const double sr = 44100.0;
    const int block = 256, total = (int) (sr * 9);
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    for (int d = 0; d < jb::kNumDiscs; ++d)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            jb::Engine e;
            e.prepare (sr, block);
            e.setDisc (d);
            e.attackS = 0.0f;
            e.releaseS = jb::discs()[(size_t) d].release;

            struct Ev { double t; int note; bool on; float vel; };
            std::vector<Ev> evs;
            if (mode == 0) for (int m : { 48, 55, 60, 64, 67, 72 }) { evs.push_back ({ .1, m, true, 1.0f }); evs.push_back ({ 2.1, m, false, 0 }); }
            else { int i = 0; for (int m : { 36, 48, 60, 72, 84 }) { evs.push_back ({ .1 + i * .5, m, true, .85f }); evs.push_back ({ .5 + i * .5, m, false, 0 }); ++i; } }
            std::sort (evs.begin(), evs.end(), [] (auto& a, auto& b) { return a.t < b.t; });

            juce::AudioBuffer<float> busOut (1, total), full (2, total);
            busOut.clear(); full.clear();
            std::vector<float> bus ((size_t) block);
            juce::dsp::IIR::Filter<float> hp;  // 20 Hz DC blocker, as in the browser check
            hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 20.0f, 0.707f);
            size_t ev = 0;
            for (int pos = 0; pos < total; pos += block)
            {
                const int n = std::min (block, total - pos);
                std::fill (bus.begin(), bus.end(), 0.0f);
                int cur = 0;
                while (ev < evs.size() && evs[ev].t * sr < pos + n)
                {
                    const int at = std::max (0, (int) (evs[ev].t * sr) - pos);
                    e.renderVoices (bus.data() + cur, at - cur); cur = at;
                    if (evs[ev].on) e.noteOn (evs[ev].note, evs[ev].vel); else e.noteOff (evs[ev].note);
                    ++ev;
                }
                e.renderVoices (bus.data() + cur, n - cur);
                for (int i = 0; i < n; ++i) busOut.setSample (0, pos + i, hp.processSample (bus[(size_t) i]));
                e.processFx (bus.data(), full.getWritePointer (0, pos), full.getWritePointer (1, pos), n);
            }

            auto stats = [&] (const juce::AudioBuffer<float>& b, double a0, double a1, double t0)
            {
                double pk = 0, s = 0, tail = 0; int c = 0, tc = 0; bool nan = false;
                for (int i = 0; i < total; ++i)
                {
                    const float x = b.getSample (0, i);
                    if (std::isnan (x)) nan = true;
                    pk = std::max (pk, (double) std::abs (x));
                    if (i >= a0 * sr && i < a1 * sr) { s += x * x; ++c; }
                    if (i >= t0 * sr) { tail += x * x; ++tc; }
                }
                std::printf ("peak=%6.1f rms=%6.1f tail=%7.1f nan=%d", 20 * std::log10 (pk + 1e-12), 10 * std::log10 (s / c + 1e-24), 10 * std::log10 (tail / tc + 1e-24), (int) nan);
            };
            std::printf ("%-7s %-5s bus: ", jb::discs()[(size_t) d].id, mode == 0 ? "chord" : "notes");
            stats (busOut, .1, mode == 0 ? 2.1 : 2.7, 7.5);
            std::printf ("   full: ");
            stats (full, .1, mode == 0 ? 2.1 : 2.7, 8.5);
            std::printf ("\n");
            writeWav (outDir.getChildFile (juce::String (jb::discs()[(size_t) d].id) + (mode == 0 ? "_chord.wav" : "_notes.wav")), full, sr);
        }
    }
    return 0;
}
