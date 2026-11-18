#include "Instruments.h"

namespace jb
{
// WaveShaper curve from the preview (7-step quantiser) with Web Audio's linear interpolation.
static float crush (float x)
{
    static const auto curve = []
    {
        std::array<float, 1024> c {};
        for (int i = 0; i < 1024; ++i) { const double v = i / 511.5 - 1.0; c[(size_t) i] = (float) (std::round (v * 7.0) / 7.0); }
        return c;
    }();
    x = std::clamp (x, -1.0f, 1.0f);
    const float pos = (x + 1.0f) * 0.5f * 1023.0f;
    const int i0 = std::min (1022, (int) pos);
    const float fr = pos - (float) i0;
    return curve[(size_t) i0] + (curve[(size_t) i0 + 1] - curve[(size_t) i0]) * fr;
}

// ---------------------------------------------------------------- ARPEGGIO (Aria Math)
Yellow::Yellow (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .13f, .025); g.target (v * .07f, .025, .6);
    lpf.set (4200, 0); lpf.target (3200, .03, .5);
    lp.setLP (4200, .7, sr); lp2.setLP (5500, .5, sr);
    f1 = f * cents (-5); f2 = f * cents (6); f5 = f * cents (3);
}
float Yellow::tick (double t)
{
    if ((cnt++ & 7) == 0) lp.setLP (lpf.at (t), .7, sr);
    float s = o1.next (f1, sr) + .8f * o2.next (f2, sr) + .6f * o3.next (f, sr) + .18f * o4.next (2.0 * f, sr) + .16f * o5.next (f5, sr);
    s = lp.process (lp2.process (s));
    const float mov = .92f + .08f * lfo.next (2.8, sr);
    return s * mov * g.at (t);
}

// ---------------------------------------------------------------- CRADLE (Subwoofer Lullaby)
Green::Green (double hz, float v, double s, Rng& rng, int midi) : Inst (s), f (hz), r (&rng)
{
    static const float H[11][2] = { {1,1},{2,.13f},{3,.06f},{4,.04f},{5,.035f},{6,.03f},{7,.028f},{8,.024f},{10,.02f},{12,.016f},{15,.012f} };
    out2 = v * .17f;
    for (int i = 0; i < 11; ++i)
    {
        og[(size_t) i].set (0, 0); og[(size_t) i].lin (H[i][1], .04);
        og[(size_t) i].target (0, .04, 1.45 / (1.0 + i * (i > 5 ? 3.0 : .6)));
        fr[(size_t) i] = f * H[i][0] * cents (i * 1.5);
    }
    lpA.setLP (3800, .6, sr); lpB.setLP (3800, .6, sr); bp.setBP (2600, .8, sr);
    ng.set (.11f, 0); ng.exp (.0005f, .07);
    subAmt = (float) (std::clamp ((66.0 - midi) / 18.0, 0.0, 1.0) * std::clamp ((f / 2.0 - 50.0) / 15.0, 0.0, 1.0));
    if (subAmt > 0) { sg.set (0, 0); sg.lin (.55f * subAmt, .08); sg.target (0, .08, 1.8); }
    stopAt = 8.0;
}
float Green::tick (double t)
{
    float s = 0;
    for (size_t i = 0; i < 11; ++i) s += os[i].next (fr[i], sr) * og[i].at (t);
    s = lpB.process (lpA.process (s));
    if (t < .08) s += bp.process (r->bipolar()) * ng.at (t);           // felt hammer
    if (subAmt > 0) s += sub.next (f * .5, sr) * sg.at (t);             // the "subwoofer"
    return s * out2;
}

// ---------------------------------------------------------------- EMBER
Red::Red (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .075f, .25); g.target (v * .075f * .85f, .25, .3);
    lpf.set (350, 0); lpf.lin (800, .6);
    lp.setLP (350, 1, sr); fa = f * cents (-8); fb = f * cents (8);
}
float Red::tick (double t)
{
    if ((cnt++ & 7) == 0) lp.setLP (lpf.at (t), 1, sr);
    const float s = lp.process (s1.next (fa, sr) + s2.next (fb, sr)) + .6f * sb.next (f * .5, sr);
    return s * g.at (t);
}

// ---------------------------------------------------------------- AMETHYST
Purple::Purple (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .1f, .008); g.exp (.0004f, 3.2);
    mg.set ((float) (f * 1.3), 0); mg.exp ((float) (f * .04), 1.2);
    bl.setLP (3000, .5, sr); stopAt = 3.1;
}
float Purple::tick (double t)
{
    const double mod = mo.next (f * 3.5, sr) * mg.at (t);
    return bl.process (c.next (f + mod, sr)) * g.at (t);
}

// ---------------------------------------------------------------- VOID
Black::Black (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .09f, .9); g.target (v * .09f * .9f, .9, .6);
    lp.setLP (480, .7, sr); fa = f * cents (-6); fb = f * cents (6);
}
float Black::tick (double t)
{
    const float s = lp.process (s1.next (fa, sr) + s2.next (fb, sr)) + .7f * sb.next (f * .5, sr);
    return s * g.at (t);
}

