#include "VoxelNotes.h"

namespace ui
{
static const char* kShape[] = { "...XXX.", "...XXXX", "...X..X", "...X...", "...X...", ".XXX...", "XXXX...", ".XX...." };

VoxelNotes::VoxelNotes()
{
    const int H = 8, W = 7; const float D = 1.6f;
    auto on = [&] (int x, int y) { return y >= 0 && y < H && x >= 0 && x < W && kShape[y][x] == 'X'; };
    auto quad = [&] (std::initializer_list<std::array<float, 3>> vs, std::array<float, 3> n)
    {
        Quad q {}; int i = 0;
        for (auto& v : vs) { q.v[i][0] = v[0]; q.v[i][1] = v[1]; q.v[i][2] = v[2]; ++i; }
        q.n[0] = n[0]; q.n[1] = n[1]; q.n[2] = n[2];
        mesh.push_back (q);
    };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (on (x, y))
            {
                const float x0 = x - W / 2.0f, x1 = x0 + 1, y0 = H / 2.0f - y, y1 = y0 - 1, z0 = -D / 2, z1 = D / 2;
                quad ({ {x0,y1,z1},{x1,y1,z1},{x1,y0,z1},{x0,y0,z1} }, { 0, 0, 1 });
                quad ({ {x1,y1,z0},{x0,y1,z0},{x0,y0,z0},{x1,y0,z0} }, { 0, 0, -1 });
                if (! on (x, y - 1)) quad ({ {x0,y0,z1},{x1,y0,z1},{x1,y0,z0},{x0,y0,z0} }, { 0, 1, 0 });
                if (! on (x, y + 1)) quad ({ {x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1} }, { 0, -1, 0 });
                if (! on (x - 1, y)) quad ({ {x0,y1,z0},{x0,y1,z1},{x0,y0,z1},{x0,y0,z0} }, { -1, 0, 0 });
                if (! on (x + 1, y)) quad ({ {x1,y1,z1},{x1,y1,z0},{x1,y0,z0},{x1,y0,z1} }, { 1, 0, 0 });
            }
}

void VoxelNotes::setCanvasSize (int w, int h)
{
    canvas = juce::Image (juce::Image::ARGB, (w + kPix - 1) / kPix, (h + kPix - 1) / kPix, true);
}

// hue drift + brightness by pitch (same as the preview)
static void shade (juce::Colour c, int m, float out[3])
{
    float h = c.getHue(), s = c.getSaturationHSL(), l = c.getLightness();
    h = std::fmod (h + ((m % 12) / 12.0f - .5f) * .14f + 1.0f, 1.0f);
    l = juce::jlimit (.35f, .85f, l + (m - 60) / 90.0f);
    const auto r = juce::Colour::fromHSL (h, s, l, 1.0f);
    out[0] = r.getFloatRed(); out[1] = r.getFloatGreen(); out[2] = r.getFloatBlue();
}

void VoxelNotes::spawn (float x, float y, juce::Colour c, int m)
{
    if (parts.size() > 40) parts.erase (parts.begin());
    Part p {};
    p.x = x; p.y = y; shade (c, m, p.col);
    p.t0 = juce::Time::getMillisecondCounterHiRes(); p.life = 1900 + rnd.nextFloat() * 700;
    p.dx = (rnd.nextFloat() - .5f) * 110; p.dy = 80 + rnd.nextFloat() * 70; p.sc = 3.3f + rnd.nextFloat() * 1.9f;
    p.spin = (rnd.nextBool() ? -1.0f : 1.0f) * (1.6f + rnd.nextFloat() * 1.6f); p.r0 = rnd.nextFloat() * 6.28f; p.tilt = (rnd.nextFloat() - .5f) * .5f;
    parts.push_back (p);
}

static float bayer (int x, int y)
{
    auto b2 = [] (int qx, int qy) { return (2 * qy + 3 * qx) % 4; };
    return (4.0f * b2 (x & 1, y & 1) + b2 ((x >> 1) & 1, (y >> 1) & 1) + .5f) / 16.0f;
}

void VoxelNotes::fillTri (juce::Image::BitmapData& bd, juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::PixelARGB colour, float alpha)
{
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::abs (area) < 1.0e-4f) return;
    const int x0 = juce::jmax (0, (int) std::floor (juce::jmin (a.x, b.x, c.x))), x1 = juce::jmin (bd.width - 1,  (int) std::ceil (juce::jmax (a.x, b.x, c.x)));
    const int y0 = juce::jmax (0, (int) std::floor (juce::jmin (a.y, b.y, c.y))), y1 = juce::jmin (bd.height - 1, (int) std::ceil (juce::jmax (a.y, b.y, c.y)));
    const float sgn = area > 0 ? 1.0f : -1.0f;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const float px = x + .5f, py = y + .5f;
            const float w0 = ((b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x)) * sgn;
            const float w1 = ((c.x - b.x) * (py - b.y) - (c.y - b.y) * (px - b.x)) * sgn;
            const float w2 = ((a.x - c.x) * (py - c.y) - (a.y - c.y) * (px - c.x)) * sgn;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if (alpha < bayer (x, y)) continue;                         // dithered fade
            *reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y)) = colour;
        }
}

