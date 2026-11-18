#include "Scene.h"

namespace ui
{
// CSS cubic-bezier easing (same curves as the preview animations)
static float cubicBezier (float x1, float y1, float x2, float y2, float x)
{
    auto bx = [&] (float t) { const float u = 1 - t; return 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t; };
    auto by = [&] (float t) { const float u = 1 - t; return 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t; };
    float lo = 0, hi = 1, t = x;
    for (int i = 0; i < 30; ++i) { t = (lo + hi) * .5f; if (bx (t) < x) lo = t; else hi = t; }
    return by (t);
}

struct Key { float off, tx, ty, rot, sx, sy, a; };
static Key lerpKeys (const std::vector<Key>& k, float t)
{
    for (size_t i = 1; i < k.size(); ++i)
        if (t <= k[i].off)
        {
            const auto& a = k[i - 1]; const auto& b = k[i];
            const float u = (t - a.off) / std::max (1.0e-4f, b.off - a.off);
            auto L = [u] (float p, float q) { return p + (q - p) * u; };
            return { t, L (a.tx, b.tx), L (a.ty, b.ty), L (a.rot, b.rot), L (a.sx, b.sx), L (a.sy, b.sy), L (a.a, b.a) };
        }
    return k.back();
}

Scene::Scene (JukeboxProcessor& p) : proc (p)
{
    juke = juce::ImageCache::getFromMemory (BinaryData::jukebox_png, BinaryData::jukebox_pngSize);
    const void* data[] = { BinaryData::disc_yellow_png, BinaryData::disc_green_png, BinaryData::disc_red_png, BinaryData::disc_purple_png, BinaryData::disc_black_png,
                           BinaryData::disc_white_png, BinaryData::disc_broken_png, BinaryData::disc_blue_png, BinaryData::disc_orange_png, BinaryData::disc_swirl_png };
    const int sizes[] = { BinaryData::disc_yellow_pngSize, BinaryData::disc_green_pngSize, BinaryData::disc_red_pngSize, BinaryData::disc_purple_pngSize, BinaryData::disc_black_pngSize,
                          BinaryData::disc_white_pngSize, BinaryData::disc_broken_pngSize, BinaryData::disc_blue_pngSize, BinaryData::disc_orange_pngSize, BinaryData::disc_swirl_pngSize };
    for (int i = 0; i < jb::kNumDiscs; ++i) discImgs[(size_t) i] = juce::ImageCache::getFromMemory (data[i], sizes[i]);

    for (int i = 0; i < jb::kNumDiscs; ++i)
        slots[(size_t) i] = { 48.0f + (i % 5) * 80.0f, 513.0f + (i / 5) * 70.0f, 64.0f, 64.0f };

    buildMasks();
    buildRedstone();
    shown = (int) proc.apvts.getRawParameterValue (ParamIDs::disc)->load() - 1;
    leverAngle = leverOn() ? 40.0f : -40.0f;
}

bool Scene::leverOn() const { return proc.apvts.getRawParameterValue (ParamIDs::lever)->load() > 0.5f; }

juce::Colour Scene::tint() const { return shown >= 0 ? juce::Colour (jb::discs()[(size_t) shown].colour) : col::neutral; }

// ----------------------------------------------------------------------------- slot glow masks
void Scene::buildMasks()
{
    const int W = juke.getWidth(), H = juke.getHeight();
    std::vector<int> dark ((size_t) (W * H)), opaque ((size_t) (W * H)), slot ((size_t) (W * H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const auto c = juke.getPixelAt (x, y);
            const bool op = c.getAlpha() > 200;
            opaque[(size_t) (y * W + x)] = op;
            dark[(size_t) (y * W + x)] = op && (c.getRed() + c.getGreen() + c.getBlue()) < 90;
            slot[(size_t) (y * W + x)] = dark[(size_t) (y * W + x)] && y >= 45 && y < 92 && x >= 100 && x < 200;
        }
    // Manhattan distance to the slot (4-neighbour dilation), up to 7 px
    std::vector<int> dist ((size_t) (W * H), 99);
    std::vector<int> q; q.reserve (4096);
    for (int i = 0; i < W * H; ++i) if (slot[(size_t) i]) { dist[(size_t) i] = 0; q.push_back (i); }
    for (size_t h = 0; h < q.size(); ++h)
    {
        const int i = q[h], x = i % W, y = i / W, d = dist[(size_t) i];
        if (d >= 7) continue;
        const int nb[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
        for (auto& o : nb)
        {
            const int nx = x + o[0], ny = y + o[1];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
            const int j = ny * W + nx;
            if (dist[(size_t) j] > d + 1) { dist[(size_t) j] = d + 1; q.push_back (j); }
        }
    }
    slotMask.assign ((size_t) (W * H), 0); rimMask.assign ((size_t) (W * H), 0);
    auto S = [&] (int x, int y) { return y >= 0 && slot[(size_t) (y * W + x)]; };
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const size_t i = (size_t) (y * W + x);
            if (slot[i]) slotMask[i] = S (x, y - 6) ? 255 : (S (x, y - 3) ? 170 : 70);     // dark top wall, light below
            const int d = dist[i];
            if (d >= 2 && d <= 7 && opaque[i] && ! dark[i] && y < 110) rimMask[i] = d <= 3 ? 200 : 90;
        }
}

void Scene::rebuildGlow (juce::Colour c)
{
    glowTint = c;
    const int W = juke.getWidth(), H = juke.getHeight();
    slotGlow = juce::Image (juce::Image::ARGB, W, H, true);
    rimGlow  = juce::Image (juce::Image::ARGB, W, H, true);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const size_t i = (size_t) (y * W + x);
            if (slotMask[i]) slotGlow.setPixelAt (x, y, c.withAlpha (slotMask[i]));
            if (rimMask[i])  rimGlow.setPixelAt  (x, y, c.withAlpha (rimMask[i]));
        }
}

// ----------------------------------------------------------------------------- redstone layout
void Scene::buildRedstone()
{
    juce::Random r (4242);
    for (int i = 0; i < 19; ++i)
    {
        const float x = 80 - i * 7.0f, y = 270 + i * 3.5f;
        const float offs[3][2] = { { -5, -2.5f }, { 0, 0 }, { 5, 2.5f } };
        for (int j = 0; j < 3; ++j)
        {
            if (j != 1 && r.nextFloat() < .3f) continue;
            const int kind = r.nextFloat() < .25f ? 1 : (r.nextFloat() < .3f ? 2 : 0);
            dust.push_back ({ { juce::roundToInt (x + offs[j][0]), juce::roundToInt (y + offs[j][1]), 7, 5 }, kind });
        }
        if (r.nextFloat() < .5f) dust.push_back ({ { juce::roundToInt (x + r.nextFloat() * 16 - 8), juce::roundToInt (y + 5 + r.nextFloat() * 4), 4, 3 }, 2 });
        if (i % 2 == 1) sparks.push_back ({ juce::roundToInt (x + 1), juce::roundToInt (y - 7 - r.nextFloat() * 4), 4, 4 });
    }
    sparkOn.assign (sparks.size(), false);

    // cobblestone base (iso slab)
    const float cx = -50, cy = 342, HW = 24, HD = 12, HT = 13;
    baseTop.addTriangle (cx - HW, cy, cx, cy - HD, cx + HW, cy); baseTop.addTriangle (cx - HW, cy, cx + HW, cy, cx, cy + HD);
    baseLeft.startNewSubPath (cx - HW, cy); baseLeft.lineTo (cx, cy + HD); baseLeft.lineTo (cx, cy + HD + HT); baseLeft.lineTo (cx - HW, cy + HT); baseLeft.closeSubPath();
    baseRight.startNewSubPath (cx + HW, cy); baseRight.lineTo (cx, cy + HD); baseRight.lineTo (cx, cy + HD + HT); baseRight.lineTo (cx + HW, cy + HT); baseRight.closeSubPath();
    auto pick = [&] (std::initializer_list<juce::uint32> cs) { auto it = cs.begin(); std::advance (it, r.nextInt ((int) cs.size())); return juce::Colour (*it); };
    for (int y = (int) (cy - HD); y < cy + HD; y += 4) for (int x = (int) (cx - HW); x < cx + HW; x += 4)
        basePixels.push_back ({ { x, y, 4, 4 }, pick ({ 0xff9a9a9a, 0xff8a8a8a, 0xff7a7a7a, 0xff6a6a6a, 0xff5c5c5c, 0xffadadad, 0xff4e4e4e }), 0 });
    for (int y = (int) (cy - 2); y < cy + HD + HT; y += 4) for (int x = (int) (cx - HW); x < cx; x += 4)
        basePixels.push_back ({ { x, y, 4, 4 }, pick ({ 0xff3d3d3d, 0xff353535, 0xff2c2c2c, 0xff454545, 0xff262626 }), 1 });
    for (int y = (int) (cy - 2); y < cy + HD + HT; y += 4) for (int x = (int) cx; x < cx + HW; x += 4)
        basePixels.push_back ({ { x, y, 4, 4 }, pick ({ 0xff2e2e2e, 0xff272727, 0xff222222, 0xff363636, 0xff1d1d1d }), 2 });

    // stick with stone tip (local coords, pivot at 0,0, pointing up)
    const int L = 52, TIP = 12, W2 = 5;
    for (int y = 0; y < L; y += 4)
    {
        const bool tip = y >= L - TIP;
        stickPixels.push_back ({ { -W2, -y - 4, 4, 4 }, tip ? pick ({ 0xffa6a6a6, 0xff9c9c9c, 0xffb0b0b0 }) : pick ({ 0xff9a7343, 0xff8f6a3c, 0xffa07a48 }), 0 });
        stickPixels.push_back ({ { -W2 + 4, -y - 4, 2, 4 }, tip ? juce::Colour (0xff8f8f8f) : pick ({ 0xff7a5a33, 0xff715230 }), 0 });
        stickPixels.push_back ({ { -W2 + 6, -y - 4, 4, 4 }, tip ? pick ({ 0xff7e7e7e, 0xff747474, 0xff868686 }) : pick ({ 0xff5c4022, 0xff654727, 0xff523a1f }), 0 });
    }
    stickPixels.push_back ({ { -W2, -L - 3, 10, 3 }, juce::Colour (0xffc4c4c4), 0 });
    stickPixels.push_back ({ { -W2 + 5, -L - 3, 5, 3 }, juce::Colour (0xffa3a3a3), 0 });
    stickPixels.push_back ({ { -W2, -3, 10, 3 }, juce::Colour (0x8c2a1d10), 0 });
}

void Scene::drawRedstone (juce::Graphics& g, bool on, bool lit)
{
    static const juce::uint32 palette[3][3] = {
        { 0xff4d0a06, 0xff6b120c, 0xff330604 },   // off
        { 0xff9e140c, 0xffc41b10, 0xff6e0d08 },   // on
        { 0xffff2a1a, 0xffff8566, 0xffc4130c } }; // lit (pulse)
    const int set = ! on ? 0 : (lit ? 2 : 1);
    for (const auto& d : dust) { g.setColour (juce::Colour (palette[set][d.kind])); g.fillRect (d.r); }
    if (on && lit)
        for (size_t i = 0; i < sparks.size(); ++i) if (sparkOn[i]) { g.setColour (juce::Colour (0xffff4a2e)); g.fillRect (sparks[i]); }

    // lever base
    for (int part = 0; part < 3; ++part)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (part == 0 ? baseTop : (part == 1 ? baseLeft : baseRight));
        for (const auto& p : basePixels) if (p.clip == part) { g.setColour (p.c); g.fillRect (p.r); }
    }
    g.setColour (juce::Colour (0x59b5b5b5));
    g.drawLine (-74, 342, -50, 330, 1.0f); g.drawLine (-50, 330, -26, 342, 1.0f);

