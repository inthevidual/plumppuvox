#include "Faceplate.h"
#include "Texts.h"
#include "RadiomixData.h"

using namespace plumppu;

namespace radiomix
{
namespace
{
constexpr float W = (float) Faceplate::width, H = (float) Faceplate::height;
constexpr float scale = 2.0f;
const juce::Colour black (0xff020304), cyan (0xff05c1f7), white (0xfffcfcfc);

const juce::Rectangle<float> logoArea { 60.0f, 30.0f, 360.0f, 124.0f };
const juce::Rectangle<float> fileDisplay { 40.0f, 172.0f, 400.0f, 76.0f };
const juce::Rectangle<float> statusDisplay { 40.0f, 452.0f, 400.0f, 52.0f };
const juce::Rectangle<float> keyBody = juce::Rectangle<float> (144.0f, 90.0f).withCentre ({ 352.0f, 326.0f });
const float selectorY = 346.0f;
const float selectorX[] = { 82.0f, 147.0f, 212.0f };
const float loudnessY = 424.0f;
float loudnessX (int pos) { return 82.0f + 32.5f * (float) pos; }
const Material glass { juce::Colour (0xff050607), 0.045f, 0.10f };
const Material keyPlastic { juce::Colour (0xff1aa9d6), 0.05f, 0.30f };

juce::Rectangle<float> selectorTab (int pos)
{
    return juce::Rectangle<float> (26.0f, 17.0f).withCentre ({ selectorX[pos], selectorY });
}
juce::Rectangle<float> loudnessTab (int pos)
{
    return juce::Rectangle<float> (26.0f, 17.0f).withCentre ({ loudnessX (pos), loudnessY });
}
juce::Rectangle<float> loudnessSlot()
{
    return juce::Rectangle<float> (loudnessX (Faceplate::loudnessStops - 1) - loudnessX (0) + 14.0f, 6.0f)
        .withCentre ({ (loudnessX (0) + loudnessX (Faceplate::loudnessStops - 1)) * 0.5f, loudnessY });
}

float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }
float smoothstep01 (float x) { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }

// Distance inside a rounded rectangle (negative outside)
float insideRounded (juce::Rectangle<float> r, float corner, juce::Point<float> p)
{
    const float qx = std::abs (p.x - r.getCentreX()) - (r.getWidth() * 0.5f - corner);
    const float qy = std::abs (p.y - r.getCentreY()) - (r.getHeight() * 0.5f - corner);
    return -(std::hypot (juce::jmax (qx, 0.0f), juce::jmax (qy, 0.0f)) + juce::jmin (juce::jmax (qx, qy), 0.0f) - corner);
}

struct KeyGeometry { float rise, side, lip, corner, bevel; juce::Rectangle<float> top; };
KeyGeometry keyGeometry (bool down)
{
    KeyGeometry k { down ? 2.0f : 7.5f, down ? 2.5f : 7.0f, 1.2f, 7.0f, 2.6f, {} };
    k.top = keyBody.withTrimmedBottom (k.side).expanded (k.lip, k.lip).withTrimmedBottom (k.lip);
    return k;
}

struct Tape { juce::String text; juce::Font font; juce::Rectangle<float> area; juce::AffineTransform tilt; };
Tape dymoTape()
{
    Tape t { text::fileTypes(), labelFont (9.0f, true, 0.04f), {}, {} };
    const float w = juce::GlyphArrangement::getStringWidth (t.font, t.text);
    t.area = juce::Rectangle<float> (w + 22.0f, 15.0f).withCentre ({ 240.0f, 260.5f });
    t.tilt = juce::AffineTransform::rotation (-0.006f, t.area.getCentreX(), t.area.getCentreY());
    return t;
}

// The light on a slide-switch tab: a thin shadow at its foot, a highlight on
// its top edge, its lower half a little darker
void paintTabLight (juce::Graphics& g, juce::Rectangle<float> tab)
{
    juce::Path tabPath;
    tabPath.addRoundedRectangle (tab, 2.6f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 3, { 1, 2 }).drawForPath (g, tabPath);
    juce::Graphics::ScopedSaveState st (g);
    g.reduceClipRegion (tabPath);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff8ee).withAlpha (0.35f), 0.0f, tab.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, tab.getY() + 3.0f, false));
    g.fillRect (tab.withHeight (3.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, tab.getCentreY(),
                                             juce::Colours::black.withAlpha (0.18f), 0.0f, tab.getBottom(), false));
    g.fillRect (tab);
}

