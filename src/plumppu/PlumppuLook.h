#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// The Plumppu house style, shared by all Plumppu plugins.
//
// A white powder-coated aluminium front plate carrying the Finnish flag as
// its signature: a blue Nordic cross painted on, offset towards the left like
// on the flag, with the plugin's main control sitting on the intersection.
// The plate's edge is diamond cut, showing bare aluminium, and all legends
// are engraved and paint filled. Controls are black bakelite. There is no
// wear.
//
// Nothing is drawn with flat gradients. Each part is described as a small
// relief (a height map) plus a material, and a software renderer lights it
// per pixel: one large soft window up and to the left that casts penumbral
// shadows, ambient occlusion in the hollows, and Fresnel reflections of two
// softboxes on every glossy surface. Printed legends are ink on the
// surface, so they pick up the same light and sheen as the plastic under them.
//
// To use it in another plugin: add this folder and the fonts in assets/fonts
// (see CMakeLists.txt for the binary data target), build the static parts once
// into a Surface and render() it to an image, and give rotary sliders the
// KnobLook.
namespace plumppu
{
namespace colours
{
const juce::Colour flagBlue { 0xff003580 };  // official Finnish flag blue
const juce::Colour enamelWhite { 0xfff4f2ea };
const juce::Colour ivory { 0xffe9e2d0 };       // cream bakelite of the panel
const juce::Colour engraving { 0xff14233d };  // printed legends on the panel
const juce::Colour bakelite { 0xff1a1817 };   // black bakelite of the controls
}

// B612 for labels and small text, B612 Mono for numeric readouts.
juce::Font labelFont (float height, bool bold = false, float tracking = 0.0f);
juce::Font readoutFont (float height);

// How a surface reflects light. reflectance is the specular reflectance seen
// head on (about 0.04 for plastics, much higher for metal), roughness runs
// from 0 (mirror) to 1 (matte).
struct Material
{
    juce::Colour colour;
    float reflectance = 0.045f;
    float roughness = 0.45f;
    bool metal = false;
};

namespace materials
{
inline const Material chassis { juce::Colour (0xff0e0d0d), 0.04f, 0.6f };
inline const Material ivory { colours::ivory, 0.045f, 0.38f };
inline const Material whiteCoat { juce::Colour (0xffe9e9e6), 0.045f, 0.40f };
inline const Material dialPlate { juce::Colour (0xfff4f2ec), 0.045f, 0.30f };
inline const Material flagBlue { colours::flagBlue, 0.045f, 0.38f };
inline const Material bluePaint { colours::flagBlue, 0.045f, 0.34f };
inline const Material engravingFill { juce::Colour (0xff1b2638), 0.035f, 0.70f };
inline const Material aluminium { juce::Colour (0xffd2d4d6), 0.9f, 0.14f, true };
inline const Material ink { colours::engraving, 0.035f, 0.75f };
inline const Material blueInk { colours::flagBlue, 0.035f, 0.70f };
inline const Material whiteInk { colours::enamelWhite, 0.035f, 0.75f };
inline const Material bakelite { juce::Colour (0xff121110), 0.05f, 0.20f };
inline const Material pointer { juce::Colour (0xfff1ecdf), 0.045f, 0.45f };
inline const Material whiteEnamel { juce::Colour (0xfff2f1ed), 0.05f, 0.10f };
inline const Material whiteMetalPaint { juce::Colour (0xffeeeeeb), 0.045f, 0.30f };
inline const Material brushedBlue { colours::flagBlue, 0.05f, 0.16f };
inline const Material spunAluminium { juce::Colour (0xffc4c6c9), 0.9f, 0.30f, true };
inline const Material blackNickel { juce::Colour (0xff4a4c50), 0.9f, 0.16f, true };
inline const Material steel { juce::Colour (0xffa3a19c), 0.6f, 0.30f, true };
inline const Material displayGlass { juce::Colour (0xff050d1e), 0.045f, 0.05f };
}

// A scalar image in device pixels.
struct Map
{
    Map() = default;
    Map (int w, int h, float value = 0.0f) : width (w), height (h), data ((size_t) (w * h), value) {}

    float& operator() (int x, int y) { return data[(size_t) (y * width + x)]; }
    float operator() (int x, int y) const { return data[(size_t) (y * width + x)]; }
    float clamped (int x, int y) const;