    // handle
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (leverAngle)).translated (-50.0f, 344.0f));
        for (const auto& p : stickPixels) { g.setColour (p.c); g.fillRect (p.r); }
    }
}

// ----------------------------------------------------------------------------- frame update
void Scene::tick (double nowMs)
{
    now = nowMs;
    const float target = leverOn() ? 40.0f : -40.0f;
    if (std::abs (leverAngle - target) > .01f) leverAngle += juce::jlimit (-27.0f, 27.0f, target - leverAngle);   // 3-step flip

    const bool lit = leverOn() && proc.engine.gateLit.load();
    if (lit && ! lastLit) { juce::Random r; for (size_t i = 0; i < sparkOn.size(); ++i) sparkOn[i] = r.nextFloat() < .45f; }
    lastLit = lit;

    if (flight.active)
    {
        const double dur = flight.inserting ? 720.0 : 650.0;
        if (now - flight.t0 >= dur) finishFlight();
    }
    else if (dragDisc < 0) syncWithParameter();

    if (tint() != glowTint || slotGlow.isNull()) rebuildGlow (tint());
}

void Scene::syncWithParameter()
{
    const int p = (int) proc.apvts.getRawParameterValue (ParamIDs::disc)->load() - 1;
    if (p == shown) return;
    if (p >= 0) { if (shown >= 0) startEject (p, slots[(size_t) p], false); else startInsert (p, slots[(size_t) p], false); }
    else startEject (-1, {}, false);
}

