#pragma once
// Minimal emulation of Web Audio AudioParam automation (setValueAtTime, linearRamp,
// exponentialRamp, setTargetAtTime, cancelScheduledValues) so the instrument
// definitions from the browser preview port 1:1. Times are voice-local seconds.
#include <array>
#include <cmath>
#include <algorithm>

namespace jb
{
struct AutoParam
{
    enum Type { Set, Lin, Exp, Target };
    struct Ev { Type type; double t; float v; double tau; };

    explicit AutoParam (float defaultValue = 0.0f) : def (defaultValue) {}

    void reset (float defaultValue) { def = defaultValue; n = 0; }
    void set    (float v, double t)             { add ({ Set, t, v, 0.0 }); }
    void lin    (float v, double t)             { add ({ Lin, t, v, 0.0 }); }
    void exp    (float v, double t)             { add ({ Exp, t, v, 0.0 }); }
    void target (float v, double t, double tau) { add ({ Target, t, v, tau }); }
    void cancel (double t)
    {
        int k = 0;
        for (int i = 0; i < n; ++i) if (evs[(size_t) i].t < t) evs[(size_t) k++] = evs[(size_t) i];
        n = k;
    }

    float at (double t) const
    {
        double lastT = 0.0; float lastV = def;
        bool tgt = false; double tT = 0.0, tTau = 1.0; float tStart = 0.0f, tV = 0.0f;

        auto stateAt = [&] (double x) -> float
        {
            return tgt ? (float) (tV + (tStart - tV) * std::exp (-(x - tT) / tTau)) : lastV;
        };

        for (int i = 0; i < n; ++i)
        {
            const Ev& e = evs[(size_t) i];
            if (t < e.t)
            {
                if (e.type == Lin || e.type == Exp)
                {
                    const float v0 = stateAt (lastT);
                    const double span = e.t - lastT;
                    if (span <= 0.0) return e.v;
                    const double k = (t - lastT) / span;
                    if (e.type == Lin) return (float) (v0 + (e.v - v0) * k);
                    if (v0 * e.v > 0.0f) return (float) (v0 * std::pow ((double) e.v / v0, k));
                    return v0;
                }
                return stateAt (t);
            }

            if (e.type == Target)
            {
                tStart = stateAt (e.t); tgt = true; tT = e.t; tV = e.v; tTau = std::max (1.0e-5, e.tau);
            }
            else
            {
                tgt = false; lastV = e.v;
            }
            lastT = e.t;
        }
        return stateAt (t);
    }

private:
    void add (Ev e)   // keeps events sorted by time (stable), fixed capacity: real-time safe
    {
        if (n >= kMax) return;
        int i = n;
        while (i > 0 && evs[(size_t) i - 1].t > e.t) { evs[(size_t) i] = evs[(size_t) i - 1]; --i; }
        evs[(size_t) i] = e; ++n;
    }

    static constexpr int kMax = 16;
    float def;
    std::array<Ev, kMax> evs {};
    int n = 0;
};
} // namespace jb