void VoxelNotes::render (double now)
{
    canvas.clear (canvas.getBounds());
    if (parts.empty()) return;
    juce::Image::BitmapData bd (canvas, juce::Image::BitmapData::writeOnly);
    const float L[3] = { -.45f / 1.0559f, .75f / 1.0559f, .6f / 1.0559f };
    std::vector<Face> faces; faces.reserve (mesh.size());

    for (int i = (int) parts.size() - 1; i >= 0; --i)
    {
        auto& q = parts[(size_t) i];
        const float k = (float) ((now - q.t0) / q.life);
        if (k >= 1.0f) { parts.erase (parts.begin() + i); continue; }
        if (k < 0.0f) continue;
        const float e = 1.0f - std::pow (1.0f - k, 2.2f);
        const float pop = k < .12f ? (k / .12f) * 1.15f : (k < .2f ? 1.15f - (k - .12f) / .08f * .15f : 1.0f);
        const float ay = q.r0 + q.spin * k * 3.0f, ax = q.tilt + std::sin (k * 6.0f) * .25f;
        const float cy = std::cos (ay), sy = std::sin (ay), cx = std::cos (ax), sx = std::sin (ax);
        // columns of Ry * Rx
        const float C0[3] = { cy, sx * sy, -cx * sy }, C1[3] = { 0, cx, sx }, C2[3] = { sy, -sx * cy, cx * cy };
        auto rot = [&] (const float v[3], float o[3]) { for (int j = 0; j < 3; ++j) o[j] = C0[j] * v[0] + C1[j] * v[1] + C2[j] * v[2]; };
        const float cxp = q.x + q.dx * e + std::sin (k * 7.0f + q.r0) * 8.0f, cyp = q.y - q.dy * e;
        const float s = q.sc * pop;
        const float alpha = k < .55f ? 1.0f : 1.0f - (k - .55f) / .45f;

        faces.clear();
        for (const auto& m : mesh)
        {
            float n[3]; rot (m.n, n);
            if (n[2] <= 0.0f) continue;                                // back-face cull
            Face f {}; f.nx = n[0]; f.ny = n[1]; f.nz = n[2];
            for (int v = 0; v < 4; ++v)
            {
                const float sv[3] = { m.v[v][0] * s, m.v[v][1] * s, m.v[v][2] * s };
                float r[3]; rot (sv, r);
                const float persp = 260.0f / (260.0f - r[2]);
                float px = (cxp + r[0] * persp) / kPix, py = (cyp - r[1] * persp) / kPix;
                px = std::round (px / 2.0f) * 2.0f; py = std::round (py / 2.0f) * 2.0f;   // PS1 vertex snapping
                f.p[v] = { px, py }; f.z[v] = r[2];
            }
            faces.push_back (f);
        }
        std::sort (faces.begin(), faces.end(), [] (const Face& a, const Face& b)
                   { return a.z[0] + a.z[1] + a.z[2] + a.z[3] < b.z[0] + b.z[1] + b.z[2] + b.z[3]; });
        for (const auto& f : faces)
        {
            float l = std::max (0.0f, f.nx * L[0] + f.ny * L[1] + f.nz * L[2]);
            l = std::floor ((0.58f + 0.6f * l) * 4.0f) / 4.0f;                    // banded light
            auto post = [] (float c) { return (juce::uint8) juce::jlimit (0, 255, juce::roundToInt (std::floor (juce::jlimit (0.0f, 1.0f, c) * 31.0f + .5f) / 31.0f * 255.0f)); };
            juce::PixelARGB px (255, post (q.col[0] * l), post (q.col[1] * l), post (q.col[2] * l));
            fillTri (bd, f.p[0], f.p[1], f.p[2], px, alpha);
            fillTri (bd, f.p[0], f.p[2], f.p[3], px, alpha);
        }
    }
}
} // namespace ui