void Scene::startInsert (int d, juce::Rectangle<float> from, bool user)
{
    flight = {}; flight.active = true; flight.inserting = true; flight.user = user; flight.disc = d; flight.from = from; flight.t0 = now;
}

void Scene::startEject (int thenInsert, juce::Rectangle<float> thenFrom, bool user)
{
    const int d = shown;
    if (d < 0) { if (thenInsert >= 0) startInsert (thenInsert, thenFrom, user); return; }
    if (user) proc.setDiscFromUI (-1);
    shown = -1;
    flight = {}; flight.active = true; flight.inserting = false; flight.user = user; flight.disc = d; flight.thenInsert = thenInsert;
    flight.from = thenFrom; flight.t0 = now;
}

void Scene::finishFlight()
{
    const auto f = flight;
    flight.active = false;
    if (f.inserting)
    {
        shown = f.disc;
        if (f.user) proc.setDiscFromUI (f.disc);
    }
    else if (f.thenInsert >= 0)
        startInsert (f.thenInsert, f.from.isEmpty() ? slots[(size_t) f.thenInsert] : f.from, f.user);
}

void Scene::bump() { bumpT = now; }
void Scene::nope() { nopeT = now; }

void Scene::toggleLever()
{
    if (auto* p = proc.apvts.getParameter (ParamIDs::lever))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost (leverOn() ? 0.0f : 1.0f); p->endChangeGesture();
    }
}

