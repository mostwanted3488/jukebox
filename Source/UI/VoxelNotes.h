#pragma once
// PS1-style low-poly voxel music notes: extruded pixel note, rendered in software at 1/3
// resolution with vertex snapping, banded flat lighting, 15-bit colour and dithered fade.
#include <juce_gui_basics/juce_gui_basics.h>

namespace ui
{
class VoxelNotes
{
public:
    VoxelNotes();
    void setCanvasSize (int designW, int designH);              // design-space size of the overlay
    void spawn (float x, float y, juce::Colour c, int midiNote);
    void render (double nowMs);                                  // draws into the low-res image
    const juce::Image& image() const { return canvas; }
    int pixel() const { return kPix; }
    bool busy() const { return ! parts.empty(); }

private:
    static constexpr int kPix = 3;
    struct Face { juce::Point<float> p[4]; float z[4]; float nx, ny, nz; };
    struct Quad { float v[4][3]; float n[3]; };
    struct Part { float x, y; float col[3]; double t0, life; float dx, dy, sc, spin, r0, tilt; };

    std::vector<Quad> mesh;
    std::vector<Part> parts;
    juce::Image canvas;
    juce::Random rnd;

    void fillTri (juce::Image::BitmapData& bd, juce::Point<float> a, juce::Point<float> b, juce::Point<float> c, juce::PixelARGB colour, float alpha);
};
} // namespace ui
