#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace jb
{
constexpr double kTwoPi = 6.283185307179586;

inline double mtof (double m)   { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
inline double cents (double c)  { return std::exp2 (c / 1200.0); }

// ---------------------------------------------------------------- oscillator
// Band-limited saw/square (polyBLEP) to stay close to Web Audio's wavetable oscillators.
struct Osc
{
    enum Type { Sine, Triangle, Saw, Square };
    explicit Osc (Type t = Sine) : type (t) {}

    float next (double freq, double sr)
    {
        const double dt = freq / sr;
        const double adt = std::abs (dt);
        double out = 0.0;
        switch (type)
        {
            case Sine:     out = std::sin (kTwoPi * ph); break;
            case Triangle: out = ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0); break;
            // 0.87: Web Audio normalises its band-limited saw/square tables (Gibbs overshoot), measured -1.2 dB
            case Saw:      out = 0.87 * (2.0 * frac (ph + 0.5) - 1.0 - blep (frac (ph + 0.5), adt)); break;
            case Square:   out = 0.87 * ((ph < 0.5 ? 1.0 : -1.0) + blep (ph, adt) - blep (frac (ph + 0.5), adt)); break;
        }
        ph += dt;
        ph -= std::floor (ph);
        return (float) out;
    }

    Type type;
    double ph = 0.0;

private:
    static double frac (double x) { return x - std::floor (x); }
    static double blep (double t, double dt)
    {
        if (dt <= 0.0) return 0.0;
        if (t < dt)       { t /= dt;               return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;    return t * t + t + t + 1.0; }
        return 0.0;
    }
};

// ---------------------------------------------------------------- biquad
// Same formulas as the Web Audio BiquadFilterNode (lowpass/highpass Q given in dB).
struct Biquad
{
    void setLP (double f, double qDb, double sr) { design (0, f, qDb, sr); }
    void setHP (double f, double qDb, double sr) { design (1, f, qDb, sr); }
    void setBP (double f, double q,   double sr) { design (2, f, q,   sr); }

    float process (float xin)
    {
        const double x = xin;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void clear() { z1 = z2 = 0.0; }

private:
    void design (int kind, double f, double q, double sr)
    {
        const double nyq = sr * 0.5;
        f = std::clamp (f, 10.0, nyq * 0.995);
        const double w0 = kTwoPi * f / sr, cw = std::cos (w0), sw = std::sin (w0);
        double nb0, nb1, nb2, na0, na1, na2;
        if (kind == 2)
        {
            const double alpha = sw / (2.0 * std::max (1.0e-4, q));
            nb0 = alpha; nb1 = 0.0; nb2 = -alpha; na0 = 1.0 + alpha; na1 = -2.0 * cw; na2 = 1.0 - alpha;
        }
        else
        {
            const double alpha = sw / (2.0 * std::pow (10.0, q / 20.0));
            if (kind == 0) { nb0 = (1.0 - cw) * 0.5; nb1 = 1.0 - cw;    nb2 = nb0; }
            else           { nb0 = (1.0 + cw) * 0.5; nb1 = -(1.0 + cw); nb2 = nb0; }
            na0 = 1.0 + alpha; na1 = -2.0 * cw; na2 = 1.0 - alpha;
        }
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// ---------------------------------------------------------------- tiny RNG (audio-thread safe)
struct Rng
{
    explicit Rng (uint32_t seed = 0x1234567u) : s (seed ? seed : 1u) {}
    float next01() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((s >> 8) * (1.0 / 16777216.0)); }
    float bipolar() { return next01() * 2.0f - 1.0f; }
    uint32_t s;
};
} // namespace jb