juce::Point<float> Scene::noteSpawnPoint (juce::Random& r) const
{
    return { stage.getX() + 300.0f * (.4f + r.nextFloat() * .2f), stage.getY() + 300.0f * .17f };
}

// ----------------------------------------------------------------------------- painting
void Scene::drawDisc (juce::Graphics& g, int d, juce::Point<float> c, float size, float rot, float sx, float sy, float alpha)
{
    if (alpha <= 0.0f) return;
    const auto& img = discImgs[(size_t) d];
    const auto t = juce::AffineTransform::scale (size / img.getWidth() * sx, size / img.getHeight() * sy)
                       .translated (-size * sx * .5f, -size * sy * .5f)
                       .rotated (juce::degreesToRadians (rot))
                       .translated (c.x, c.y);
    juce::Graphics::ScopedSaveState s (g);
    g.setOpacity (alpha);
    g.drawImageTransformed (img, t);
}

void Scene::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);   // hard pixels
    const auto tc = tint();

    // title
    g.setColour (col::title); g.setFont (font (32.0f));
    g.drawText ("JUKEBOX", juce::Rectangle<int> (0, 30, 480, 40), juce::Justification::centred);
    g.setColour (col::dim); g.setFont (font (16.0f));
    g.drawText ("drop a disc in - click to eject", juce::Rectangle<int> (0, 70, 480, 22), juce::Justification::centred);

    // halo behind the jukebox
    if (shown >= 0)
    {
        const auto c = stage.getCentre(); const float rad = stage.getWidth() * .62f;
        juce::ColourGradient grad (tc.withAlpha (.27f), c.x, c.y, tc.withAlpha (0.0f), c.x + rad, c.y, true);
        g.setGradientFill (grad); g.fillEllipse (stage.expanded (stage.getWidth() * .2f));
    }

    // redstone + lever (stage coordinates)
    {
        juce::Graphics::ScopedSaveState s (g);
        g.addTransform (juce::AffineTransform::translation (stage.getX(), stage.getY()));
        drawRedstone (g, leverOn(), leverOn() && proc.engine.gateLit.load());
    }

    // jukebox with bump / nope
    {
        juce::Graphics::ScopedSaveState s (g);
        const float kb = (float) juce::jlimit (0.0, 1.0, (now - bumpT) / 220.0), kn = (float) juce::jlimit (0.0, 1.0, (now - nopeT) / 350.0);
        const float e = (1 - kb) * (1 - kb);
        const float shake = kn < 1 ? std::sin (kn * juce::MathConstants<float>::twoPi * 2) * 6.0f : 0.0f;
        const float ox = stage.getX() + 150, oy = stage.getY() + 270;
        g.addTransform (juce::AffineTransform::scale (1 + .035f * e, 1 - .025f * e, ox, oy).translated (shake, 0));
        if (overStage)
        {
            g.setColour (tc.brighter (.4f).withAlpha (.25f));
            g.fillRoundedRectangle (stage.reduced (20), 30);
        }
        g.drawImage (juke, stage, juce::RectanglePlacement::stretchToFit);
        if (shown >= 0 && slotGlow.isValid())
        {
            const float ph = (float) std::fmod (now / 2400.0, 1.0);
            const float stepped = std::floor (ph * 6.0f) / 6.0f;
            const float tri = 1.0f - std::abs (2.0f * stepped - 1.0f);
            g.setOpacity (.75f + .25f * tri); g.drawImage (slotGlow, stage, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (.22f + .16f * tri); g.drawImage (rimGlow, stage, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
    }

    // shelf
    {
        const auto sh = shelf;
        g.setColour (juce::Colour (0xff2a2622)); g.fillRect (sh);
        g.setColour (juce::Colour (0xff3b352f)); g.fillRect (sh.getX() + 4, sh.getY() + 4, sh.getWidth() - 8, 4);
        g.setColour (juce::Colour (0xff1d1a17)); g.fillRect (sh.getX() + 4, sh.getBottom() - 10, sh.getWidth() - 8, 6);
        g.setColour (juce::Colour (0xff121110)); g.drawRect (sh, 4);
    }
    const int flyingDisc = flight.active ? flight.disc : -1;
    for (int i = 0; i < jb::kNumDiscs; ++i)
    {
        const auto r = slots[(size_t) i];
        const bool empty = i == shown || i == flyingDisc || (i == dragDisc && dragMoved);
        if (empty)
        {
            g.setColour (juce::Colour (0xff4a443d));
            const auto b = r.reduced (r.getWidth() * .1f).toNearestInt();
            for (int x = b.getX(); x < b.getRight(); x += 8) { g.fillRect (x, b.getY(), 4, 3); g.fillRect (x, b.getBottom() - 3, 4, 3); }
            for (int y = b.getY(); y < b.getBottom(); y += 8) { g.fillRect (b.getX(), y, 3, 4); g.fillRect (b.getRight() - 3, y, 3, 4); }
            continue;
        }
        const float bob = -3.0f * (.5f - .5f * std::cos ((float) (now / 3000.0 + i * .37) * juce::MathConstants<float>::twoPi));
        const bool h = (i == hover) && dragDisc < 0;
        drawDisc (g, i, r.getCentre().translated (0, bob + (h ? -5.0f : 0.0f)), r.getWidth(), 0, h ? 1.1f : 1.0f, h ? 1.1f : 1.0f, 1.0f);
    }

    // flight animation
    if (flight.active)
    {
        const float raw = (float) juce::jlimit (0.0, 1.0, (now - flight.t0) / (flight.inserting ? 720.0 : 650.0));
        const auto p = slotPoint(); const float size = 300.0f * .42f;
        if (flight.inserting)
        {
            const float t = cubicBezier (.4f, .1f, .3f, 1.0f, raw);
            const auto from = flight.from; const float sx = size / from.getWidth();
            const float tx = p.x - from.getCentreX(), ty = p.y - from.getCentreY();
            const auto k = lerpKeys ({ { 0, 0, 0, 0, 1, 1, 1 }, { .55f, tx, ty - size * .9f, -8, sx, sx, 1 },
                                       { .8f, tx, ty - size * .35f, 9, sx, sx * .22f, 1 }, { 1, tx, ty, 9, sx * .9f, sx * .05f, 0 } }, t);
            drawDisc (g, flight.disc, from.getCentre().translated (k.tx, k.ty), from.getWidth(), k.rot, k.sx, k.sy, k.a);
        }
        else
        {
            const float t = cubicBezier (.3f, .1f, .3f, 1.0f, raw);
            const auto home = slots[(size_t) flight.disc]; const float sx = size / home.getWidth();
            const float tx = p.x - home.getCentreX(), ty = p.y - home.getCentreY();
            const auto k = lerpKeys ({ { 0, tx, ty, 9, sx * .9f, sx * .05f, 0 }, { .25f, tx, ty - size * .5f, 9, sx, sx * .25f, 1 },
                                       { .55f, tx * .5f, ty - size * 1.1f, -180, sx, sx, 1 }, { 1, 0, 0, -360, 1, 1, 1 } }, t);
            drawDisc (g, flight.disc, home.getCentre().translated (k.tx, k.ty), home.getWidth(), k.rot, k.sx, k.sy, k.a);
        }
    }

    // dragged disc
    if (dragDisc >= 0 && dragMoved)
        drawDisc (g, dragDisc, dragPos - dragOffset + juce::Point<float> (32, 32), 64, (dragPos.x - dragStart.x) * .05f, 1.1f, 1.1f, 1.0f);

    // tooltip
    if (hover >= 0 && dragDisc < 0 && hover != shown && hover != flyingDisc)
    {
        const auto r = slots[(size_t) hover];
        const juce::String name (jb::discs()[(size_t) hover].name);
        g.setFont (font (16.0f));
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 16;
        juce::Rectangle<int> tip (juce::roundToInt (r.getCentreX()) - w / 2, (int) r.getY() - 26, w, 24);
        g.setColour (juce::Colour (0xee100f12)); g.fillRect (tip);
        g.setColour (juce::Colour (0xff2b1a4a)); g.drawRect (tip, 2);
        g.setColour (juce::Colours::white); g.drawText (name, tip, juce::Justification::centred);
    }
}

// ----------------------------------------------------------------------------- mouse
int Scene::slotAt (juce::Point<float> p) const
{
    for (int i = 0; i < jb::kNumDiscs; ++i) if (slots[(size_t) i].contains (p)) return i;
    return -1;
}
bool Scene::leverHit (juce::Point<float> p) const
{
    return juce::Rectangle<float> (stage.getX() - 50 - 34, stage.getY() + 342 - 64, 68, 100).contains (p);
}
bool Scene::overJukebox (juce::Point<float> p) const { return stage.expanded (stage.getWidth() * .12f).contains (p); }

void Scene::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    hover = slotAt (p);
    const bool clickable = leverHit (p) || (hover >= 0 && hover != shown) || (shown >= 0 && stage.contains (p));
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void Scene::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (leverHit (p)) { toggleLever(); return; }
    const int s = slotAt (p);
    if (s >= 0 && s != shown && ! flight.active)
    {
        dragDisc = s; dragMoved = false; dragStart = dragPos = p; dragOffset = p - slots[(size_t) s].getPosition();
        return;
    }
    if (stage.contains (p) && shown >= 0 && ! flight.active) startEject (-1, {}, true);
}

void Scene::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDisc < 0) return;
    dragPos = e.position;
    if (! dragMoved && dragPos.getDistanceFrom (dragStart) > 6.0f) dragMoved = true;
    overStage = dragMoved && overJukebox (dragPos);
}

void Scene::mouseUp (const juce::MouseEvent&)
{
    if (dragDisc < 0) return;
    const int d = dragDisc;
    const auto ghost = juce::Rectangle<float> (64, 64).withPosition (dragPos - dragOffset);
    const bool drop = ! dragMoved || overStage;
    dragDisc = -1; overStage = false;
    if (! drop) return;
    const auto from = dragMoved ? ghost : slots[(size_t) d];
    if (shown >= 0) startEject (d, from, true); else startInsert (d, from, true);
}
} // namespace ui