// A charcoal plastic slide-switch tab with grip ridges, standing in a slot
void buildTab (Surface& s, juce::Rectangle<float> tab, juce::Rectangle<float> slot)
{
    juce::Random wear (0x5117);
    s.forEach (tab.expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const float d = insideRounded (tab, 2.6f, p);
        if (d <= -0.5f / s.getScale())
            return;
        const float cover = clamp01 (d * s.getScale() + 0.5f);
        const float panelLevel = s.heightAt (px, py) + (slot.contains (p) ? 6.0f : 0.0f);
        const float edge = smoothstep01 (d / 1.4f);
        const float ridge = 0.06f * std::cos ((p.x - tab.getX()) / 1.9f * juce::MathConstants<float>::twoPi);
        const float convex = 0.25f * (1.0f - juce::square ((p.x - tab.getCentreX()) / (tab.getWidth() * 0.5f)));
        const float h = panelLevel + 3.2f * (0.55f + 0.45f * edge) + (ridge + convex) * edge;
        s.heightAt (px, py) = h * cover + s.heightAt (px, py) * (1.0f - cover);
        s.setMaterial (px, py, { juce::Colour (0xff2b2a29), 0.05f, 0.36f }, cover);
        s.scaleColour (px, py, 1.0f + 0.10f * (1.0f - edge) + 0.04f * (wear.nextFloat() - 0.5f));
    });
}

juce::Typeface::Ptr typeface (const void* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}
}

