#pragma once
// The 10 disc instruments, ported 1:1 from the browser preview (see preview/index.html, INST).
#include "Automation.h"
#include "DspBits.h"
#include <array>
#include <variant>

namespace jb
{
// ---------------------------------------------------------------- disc table
struct DiscInfo
{
    const char* id; const char* name; const char* desc; uint32_t colour;
    float release;       // default release (s)
    float rev, dly;      // reverb / delay send
    int   dlyT;          // delay time in 16th notes
    float level;         // loudness trim (measured offline)
};

inline constexpr int kNumDiscs = 10;
inline const std::array<DiscInfo, kNumDiscs>& discs()
{
    static const std::array<DiscInfo, kNumDiscs> d { {
        { "yellow", "ARPEGGIO", "soft sustained synth - based on Aria Math",         0xfff2c12e, 1.20f, .75f, .22f, 3, 0.412f },
        { "green",  "CRADLE",   "felt piano + sub - based on Subwoofer Lullaby",     0xff5fe05a, 0.45f, .65f, .04f, 4, 0.437f },
        { "red",    "EMBER",    "dark saw pad with a sub octave",                    0xffe0413b, 0.80f, .55f, .20f, 3, 0.912f },
        { "purple", "AMETHYST", "glass FM bells",                                    0xff8b7bff, 0.50f, .75f, .30f, 3, 1.084f },
        { "black",  "VOID",     "slow dark drone",                                   0xff9a9aa2, 1.80f, .90f, .12f, 6, 0.977f },
        { "white",  "SNOW",     "music box, metal tines",                            0xffeef0ff, 0.30f, .60f, .25f, 3, 1.230f },
        { "broken", "CRACKED",  "broken record: bitcrush, wow & flutter, dropouts",  0xffbdb3a2, 0.20f, .40f, .20f, 2, 1.462f },
        { "blue",   "LAGOON",   "watery electric piano, tremolo",                    0xff6fa8ff, 0.40f, .55f, .22f, 3, 0.638f },
        { "orange", "AMBER",    "warm analog brass, vibrato",                        0xffff6a2b, 0.25f, .40f, .18f, 3, 0.851f },
        { "swirl",  "SPIRAL",   "hypnotic swirling supersaw",                        0xffff2448, 0.70f, .60f, .40f, 3, 0.955f },
    } };
    return d;
}

// ---------------------------------------------------------------- base
struct Inst
{
    explicit Inst (double sampleRate) : sr (sampleRate) {}
    virtual ~Inst() = default;
    virtual float tick (double t) = 0;
    double sr;
    double stopAt = -1.0;   // natural end (oscillators stopped), -1 = sustains until released
};

#define JB_INST(Name) struct Name final : Inst { Name (double f, float v, double sr, Rng& rng, int midi); float tick (double t) override;
#define JB_END };

JB_INST (Yellow)
    AutoParam g, lpf; Osc o1 { Osc::Triangle }, o2 { Osc::Triangle }, o3, o4, o5 { Osc::Saw }, lfo;
    Biquad lp, lp2; double f, f1, f2, f5; int cnt = 0;
JB_END
JB_INST (Green)
    std::array<Osc, 11> os; std::array<AutoParam, 11> og; std::array<double, 11> fr {};
    Biquad lpA, lpB, bp; AutoParam ng, sg; Osc sub; float out2, subAmt; double f; Rng* r;
JB_END
JB_INST (Red)
    AutoParam g, lpf; Osc s1 { Osc::Saw }, s2 { Osc::Saw }, sb; Biquad lp; double f, fa, fb; int cnt = 0;
JB_END
JB_INST (Purple)
    AutoParam g, mg; Osc c, mo; Biquad bl; double f;
JB_END
JB_INST (Black)
    AutoParam g; Osc s1 { Osc::Saw }, s2 { Osc::Saw }, sb; Biquad lp; double f, fa, fb;
JB_END
JB_INST (White)
    std::array<Osc, 3> os; std::array<AutoParam, 3> og; std::array<double, 3> fr {}; Biquad lp; float gain;
JB_END
JB_INST (Broken)
    AutoParam g, gate, det; Osc o1 { Osc::Triangle }, o2 { Osc::Square }, lfo; Biquad lp; double f, lfoRate;
JB_END
JB_INST (Blue)
    AutoParam g, mg; Osc c1, c2, mo, tl; Biquad lp; double f, f2;
JB_END
JB_INST (Orange)
    AutoParam g, lpf, vib; Osc s1 { Osc::Saw }, s2 { Osc::Saw }, sq { Osc::Square }, lfo; Biquad lp; double f; int cnt = 0;
JB_END
JB_INST (Swirl)
    AutoParam g; std::array<Osc, 3> saws { Osc (Osc::Saw), Osc (Osc::Saw), Osc (Osc::Saw) }; Osc fl, pl; Biquad lp; double f; int cnt = 0;
JB_END

#undef JB_INST
#undef JB_END

using AnyInst = std::variant<std::monostate, Yellow, Green, Red, Purple, Black, White, Broken, Blue, Orange, Swirl>;

// Builds instrument `disc` (0..9) in place inside `slot`, returns a pointer to it.
Inst* makeInstrument (AnyInst& slot, int disc, int midi, float vel, double sr, Rng& rng);
} // namespace jb