    int width = 0, height = 0;
    std::vector<float> data;
};

// Gaussian blur (three box passes); sigma in pixels.
void blur (Map&, float sigma);
void blur (Map&, float sigmaX, float sigmaY);

// A relief with materials, lit and rendered per pixel. All coordinates and
// heights passed in are logical (editor) units; the surface stores them at
// `scale` pixels per unit.
class Surface
{
public:
    Surface (juce::Rectangle<float> area, float scale);

    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    float getScale() const noexcept { return scale; }

    // Logical position of the centre of a pixel.
    juce::Point<float> positionOf (int px, int py) const noexcept;

    // Calls fn (px, py, position) for every pixel touching a logical area.
    void forEach (juce::Rectangle<float> area, const std::function<void (int, int, juce::Point<float>)>& fn);

    // Coverage of whatever draw() fills, in logical coordinates.
    Map mask (const std::function<void (juce::Graphics&)>& draw) const;

    // Lays a material wherever coverage is non-zero.
    void apply (const Map& coverage, const Material&);
    void paint (const std::function<void (juce::Graphics&)>& draw, const Material& m) { apply (mask (draw), m); }
    void setMaterial (int px, int py, const Material&, float coverage);

    // Raises the covered area by `amount`, with edges rounded over `softness`.
    void raise (const Map& coverage, float amount, float softness);

    float& heightAt (int px, int py) { return heights (px, py); }
    float& alphaAt (int px, int py) { return alpha (px, py); }
    void addEmission (int px, int py, float r, float g, float b);
    void scaleColour (int px, int py, float factor);

    juce::Image render (bool castShadows, bool occlusion) const;

private:
    juce::Point<float> origin;
    float scale;
    int width, height;
    Map heights, alpha, red, green, blue, reflectance, roughness, metal;
    Map emitRed, emitGreen, emitBlue;
};

struct PanelLayout
{
    juce::Point<float> crossCentre;  // where the two blue bands meet
    float bandThickness = 50.0f;
};

// Ivory panel with the blue cross and corner screws.
void buildPanel (Surface&, juce::Rectangle<float> bounds, const PanelLayout&);

// Spun aluminium scale plate for a rotary control. Labels are spread evenly
// from startAngle to endAngle; minorPerMajor ticks sit between two labels.
void buildDialPlate (Surface&, juce::Point<float> centre, float radius,
                     float startAngle, float endAngle,
                     const juce::StringArray& labels, int minorPerMajor = 5);

void buildScrew (Surface&, juce::Point<float> centre, float radius, float slotAngle);

// Pad-printed text: a thin film of ink lying on the surface.
void printText (Surface&, const juce::String&, const juce::Font&, juce::Rectangle<float> area,
                juce::Justification, const Material& ink = materials::ink);

// Lettering engraved `depth` into the surface and filled with paint.
void engraveText (Surface&, const juce::String&, const juce::Font&, juce::Rectangle<float> area,
                  juce::Justification, const Material& fill = materials::engravingFill, float depth = 0.6f);
void engrave (Surface&, const Map& coverage, const Material& fill, float depth);

// A thin layer of paint with crisp edges.
void paintOn (Surface&, const Map& coverage, const Material&);

// Lettering moulded in relief, standing `height` proud of the surface.
void mouldText (Surface&, const juce::String&, const juce::Font&, juce::Rectangle<float> area,
                juce::Justification, const Material&, float height);

// A raised plate with rounded edges.
void buildPlate (Surface&, juce::Rectangle<float> area, float corner, float height, const Material&);

// Bakelite bezel around a recessed display window; the value is drawn into the
// window at run time with drawReadout().
void buildReadoutBezel (Surface&, juce::Rectangle<float> area);
void drawReadout (juce::Graphics&, juce::Rectangle<float> area, const juce::String& text);

// Bakelite sockets for a row of jewel lamps; the jewels are drawn at run time
// with drawLampRow(), where level runs from 0 (all dark) to lampCount (all lit).
void buildLampSockets (Surface&, juce::Rectangle<float> area, int lampCount);
void drawLampRow (juce::Graphics&, juce::Rectangle<float> area, int lampCount, float level);

// Black Meccano gear wheel with a brass dome nut, used as a knob. The slider's bounds must leave
// knobShadowMargin around the knob for its shadow.
constexpr float knobShadowMargin = 30.0f;

class KnobLook final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle, juce::Slider&) override;

private:
    juce::Image cached;
    float cachedAngle = -1000.0f, cachedScale = 0.0f;
    juce::Rectangle<int> cachedBounds;
};
}