//==============================================================================
Faceplate::Faceplate()
{
    const juce::String svg (RadiomixData::plumppuradiomix_svg, RadiomixData::plumppuradiomix_svgSize);
    logo = juce::Drawable::createFromSVGString (svg);
    matrixPrint = typeface (RadiomixData::MatrixSansPrintRegular_ttf, RadiomixData::MatrixSansPrintRegular_ttfSize);
    matrixMonoSC = typeface (RadiomixData::MatrixMonoPrintSCRegular_ttf, RadiomixData::MatrixMonoPrintSCRegular_ttfSize);

    // "MIX" cut out of the logo: the white letters and cyan shadows of the M,
    // I and X in RADIOMIX, the black outline redrawn around just them at the
    // logo's own thickness
    const auto xml = juce::XmlDocument::parse (svg);
    std::vector<juce::Path> layers;   // black, cyan, white
    std::function<void (const juce::XmlElement&)> collect = [&] (const juce::XmlElement& e)
    {
        if (e.hasTagName ("path"))
            layers.push_back (juce::Drawable::parseSVGPath (e.getStringAttribute ("d")));
        for (auto* c : e.getChildIterator())
            collect (*c);
    };
    if (xml != nullptr)
        collect (*xml);
    jassert (layers.size() == 3);
    if (layers.size() != 3)
        return;
    const auto t = juce::AffineTransform::scale (0.1f, -0.1f).translated (0.0f, 736.0f);   // the file's group transform
    for (auto& p : layers)
        p.applyTransform (t);

    const auto split = [] (const juce::Path& p)
    {
        std::vector<juce::Path> parts;
        juce::Path::Iterator it (p);
        while (it.next())
        {
            if (it.elementType == juce::Path::Iterator::startNewSubPath)
                parts.emplace_back();
            if (parts.empty())
                continue;
            auto& q = parts.back();
            switch (it.elementType)
            {
                case juce::Path::Iterator::startNewSubPath: q.startNewSubPath (it.x1, it.y1); break;
                case juce::Path::Iterator::lineTo: q.lineTo (it.x1, it.y1); break;
                case juce::Path::Iterator::quadraticTo: q.quadraticTo (it.x1, it.y1, it.x2, it.y2); break;
                case juce::Path::Iterator::cubicTo: q.cubicTo (it.x1, it.y1, it.x2, it.y2, it.x3, it.y3); break;
                case juce::Path::Iterator::closePath: q.closeSubPath(); break;
            }
        }
        return parts;
    };

    const float midY = layers[0].getBounds().getCentreY();
    auto whites = split (layers[2]), cyans = split (layers[1]);
    std::vector<juce::Path> line2;
    for (auto& w : whites)
        if (w.getBounds().getCentreY() > midY && w.getBounds().getWidth() > 20.0f)
            line2.push_back (w);
    std::sort (line2.begin(), line2.end(), [] (auto& a, auto& b) { return a.getBounds().getX() < b.getBounds().getX(); });
    if (line2.size() < 3)
        return;
    const float left = line2[line2.size() - 3].getBounds().getX() - 4.0f;   // the M

    for (auto& w : whites)
        if (w.getBounds().getCentreY() > midY && w.getBounds().getX() >= left)
            mix.letters.addPath (w);
    for (auto& c : cyans)
        if (c.getBounds().getCentreY() > midY && c.getBounds().getX() >= left)
            mix.shadow.addPath (c);
    const float thickness = layers[2].getBounds().getX() - layers[0].getBounds().getX();
    juce::Path body (mix.letters);
    body.addPath (mix.shadow);
    juce::PathStrokeType (2.0f * thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath (mix.outline, body);
    mix.outline.addPath (body);
}

//==============================================================================
juce::Image Faceplate::render (bool keyDown, int selector, bool replaceAvailable, int loudnessPos) const
{
    Surface s ({ 0.0f, 0.0f, W, H }, scale);
    PanelStyle style;
    style.coat = { juce::Colour (0xfff1f0ec), 0.045f, 0.40f };
    style.withCross = false;
    buildPanel (s, { 0.0f, 0.0f, W, H }, { { 0.0f, 0.0f }, 0.0f }, style);

    // The two displays: raised blocks of glossy black glass
    buildPlate (s, fileDisplay, 10.0f, 2.4f, glass);
    buildPlate (s, statusDisplay, 9.0f, 2.2f, glass);

    // The Dymo label: plain white glossy tape stuck on slightly askew, its
    // edges catching the light, the letters embossed in plain black
    const auto tp = dymoTape();
    {
        const auto tapeMask = s.mask ([&] (juce::Graphics& g) { g.addTransform (tp.tilt); g.fillRoundedRectangle (tp.area, 1.2f); });
        s.raise (tapeMask, 0.45f, 0.18f);
        s.apply (tapeMask, { juce::Colours::white, 0.08f, 0.14f });
        s.forEach (tp.area.expanded (2.0f), [&] (int px, int py, juce::Point<float> p)
        {
            if (const float m = tapeMask (px, py); m > 0.0f)
            {
                s.heightAt (px, py) += m * 0.025f * std::sin ((p.x - tp.area.getX()) * 0.11f) * std::sin ((p.y - tp.area.getY()) * 0.4f);
                s.addEmission (px, py, 0.10f * m, 0.10f * m, 0.10f * m);   // reads as white under the lamp
            }
        });
        const auto letters = s.mask ([&] (juce::Graphics& g)
        {
            g.addTransform (tp.tilt);
            g.setFont (tp.font);
            g.drawText (tp.text, tp.area.translated (0.0f, 0.3f), juce::Justification::centred, false);
        });
        s.raise (letters, 0.22f, 0.10f);
        s.apply (letters, { juce::Colour (0xff000000), 0.05f, 0.25f });
        s.forEach (tp.area.expanded (2.0f), [&] (int px, int py, juce::Point<float>)
        {
            if (const float m = letters (px, py); m > 0.0f)
                s.addEmission (px, py, -0.10f * m, -0.10f * m, -0.10f * m);
        });
    }

    // The output selector: legends and ticks printed on the panel, a slot cut
    // into it, and a charcoal plastic tab with grip ridges
    {
        for (int i = 0; i < 3; ++i)
        {
            const float x = selectorX[i];
            const bool dim = i == 0 && ! replaceAvailable;
            const Material ink { dim ? juce::Colour (0xffa8a7a3) : juce::Colour (0xff0c0c0c), 0.03f, 0.7f };
            const auto names = text::selector (i);
            const float top = selectorY - 17.0f - 7.0f - 30.0f;
            const auto legend = s.mask ([&] (juce::Graphics& g)
            {
                g.setFont (labelFont (7.8f, true, 0.12f));
                for (int line = 0; line < 3; ++line)
                    g.drawText (names[line], juce::Rectangle<float> (x - 34.0f, top + 10.0f * (float) line, 68.0f, 9.0f),
                                juce::Justification::centred, false);
            });
            s.raise (legend, 0.05f, 0.1f);
            s.apply (legend, ink);
            s.apply (s.mask ([&] (juce::Graphics& g) { g.fillRect (juce::Rectangle<float> (1.2f, 6.0f).withCentre ({ x, selectorY - 17.0f })); }), ink);
        }

        const auto slot = juce::Rectangle<float> (selectorX[2] - selectorX[0] + 14.0f, 6.0f).withCentre ({ selectorX[1], selectorY });
        const auto slotMask = s.mask ([&] (juce::Graphics& g) { g.fillRoundedRectangle (slot, 2.5f); });
        s.raise (slotMask, -6.0f, 0.15f);
        s.apply (slotMask, { juce::Colour (0xff0b0b0c), 0.02f, 0.8f });

        buildTab (s, selectorTab (selector), slot);
    }

    // The loudness slider: the same kind of slide switch, five stops, with
    // its legend and a note on the right
    {
        const Material ink { juce::Colour (0xff0c0c0c), 0.03f, 0.7f };
        const auto printed = s.mask ([&] (juce::Graphics& g)
        {
            g.setFont (labelFont (7.8f, true, 0.12f));
            g.drawText (text::loudnessTitle(), juce::Rectangle<float> (loudnessX (0) - 30.0f, loudnessY - 44.0f, loudnessX (4) - loudnessX (0) + 60.0f, 9.0f),
                        juce::Justification::centred, false);
            for (int i = 0; i < loudnessStops; ++i)
            {
                const float x = loudnessX (i);
                g.drawText (text::loudnessStop (i), juce::Rectangle<float> (x - 16.0f, loudnessY - 31.0f, 32.0f, 9.0f), juce::Justification::centred, false);
                g.fillRect (juce::Rectangle<float> (1.2f, 6.0f).withCentre ({ x, loudnessY - 17.0f }));
            }
            g.setFont (labelFont (7.0f, true, 0.10f));
            const auto note = text::loudnessNote();
            for (int line = 0; line < note.size(); ++line)
                g.drawText (note[line], juce::Rectangle<float> (262.0f, loudnessY - 22.0f + 11.0f * (float) line, 178.0f, 9.0f),
                            juce::Justification::centredLeft, false);
        });
        s.raise (printed, 0.05f, 0.1f);
        s.apply (printed, ink);

        const auto slot = loudnessSlot();
        const auto slotMask = s.mask ([&] (juce::Graphics& g) { g.fillRoundedRectangle (slot, 2.5f); });
        s.raise (slotMask, -6.0f, 0.15f);
        s.apply (slotMask, { juce::Colour (0xff0b0b0c), 0.02f, 0.8f });
        if (loudnessPos >= 0)
            buildTab (s, loudnessTab (loudnessPos), slot);
    }

    // The MIX key: a well in the panel, and a thick satin block standing in it
    const auto geo = keyGeometry (keyDown);
    {
        const float depth = 11.0f;
        const auto mouth = keyBody.expanded (9.0f);
        const float wall = mouth.getRight() - keyBody.expanded (2.5f).getRight();
        s.forEach (mouth.expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
        {
            const float d = insideRounded (mouth, 11.0f, p);
            if (d <= -0.5f / s.getScale())
                return;
            const float cover = clamp01 (d * s.getScale() + 0.5f);
            const float t = clamp01 (d / wall);
            s.heightAt (px, py) -= depth * (1.0f - std::pow (1.0f - t, 1.6f)) * cover;
            if (t < 1.0f)
                s.scaleColour (px, py, 1.0f - 0.18f * t * cover);
            else
                s.setMaterial (px, py, { juce::Colour (0xff1c1d20), 0.03f, 0.7f }, cover);
        });

        Map peel (s.getWidth(), s.getHeight());
        {
            juce::Random r (0x5a7a);
            for (auto& v : peel.data) v = r.nextFloat() - 0.5f;
            blur (peel, 0.9f * s.getScale());
            float e = 0.0f;
            for (auto v : peel.data) e += v * v;
            const float k = 1.0f / std::sqrt (e / (float) peel.data.size());
            for (auto& v : peel.data) v *= k;
        }
        juce::Random wear (0x1970);
        const auto scratches = s.mask ([&] (juce::Graphics& g)
        {
            for (int i = 0; i < 70; ++i)
            {
                const auto a = geo.top.reduced (6.0f).getRelativePoint (wear.nextFloat(), wear.nextFloat());
                const float ang = wear.nextFloat() * 6.283f, len = 3.0f + 14.0f * wear.nextFloat();
                g.setOpacity (0.15f + 0.35f * wear.nextFloat());
                g.drawLine ({ a, a + juce::Point<float> (std::cos (ang), std::sin (ang)) * len }, 0.25f);
            }
        });

        s.forEach (keyBody.expanded (geo.lip + 1.0f), [&] (int px, int py, juce::Point<float> p)
        {
            const float dTop = insideRounded (geo.top, geo.corner, p);
            const float dBody = insideRounded (keyBody, geo.corner - 1.0f, p);
            const float panelLevel = s.heightAt (px, py) + depth;
            if (dTop > -0.5f / s.getScale())
            {
                const float cover = clamp01 (dTop * s.getScale() + 0.5f);
                const float edge = smoothstep01 (dTop / geo.bevel);
                const float h = panelLevel + geo.rise - 2.2f * (1.0f - edge) + 0.012f * peel (px, py) * edge;
                s.heightAt (px, py) = juce::jmax (s.heightAt (px, py), h * cover + s.heightAt (px, py) * (1.0f - cover));
                s.setMaterial (px, py, keyPlastic, cover);
                s.scaleColour (px, py, 1.0f + 0.04f * (1.0f - smoothstep01 (dTop / 10.0f)) + 0.10f * scratches (px, py));
            }
            else if (dBody > -0.5f / s.getScale() && p.y > geo.top.getBottom() - geo.corner * 0.3f)
            {
                const float cover = clamp01 (dBody * s.getScale() + 0.5f);
                const float t = clamp01 ((p.y - geo.top.getBottom()) / juce::jmax (1.0f, keyBody.getBottom() - geo.top.getBottom()));
                s.heightAt (px, py) = panelLevel + geo.rise - 2.2f - (geo.rise + depth * 0.6f) * (t * t * 0.4f + t * 0.6f);
                s.setMaterial (px, py, keyPlastic, cover);
                s.scaleColour (px, py, 0.86f - 0.22f * t + 0.10f * smoothstep01 ((t - 0.75f) / 0.25f));
            }
        });

        // The marking: the play triangle and MIX in the logo's sticker style,
        // printed on the face as three thin inks
        const float h = geo.top.getHeight() * 0.40f;
        const float playW = h * 0.62f, mixW = h * 2.25f, gapW = h * 0.22f;
        const auto group = juce::Rectangle<float> (playW + gapW + mixW, h).withCentre (geo.top.getCentre().translated (0.0f, -1.0f));
        const auto fit = juce::RectanglePlacement (juce::RectanglePlacement::centred)
                             .getTransformToFit (mix.outline.getBounds(), group.withTrimmedLeft (playW + gapW));
        const float edgeW = 32.4f * std::sqrt (std::abs (fit.getDeterminant()));
        const auto pa = group.withWidth (playW).withSizeKeepingCentre (playW, h * 0.62f);
        juce::Path tri;
        tri.addTriangle (pa.getX(), pa.getY(), pa.getX(), pa.getBottom(), pa.getRight(), pa.getCentreY());
        tri = tri.createPathWithRoundedCorners (pa.getHeight() * 0.12f);
        juce::Path outline, shadow, letters;
        juce::PathStrokeType (2 * edgeW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (outline, tri);
        outline.addPath (tri);
        outline.addPath (mix.outline, fit);
        shadow.addPath (tri, juce::AffineTransform::translation (edgeW * 0.45f, edgeW * 0.45f));
        shadow.addPath (mix.shadow, fit);
        letters.addPath (tri);
        letters.addPath (mix.letters, fit);
        const auto layer = [&] (const juce::Path& path, Material m, float lift)
        {
            const auto mask = s.mask ([&] (juce::Graphics& g) { g.fillPath (path); });
            s.raise (mask, 0.03f, 0.08f);
            s.apply (mask, m);
            if (lift != 0.0f)
                s.forEach (keyBody, [&] (int px, int py, juce::Point<float>)
                {
                    if (const float v = mask (px, py); v > 0.0f)
                        s.addEmission (px, py, lift * v, lift * v, lift * v);
                });
        };
        layer (outline, { black, 0.02f, 0.70f }, 0.0f);
        layer (shadow, { cyan, 0.045f, 0.40f }, 0.0f);
        layer (letters, { white, 0.045f, 0.38f }, 0.09f);
    }

    juce::Image img = s.render (true, true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));

    if (logo != nullptr)
        logo->drawWithin (g, logoArea, juce::RectanglePlacement::centred, 1.0f);

    // The tape's gloss along its upper edge
    {
        juce::Graphics::ScopedSaveState st (g);
        g.addTransform (tp.tilt);
        const auto edge = tp.area.reduced (1.5f, 0.0f).withHeight (3.5f).translated (0.0f, 0.8f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), 0.0f, edge.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0.0f, edge.getBottom(), false));
        g.fillRect (edge);
    }

    // The selector tab: a thin shadow at its foot and a highlight on its top edge
    paintTabLight (g, selectorTab (selector));
    if (loudnessPos >= 0)
        paintTabLight (g, loudnessTab (loudnessPos));

    // The key: its soft shadow into the well, a broad sheen and bevel glints
    {
        juce::Path face;
        face.addRoundedRectangle (geo.top, geo.corner);
        {
            juce::Graphics::ScopedSaveState st (g);
            juce::Path well;
            well.setUsingNonZeroWinding (false);
            well.addRoundedRectangle (keyBody.expanded (9.0f), 11.0f);
            well.addRoundedRectangle (geo.top.getUnion (keyBody), geo.corner);
            g.reduceClipRegion (well);
            juce::Path keyShape;
            keyShape.addRoundedRectangle (keyBody, geo.corner);
            juce::DropShadow (juce::Colours::black.withAlpha (keyDown ? 0.35f : 0.55f), keyDown ? 5 : 9,
                              keyDown ? juce::Point<int> (1, 1) : juce::Point<int> (2, 4)).drawForPath (g, keyShape);
            juce::Path ring;
            ring.setUsingNonZeroWinding (false);
            ring.addRoundedRectangle (keyBody.expanded (9.0f), 11.0f);
            ring.addRoundedRectangle (keyBody.expanded (2.5f), 8.0f);
            juce::DropShadow (juce::Colours::black.withAlpha (0.18f), 3, {}).drawForPath (g, ring);
        }
        {
            juce::Graphics::ScopedSaveState st (g);
            g.reduceClipRegion (face);
            const auto c = geo.top.getRelativePoint (0.3f, 0.15f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffff8ee).withAlpha (0.20f), c.x, c.y,
                                                     juce::Colours::white.withAlpha (0.0f), c.x + geo.top.getWidth() * 0.75f, c.y, true));
            g.fillPath (face);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), 0.0f, geo.top.getCentreY(),
                                                     juce::Colours::black.withAlpha (0.10f), 0.0f, geo.top.getBottom(), false));
            g.fillPath (face);
        }
        juce::Path rim;
        rim.addRoundedRectangle (geo.top.reduced (1.1f), geo.corner - 1.1f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffffaf0).withAlpha (0.75f), geo.top.getX(), geo.top.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), geo.top.getCentreX() + 20.0f, geo.top.getBottom(), false));
        g.strokePath (rim, juce::PathStrokeType (0.9f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.0f), geo.top.getX(), geo.top.getY(),
                                                 juce::Colours::black.withAlpha (0.35f), geo.top.getRight(), geo.top.getBottom(), false));
        g.strokePath (rim, juce::PathStrokeType (0.7f));
    }

    // Warm light: highlights a touch warm, shadows slightly cool, a faint
    // vignette and grain. The tape stays plain white.
    {
        juce::Image::BitmapData bits (img, juce::Image::BitmapData::readWrite);
        juce::Random grain (12345);
        const float cx = (float) img.getWidth() * 0.38f, cy = (float) img.getHeight() * 0.30f;
        const float rmax = std::hypot ((float) img.getWidth(), (float) img.getHeight()) * 0.75f;
        const auto tapeArea = tp.area.expanded (1.5f, 2.5f) * scale;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                if (tapeArea.contains ((float) x, (float) y))
                    continue;
                auto c = bits.getPixelColour (x, y);
                float r = c.getFloatRed(), gg = c.getFloatGreen(), b = c.getFloatBlue();
                const float lum = 0.2126f * r + 0.7152f * gg + 0.0722f * b;
                const float warm = clamp01 ((lum - 0.45f) / 0.55f), cool = clamp01 ((0.35f - lum) / 0.35f);
                r += 0.030f * warm - 0.010f * cool;
                gg += 0.012f * warm;
                b += -0.028f * warm + 0.018f * cool;
                const float d = std::hypot ((float) x - cx, (float) y - cy) / rmax;
                const float vig = 1.0f - 0.16f * d * d;
                const float n = (grain.nextFloat() - 0.5f) * 0.012f;
                bits.setPixelColour (x, y, juce::Colour::fromFloatRGBA (clamp01 (r * vig + n), clamp01 (gg * vig + n),
                                                                        clamp01 (b * vig + n), c.getFloatAlpha()));
            }
    }
    return img;
}