// ---------------------------------------------------------------- SNOW
White::White (double hz, float v, double s, Rng&, int) : Inst (s)
{
    static const float P[3][3] = { {1, 1, 2.2f}, {2.76f, .18f, .5f}, {5.4f, .05f, .22f} };
    const double f2 = hz * 2.0;
    for (size_t i = 0; i < 3; ++i) { og[i].set (0, 0); og[i].lin (P[i][1], .002); og[i].exp (.0004f, P[i][2]); this->fr[i] = f2 * P[i][0]; }
    gain = v * .12f; lp.setLP (4200, .5, sr); stopAt = 1.9;
}
float White::tick (double t)
{
    float s = 0;
    for (size_t i = 0; i < 3; ++i) s += os[i].next (fr[i], sr) * og[i].at (t);
    return lp.process (s * gain);
}

// ---------------------------------------------------------------- CRACKED
Broken::Broken (double hz, float v, double s, Rng& rng, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .1f, .005); g.exp (.0004f, 1.7);
    gate.reset (1.0f);
    lp.setLP (2300, .8, sr);
    const float off = (rng.next01() - .5f) * 60.0f;
    det.set (off - 220.0f, 0); det.lin (off, .07);
    lfoRate = 4.5 + rng.next01() * 3.0;
    for (int k = 0; k < 3; ++k)
        if (rng.next01() < .55f)
        {
            const double st = .12 + rng.next01() * 1.1;
            gate.set (0, st); gate.set (1, st + .03 + rng.next01() * .05);
        }
    stopAt = 1.8;
}
float Broken::tick (double t)
{
    const double fr = f * cents (det.at (t) + 28.0 * lfo.next (lfoRate, sr));
    float s = (o1.next (fr, sr) + .25f * o2.next (fr, sr)) * .9f;
    s = lp.process (crush (s));
    return s * gate.at (t) * g.at (t);
}

// ---------------------------------------------------------------- LAGOON
Blue::Blue (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .13f, .005); g.exp (.0004f, 4.5);
    mg.set ((float) (f * 1.3), 0); mg.exp ((float) (f * .12), 1.3);
    lp.setLP (2300, .6, sr); f2 = f * cents (9); stopAt = 4.6;
}
float Blue::tick (double t)
{
    const double mod = mo.next (f, sr) * mg.at (t);
    float s = c1.next (f + mod, sr) + .6f * c2.next (f2 + mod, sr);
    s = lp.process (s);
    const float trem = .78f + .22f * tl.next (4.3, sr);
    return s * trem * g.at (t);
}

// ---------------------------------------------------------------- AMBER
Orange::Orange (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .08f, .035); g.target (v * .08f * .8f, .035, .25);
    lpf.set (300, 0); lpf.lin (2200, .09); lpf.target (1000, .09, .3);
    vib.set (0, 0); vib.lin (14, .7);
    lp.setLP (300, 1.5, sr);
}
float Orange::tick (double t)
{
    if ((cnt++ & 7) == 0) lp.setLP (lpf.at (t), 1.5, sr);
    const double v = vib.at (t) * lfo.next (5.2, sr);
    const float s = s1.next (f * cents (-7 + v), sr) + s2.next (f * cents (7 + v), sr) + .3f * sq.next (f * .5, sr);
    return lp.process (s) * g.at (t);
}

// ---------------------------------------------------------------- SPIRAL
Swirl::Swirl (double hz, float v, double s, Rng&, int) : Inst (s), f (hz)
{
    g.set (0, 0); g.lin (v * .06f, .12); g.target (v * .06f * .9f, .12, .3);
    lp.setLP (1100, 3, sr);
}
float Swirl::tick (double t)
{
    const double flv = fl.next (2.2, sr);
    if ((cnt++ & 7) == 0) lp.setLP (1100.0 + 600.0 * flv, 3, sr);
    const double p = 18.0 * pl.next (.35, sr);
    const float s = saws[0].next (f * cents (-14 + p), sr) + saws[1].next (f, sr) + saws[2].next (f * cents (14 + p), sr);
    return lp.process (s) * g.at (t);
}

// ---------------------------------------------------------------- factory
Inst* makeInstrument (AnyInst& slot, int disc, int midi, float vel, double sr, Rng& rng)
{
    const double f = mtof (midi);
    switch (disc)
    {
        case 0: return &slot.emplace<Yellow> (f, vel, sr, rng, midi);
        case 1: return &slot.emplace<Green>  (f, vel, sr, rng, midi);
        case 2: return &slot.emplace<Red>    (f, vel, sr, rng, midi);
        case 3: return &slot.emplace<Purple> (f, vel, sr, rng, midi);
        case 4: return &slot.emplace<Black>  (f, vel, sr, rng, midi);
        case 5: return &slot.emplace<White>  (f, vel, sr, rng, midi);
        case 6: return &slot.emplace<Broken> (f, vel, sr, rng, midi);
        case 7: return &slot.emplace<Blue>   (f, vel, sr, rng, midi);
        case 8: return &slot.emplace<Orange> (f, vel, sr, rng, midi);
        case 9: return &slot.emplace<Swirl>  (f, vel, sr, rng, midi);
        default: slot.emplace<std::monostate>(); return nullptr;
    }
}
} // namespace jb