//==============================================================================
void Faceplate::drawDotText (juce::Graphics& g, const juce::String& t, juce::Typeface::Ptr tf, float h,
                             juce::Rectangle<float> r, juce::Justification j, float alpha) const
{
    if (t.isEmpty())
        return;
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText (juce::Font (juce::FontOptions (tf).withHeight (h)), t, r.getX(), r.getY(), r.getWidth(), r.getHeight(), j, 1);
    juce::Path path;
    glyphs.createPath (path);
    juce::DropShadow (white.withAlpha (0.45f * alpha), 6, {}).drawForPath (g, path);
    g.setColour (white.withAlpha (alpha));
    g.fillPath (path);
}

void Faceplate::paintDisplays (juce::Graphics& g, const DisplayState& d) const
{
    // The file display
    {
        juce::Graphics::ScopedSaveState st (g);
        g.reduceClipRegion (fileDisplay.toNearestInt());
        const auto inner = fileDisplay.reduced (20.0f, 11.0f);
        drawDotText (g, d.fileName, d.fileIsPlaceholder ? matrixMonoSC : matrixPrint, d.fileIsPlaceholder ? 18.0f : 26.0f,
                     inner.withHeight (30.0f), juce::Justification::centredLeft, d.fileIsPlaceholder ? 0.85f : 1.0f);
        drawDotText (g, d.info, matrixMonoSC, 14.0f, inner.withTrimmedTop (32.0f), juce::Justification::centredLeft, 0.8f);
        drawDotText (g, d.state, matrixMonoSC, 14.0f, inner.withTrimmedTop (32.0f), juce::Justification::centredRight, 0.9f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, fileDisplay.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, fileDisplay.getCentreY(), false));
        g.fillRoundedRectangle (fileDisplay, 10.0f);
    }

    // The status display with its fluorescent bar
    {
        juce::Graphics::ScopedSaveState st (g);
        g.reduceClipRegion (statusDisplay.toNearestInt());
        const auto inner = statusDisplay.reduced (16.0f, 9.0f);
        const auto line = inner.withHeight (15.0f);
        drawDotText (g, d.status, matrixMonoSC, 13.0f, line, juce::Justification::centredLeft, 1.0f);
        drawDotText (g, d.statusAction, matrixMonoSC, 13.0f, line, juce::Justification::centredRight, 0.7f);

        const auto track = inner.withTrimmedTop (22.0f).withHeight (9.0f);
        const juce::Colour phosphor = cyan, core (0xffc4efff);
        g.setColour (phosphor.withAlpha (0.10f));
        g.fillRoundedRectangle (track, 1.5f);
        if (d.progress > 0.0f)
        {
            const auto lit = track.withWidth (track.getWidth() * juce::jmin (1.0f, d.progress));
            juce::Path litPath;
            litPath.addRoundedRectangle (lit, 1.5f);
            juce::DropShadow (phosphor.withAlpha (0.55f), 14, {}).drawForPath (g, litPath);
            juce::DropShadow (phosphor.withAlpha (0.65f), 5, {}).drawForPath (g, litPath);
            juce::ColourGradient body (phosphor.withAlpha (0.85f), 0.0f, lit.getY(), phosphor.withAlpha (0.85f), 0.0f, lit.getBottom(), false);
            body.addColour (0.5, core);
            g.setGradientFill (body);
            g.fillPath (litPath);
            g.setColour (core.withAlpha (0.6f));
            g.fillRect (lit.withLeft (lit.getRight() - 2.0f));
        }
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        for (float x = track.getX() + 1.5f; x < track.getRight(); x += 3.0f)
            g.fillRect (juce::Rectangle<float> (x, track.getY(), 0.6f, track.getHeight()));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.07f), 0.0f, statusDisplay.getY(),
                                                 juce::Colours::transparentWhite, 0.0f, statusDisplay.getCentreY(), false));
        g.fillRoundedRectangle (statusDisplay, 9.0f);
    }
}

juce::Rectangle<float> Faceplate::fileDisplayArea() { return fileDisplay; }
juce::Rectangle<float> Faceplate::keyArea() { return keyBody.expanded (4.0f); }
juce::Rectangle<float> Faceplate::selectorArea() { return { selectorX[0] - 36.0f, selectorY - 56.0f, selectorX[2] - selectorX[0] + 72.0f, 72.0f }; }
juce::Rectangle<float> Faceplate::statusActionArea() { return statusDisplay.withTrimmedLeft (statusDisplay.getWidth() * 0.6f); }

juce::Rectangle<float> Faceplate::loudnessArea() { return { loudnessX (0) - 24.0f, loudnessY - 48.0f, loudnessX (4) - loudnessX (0) + 48.0f, 62.0f }; }

int Faceplate::loudnessPositionAt (float x)
{
    return juce::jlimit (0, loudnessStops - 1, juce::roundToInt ((x - loudnessX (0)) / 32.5f));
}

// The tab is cut out of a render that has it in place (graded with the rest),
// and drawn with its light and shadow wherever the slider is
void Faceplate::paintLoudnessTab (juce::Graphics& g, int position) const
{
    const int home = loudnessStops - 1;
    {
        const juce::ScopedLock sl (spriteLock);
        if (! tabSprite.isValid())
        {
            const auto full = render (false, 1, true, home);
            tabSprite = full.getClippedImage ((loudnessTab (home).expanded (1.0f) * scale).getSmallestIntegerContainer());
        }
    }
    const auto tab = loudnessTab (position);
    juce::Path tabPath;
    tabPath.addRoundedRectangle (tab, 2.6f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 3, { 1, 2 }).drawForPath (g, tabPath);
    {
        juce::Graphics::ScopedSaveState st (g);
        g.reduceClipRegion (tabPath);
        g.setOpacity (1.0f);   // drawImage takes the current colour's alpha, the shadow's
        const auto src = (loudnessTab (home).expanded (1.0f) * scale).getSmallestIntegerContainer().toFloat() / scale;
        g.drawImage (tabSprite, src.translated (tab.getX() - loudnessTab (home).getX(), 0.0f));
    }
}

int Faceplate::selectorPositionAt (float x)
{
    int best = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs (x - selectorX[i]) < std::abs (x - selectorX[best]))
            best = i;
    return best;
}
}
