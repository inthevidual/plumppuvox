#include "PlumppuLook.h"

#include "BinaryData.h"

namespace plumppu
{
namespace
{
constexpr float pi = juce::MathConstants<float>::pi;

juce::Typeface::Ptr loadTypeface (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

juce::Font fontFrom (const juce::Typeface::Ptr& typeface, float height, float tracking)
{
    return juce::Font (juce::FontOptions().withTypeface (typeface).withHeight (height).withKerningFactor (tracking));
}

juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
{
    return { centre.x + radius * std::sin (angle), centre.y - radius * std::cos (angle) };
}

struct Vec3
{
    Vec3 (float xx, float yy, float zz) : x (xx), y (yy), z (zz) {}
    float x, y, z;
    Vec3 operator- (const Vec3& o) const { return Vec3 (x - o.x, y - o.y, z - o.z); }
    Vec3 operator/ (float k) const { return Vec3 (x / k, y / k, z / k); }
    float operator* (const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float length() const { return std::sqrt (x * x + y * y + z * z); }
    Vec3 normalised() const { return *this / length(); }
};

constexpr float panelHeight = 6.0f;

float clamp01 (float x) { return juce::jlimit (0.0f, 1.0f, x); }

// Rises from 0 to 1 over u = 0..1 like the quarter round of a moulded edge.
float roundOver (float u)
{
    return u <= 0.0f ? 0.0f : u >= 1.0f ? 1.0f : std::sin (u * pi * 0.5f);
}

float smooth (float u)
{
    u = clamp01 (u);
    return u * u * (3.0f - 2.0f * u);
}

// Fine concentric marks left by a lathe or spinning tool: a random height
// per ring of the given radius in pixels, smoothed between rings.
float turningMarks (float radiusInPixels, juce::uint32 seed)
{
    auto ring = [seed] (int i)
    {
        auto h = (juce::uint32) i * 0x9e3779b1u ^ seed * 0x85ebca6bu;
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
        return (float) (h & 0xffff) / 65536.0f - 0.5f;
    };
    const float x = radiusInPixels * 0.9f;
    const int i = (int) std::floor (x);
    const float f = smooth (x - (float) i);
    return 0.008f * (ring (i) + (ring (i + 1) - ring (i)) * f);
}

// Signed distance to the edge of a rounded rectangle, positive inside.
float insideRoundedRect (juce::Rectangle<float> r, float corner, juce::Point<float> p)
{
    const auto c = r.getCentre();
    const float qx = std::abs (p.x - c.x) - (r.getWidth() * 0.5f - corner);
    const float qy = std::abs (p.y - c.y) - (r.getHeight() * 0.5f - corner);
    const float outside = std::hypot (juce::jmax (qx, 0.0f), juce::jmax (qy, 0.0f)) + juce::jmin (juce::jmax (qx, qy), 0.0f);
    return corner - outside;
}

float toLinear (juce::uint8 c) { return std::pow ((float) c / 255.0f, 2.2f); }

//==============================================================================
// The studio. All in editor coordinates, z pointing out of the panel.
//
// The key light is a large window up and to the left; it lights the panel,
// casts the shadows and shows up as a broad reflection on curved surfaces.
// A second softbox almost overhead gives glossy domes their highlight.
const Vec3 keyLight { -170.0f, -280.0f, 560.0f };
const Vec3 overheadSoftbox = Vec3 (-0.33f, -0.45f, 0.83f).normalised();
const juce::Point<float> studioCentre { 210.0f, 220.0f };

constexpr float ambientLight = 0.40f;
constexpr float keyStrength = 0.80f;
constexpr float keyAngularSize = 0.11f;  // softness of the cast shadows

float keyNormalisation()
{
    const auto toLight = (keyLight - Vec3 (studioCentre.x, studioCentre.y, 0.0f)).normalised();
    return ambientLight + keyStrength * toLight.z;
}

// Turned and spun metal reflects anisotropically: the light shows as a
// radial streak through the centre, towards the light and away from it,
// grainy with the turning marks.
void addSpunSheen (Surface& s, int px, int py, juce::Point<float> p, juce::Point<float> centre, float radius,
                   juce::Colour tint, float strength, juce::uint32 seed)
{
    const auto d = p - centre;
    const float r = d.getDistanceFromOrigin();

    if (r < 0.5f)
        return;

    const float lightAzimuth = std::atan2 (keyLight.y - centre.y, keyLight.x - centre.x);
    float delta = std::remainder (std::atan2 (d.y, d.x) - lightAzimuth, pi);
    const float towards = std::cos (std::atan2 (d.y, d.x) - lightAzimuth) > 0.0f ? 1.0f : 0.55f;
    const float streak = std::exp (-juce::square (delta / 0.28f)) + 0.25f * std::exp (-juce::square (delta / 0.9f));
    const float grain = 1.0f + 22.0f * turningMarks (r * s.getScale() * 1.7f, seed);
    const float k = strength * streak * towards * grain * (0.25f + 0.75f * juce::jmin (1.0f, r / radius));
    s.addEmission (px, py, k * toLinear (tint.getRed()), k * toLinear (tint.getGreen()), k * toLinear (tint.getBlue()));
}

// Radiance seen in direction r from a surface of the given roughness.
float environment (const Vec3& r, const Vec3& toKey, float rough, float keyVisible, float open)
{
    const float sigmaKey = 0.05f + 0.32f * rough;
    const float dKey = std::acos (juce::jlimit (-1.0f, 1.0f, r * toKey));
    const float key = 5.5f * juce::square (0.10f / sigmaKey) * std::exp (-0.5f * juce::square (dKey / sigmaKey)) * keyVisible;

    const float sigmaBox = 0.14f + 0.32f * rough;
    const float dBox = std::acos (juce::jlimit (-1.0f, 1.0f, r * overheadSoftbox));
    const float box = 2.2f * juce::square (0.18f / sigmaBox) * std::exp (-0.5f * juce::square (dBox / sigmaBox));

    // A rough surface sees the room's average; a mirror sees its layout:
    // the dark camera and photographer straight ahead, a bright ceiling and
    // window wall above, a darker bench below.
    const float average = 0.07f + 0.12f * juce::jmax (0.0f, r.z);
    const float upwards = juce::jmax (0.0f, -r.y), downwards = juce::jmax (0.0f, r.y);
    const float layout = 0.025f + 0.10f * (1.0f - juce::jmax (0.0f, r.z)) + 0.75f * upwards * upwards + 0.06f * downwards;
    const float room = layout + (average - layout) * clamp01 (rough / 0.3f);
    return (key + box) * open + room * open;
}

// Gentle shoulder so highlights roll off instead of clipping.
float toneMap (float x)
{
    constexpr float knee = 0.78f;
    return x < knee ? x : knee + (1.0f - knee) * (1.0f - std::exp (-(x - knee) / (1.0f - knee)));
}

juce::uint8 toSrgb8 (float linear, float dither)
{
    const float v = std::pow (clamp01 (toneMap (juce::jmax (0.0f, linear))), 1.0f / 2.2f);
    return (juce::uint8) juce::jlimit (0, 255, (int) (v * 255.0f + dither));
}

// Triangular dither in [-1, 1) so smooth gradients never band.
float ditherAt (int x, int y)
{
    auto hash = [] (juce::uint32 h)
    {
        h ^= h >> 16; h *= 0x7feb352d; h ^= h >> 15; h *= 0x846ca68b; h ^= h >> 16;
        return (float) (h & 0xffff) / 65536.0f;
    };
    const auto seed = (juce::uint32) x * 1973u + (juce::uint32) y * 9277u;
    return hash (seed) + hash (seed ^ 0x9e3779b9u) - 1.0f + 0.5f;
}

void boxBlurLine (const float* in, float* out, int n, int stride, int radius)
{
    const float scale = 1.0f / (float) (2 * radius + 1);
    float sum = 0.0f;

    for (int i = -radius; i <= radius; ++i)
        sum += in[juce::jlimit (0, n - 1, i) * stride];

    for (int i = 0; i < n; ++i)
    {
        out[i * stride] = sum * scale;
        sum += in[juce::jmin (n - 1, i + radius + 1) * stride] - in[juce::jmax (0, i - radius) * stride];
    }
}
}

//==============================================================================
juce::Font labelFont (float height, bool bold, float tracking)
{
    static const auto regular = loadTypeface (BinaryData::B612Regular_ttf, BinaryData::B612Regular_ttfSize);
    static const auto strong = loadTypeface (BinaryData::B612Bold_ttf, BinaryData::B612Bold_ttfSize);
    return fontFrom (bold ? strong : regular, height, tracking);
}

juce::Font readoutFont (float height)
{
    static const auto mono = loadTypeface (BinaryData::B612MonoBold_ttf, BinaryData::B612MonoBold_ttfSize);
    return fontFrom (mono, height, 0.0f);
}

juce::Font modelFont (float height, float tracking)
{
    static const auto heavy = loadTypeface (BinaryData::BarlowBlack_ttf, BinaryData::BarlowBlack_ttfSize);
    return fontFrom (heavy, height, tracking);
}

//==============================================================================
float Map::clamped (int x, int y) const
{
    return (*this) (juce::jlimit (0, width - 1, x), juce::jlimit (0, height - 1, y));
}

void blur (Map& map, float sigma)
{
    blur (map, sigma, sigma);
}

void blur (Map& map, float sigmaX, float sigmaY)
{
    if (map.data.empty())
        return;

    // Three box passes approximate a Gaussian.
    auto radiusFor = [] (float sigma) { return sigma < 0.3f ? 0 : juce::jmax (1, juce::roundToInt (std::sqrt (4.0f * sigma * sigma + 1.0f) * 0.5f)); };
    const int rx = radiusFor (sigmaX), ry = radiusFor (sigmaY);
    std::vector<float> temp (map.data.size());

    for (int pass = 0; pass < 3; ++pass)
    {
        if (rx > 0)
        {
            for (int y = 0; y < map.height; ++y)
                boxBlurLine (map.data.data() + y * map.width, temp.data() + y * map.width, map.width, 1, rx);

            std::copy (temp.begin(), temp.end(), map.data.begin());
        }

        if (ry > 0)
        {
            for (int x = 0; x < map.width; ++x)
                boxBlurLine (map.data.data() + x, temp.data() + x, map.height, map.width, ry);

            for (int x = 0; x < map.width; ++x)
                for (int y = 0; y < map.height; ++y)
                    map.data[(size_t) (y * map.width + x)] = temp[(size_t) (y * map.width + x)];
        }
    }
}

//==============================================================================
Surface::Surface (juce::Rectangle<float> area, float s)
    : origin (area.getPosition()), scale (s),
      width (juce::roundToInt (area.getWidth() * s)), height (juce::roundToInt (area.getHeight() * s)),
      heights (width, height), alpha (width, height, 1.0f),
      red (width, height), green (width, height), blue (width, height),
      reflectance (width, height, 0.04f), roughness (width, height, 0.5f), metal (width, height)
{
}

juce::Point<float> Surface::positionOf (int px, int py) const noexcept
{
    return origin + juce::Point<float> (((float) px + 0.5f) / scale, ((float) py + 0.5f) / scale);
}

void Surface::forEach (juce::Rectangle<float> area, const std::function<void (int, int, juce::Point<float>)>& fn)
{
    const int x0 = juce::jmax (0, (int) std::floor ((area.getX() - origin.x) * scale));
    const int y0 = juce::jmax (0, (int) std::floor ((area.getY() - origin.y) * scale));
    const int x1 = juce::jmin (width, (int) std::ceil ((area.getRight() - origin.x) * scale));
    const int y1 = juce::jmin (height, (int) std::ceil ((area.getBottom() - origin.y) * scale));

    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            fn (x, y, positionOf (x, y));
}

Map Surface::mask (const std::function<void (juce::Graphics&)>& draw) const
{
    juce::Image image (juce::Image::SingleChannel, width, height, true);

    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::translation (-origin.x, -origin.y).scaled (scale));
        g.setColour (juce::Colours::white);
        draw (g);
    }

    Map result (width, height);
    const juce::Image::BitmapData bits (image, juce::Image::BitmapData::readOnly);

    for (int y = 0; y < height; ++y)
    {
        const auto* line = bits.getLinePointer (y);

        for (int x = 0; x < width; ++x)
            result (x, y) = (float) line[x * bits.pixelStride] / 255.0f;
    }

    return result;
}

void Surface::setMaterial (int px, int py, const Material& m, float coverage)
{
    if (coverage <= 0.0f)
        return;

    coverage = juce::jmin (1.0f, coverage);
    auto mix = [coverage] (float& to, float value) { to += (value - to) * coverage; };
    mix (red (px, py), toLinear (m.colour.getRed()));
    mix (green (px, py), toLinear (m.colour.getGreen()));
    mix (blue (px, py), toLinear (m.colour.getBlue()));
    mix (reflectance (px, py), m.reflectance);
    mix (roughness (px, py), m.roughness);
    mix (metal (px, py), m.metal ? 1.0f : 0.0f);
}

void Surface::apply (const Map& coverage, const Material& m)
{
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            setMaterial (x, y, m, coverage (x, y));
}

void Surface::raise (const Map& coverage, float amount, float softness)
{
    Map shape = coverage;
    blur (shape, softness * scale * 0.5f);

    // Sharpen the blurred mask again so the top stays flat and only the
    // edges are rounded.
    for (size_t i = 0; i < shape.data.size(); ++i)
        heights.data[i] += amount * smooth ((shape.data[i] - 0.08f) / 0.84f);
}

void Surface::scaleColour (int px, int py, float factor)
{
    red (px, py) *= factor;
    green (px, py) *= factor;
    blue (px, py) *= factor;
}

void Surface::addEmission (int px, int py, float r, float g, float b)
{
    if (emitRed.data.empty())
    {
        emitRed = Map (width, height);
        emitGreen = Map (width, height);
        emitBlue = Map (width, height);
    }

    emitRed (px, py) += r;
    emitGreen (px, py) += g;
    emitBlue (px, py) += b;
}

juce::Image Surface::render (bool castShadows, bool occlusion) const
{
    // Ambient occlusion: hollows next to raised parts see less of the room.
    Map open (width, height, 1.0f);

    if (occlusion)
    {
        Map surroundings = heights;
        blur (surroundings, 7.0f * scale);

        for (size_t i = 0; i < open.data.size(); ++i)
            open.data[i] = juce::jlimit (0.45f, 1.0f, 1.0f - 0.085f * juce::jmax (0.0f, surroundings.data[i] - heights.data[i]));
    }

    // Soft shadows from the key light: march towards it and see how close
    // the ray passes to anything standing in the way.
    Map lit (width, height, 1.0f);

    if (castShadows)
    {
        const float highest = *std::max_element (heights.data.begin(), heights.data.end());

        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto p = positionOf (x, y);
                const float h0 = heights (x, y);
                const Vec3 toLight (keyLight.x - p.x, keyLight.y - p.y, keyLight.z - h0);
                const float across = std::hypot (toLight.x, toLight.y);
                const float dx = toLight.x / across, dy = toLight.y / across;
                const float rise = toLight.z / across;
                float visible = 1.0f;

                for (float t = 0.6f; h0 + t * rise < highest; t += 0.5f / scale + t * 0.04f)
                {
                    const int qx = juce::roundToInt ((p.x + dx * t - origin.x) * scale - 0.5f);
                    const int qy = juce::roundToInt ((p.y + dy * t - origin.y) * scale - 0.5f);

                    if (qx < 0 || qy < 0 || qx >= width || qy >= height)
                        break;

                    const float above = heights (qx, qy) - (h0 + t * rise);
                    visible = juce::jmin (visible, clamp01 (0.5f - above / (2.0f * t * keyAngularSize)));

                    if (visible <= 0.0f)
                        break;
                }

                lit (x, y) = visible;
            }

        blur (lit, 0.6f * scale);
    }

    const float norm = keyNormalisation();
    const float referenceDistance = (keyLight - Vec3 (studioCentre.x, studioCentre.y, 0.0f)).length();
    const bool emits = ! emitRed.data.empty();

    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Image::BitmapData bits (image, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const float a = alpha (x, y);

            if (a <= 0.0f)
                continue;

            // Height is in logical units, so the slope per pixel is scaled.
            const float sx = (heights.clamped (x + 1, y) - heights.clamped (x - 1, y)) * 0.5f * scale;
            const float sy = (heights.clamped (x, y + 1) - heights.clamped (x, y - 1)) * 0.5f * scale;
            const auto n = Vec3 (-sx, -sy, 1.0f).normalised();

            const auto p = positionOf (x, y);
            const auto toLightVector = keyLight - Vec3 (p.x, p.y, heights (x, y));
            const float distance = toLightVector.length();
            const auto toLight = toLightVector / distance;
            const float falloff = std::pow (referenceDistance / distance, 1.3f);

            const float shade = lit (x, y), ambient = open (x, y);
            const float direct = keyStrength * juce::jmax (0.0f, n * toLight) * shade * falloff;
            const float diffuse = (ambientLight * ambient + direct) / norm;

            const auto reflected = Vec3 (2.0f * n.z * n.x, 2.0f * n.z * n.y, 2.0f * n.z * n.z - 1.0f);
            const float f0 = reflectance (x, y);
            const float fresnel = f0 + (1.0f - f0) * std::pow (1.0f - juce::jmax (0.0f, n.z), 5.0f);
            const float env = environment (reflected, toLight, roughness (x, y), shade * falloff, ambient);

            float rgb[3] = { red (x, y), green (x, y), blue (x, y) };
            const float m = metal (x, y);

            for (auto& c : rgb)
            {
                const float dielectric = c * diffuse * (1.0f - fresnel) + env * fresnel;
                const float edge = std::pow (1.0f - juce::jmax (0.0f, n.z), 5.0f);
                const float metallic = c * diffuse * 0.75f * roughness (x, y) + env * (c + (1.0f - c) * edge);
                c = dielectric + (metallic - dielectric) * m;
            }

            if (emits)
            {
                rgb[0] += emitRed (x, y);
                rgb[1] += emitGreen (x, y);
                rgb[2] += emitBlue (x, y);
            }

            const float dither = ditherAt (x, y);
            bits.setPixelColour (x, y, juce::Colour (toSrgb8 (rgb[0], dither), toSrgb8 (rgb[1], dither),
                                                     toSrgb8 (rgb[2], dither), clamp01 (a)));
        }

    return image;
}

//==============================================================================
void buildScrew (Surface& s, juce::Point<float> c, float radius, float slotAngle)
{
    const float sink = radius * 1.35f;
    const juce::Point<float> slot (std::cos (slotAngle), std::sin (slotAngle));

    s.forEach (juce::Rectangle<float> (2 * sink, 2 * sink).withCentre (c).expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const auto d = p - c;
        const float r = d.getDistanceFromOrigin();

        // Countersink in the panel
        if (r < sink)
            s.heightAt (px, py) -= 1.1f * smooth ((sink - r) / (sink - radius * 0.9f));

        const float cover = clamp01 ((radius - r) * s.getScale() + 0.5f);

        if (cover <= 0.0f)
            return;

        // Domed head with a slot across it
        float h = 0.4f + 1.5f * std::sqrt (juce::jmax (0.0f, 1.0f - juce::square (r / radius)));
        const float fromSlot = std::abs (d.x * slot.y - d.y * slot.x);
        h -= 1.2f * smooth ((radius * 0.15f - fromSlot) * s.getScale() * 0.6f + 0.5f);

        s.heightAt (px, py) += h * cover;
        s.setMaterial (px, py, materials::steel, cover);
    });
}

void printText (Surface& s, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                juce::Justification just, const Material& ink)
{
    const auto coverage = s.mask ([&] (juce::Graphics& g)
    {
        g.setFont (font);
        g.drawText (text, area, just, false);
    });

    // Ink lies a few microns proud of the plastic, and a pad print is never
    // quite opaque.
    s.raise (coverage, 0.10f, 0.25f);
    Map film = coverage;

    for (auto& v : film.data)
        v *= 0.94f;

    s.apply (film, ink);
}

void mouldText (Surface& s, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                juce::Justification just, const Material& m, float height)
{
    const auto coverage = s.mask ([&] (juce::Graphics& g)
    {
        g.setFont (font);
        g.drawText (text, area, just, false);
    });

    s.raise (coverage, height, height * 1.1f);
    s.apply (coverage, m);
}

void buildPlate (Surface& s, juce::Rectangle<float> area, float corner, float height, const Material& m)
{
    s.forEach (area.expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const float d = insideRoundedRect (area, corner, p);
        const float cover = clamp01 (d * s.getScale() + 0.5f);

        if (cover <= 0.0f)
            return;

        s.heightAt (px, py) += height * roundOver (d / (height * 1.2f));
        s.setMaterial (px, py, m, cover);
    });
}

//==============================================================================
// Brush-painted enamel inside masking tape: edges that wander very slightly
// and carry a small ridge of paint where the tape was, brush marks running
// along each band, and colour that varies with the thickness of the paint.
void paintCross (Surface& s, juce::Rectangle<float> clip, float clipCorner,
                 juce::Rectangle<float> vertical, juce::Rectangle<float> horizontal, const Material& paint)
{
    juce::Random random (0x464c4147);

    auto strokes = [&] (float alongX, float alongY)
    {
        Map m (s.getWidth(), s.getHeight());

        for (auto& v : m.data)
            v = random.nextFloat() - 0.5f;

        blur (m, alongX * s.getScale(), alongY * s.getScale());
        float energy = 0.0f;

        for (auto v : m.data)
            energy += v * v;

        const float gain = 1.0f / std::sqrt (energy / (float) m.data.size());

        for (auto& v : m.data)
            v *= gain;

        return m;
    };

    const auto upDown = strokes (4.0f, 60.0f), sideways = strokes (60.0f, 4.0f);
    const auto bristlesUpDown = strokes (0.6f, 22.0f), bristlesSideways = strokes (22.0f, 0.6f);
    const auto blotches = strokes (10.0f, 10.0f);

    // The tape edge wanders by a fraction of a unit
    float phases[6];

    for (auto& ph : phases)
        ph = random.nextFloat() * 2.0f * pi;

    auto wander = [&] (float t, int side)
    {
        const float* ph = phases + side * 3;
        return 0.16f * std::sin (t * 0.027f + ph[0]) + 0.08f * std::sin (t * 0.097f + ph[1]) + 0.04f * std::sin (t * 0.37f + ph[2]);
    };

    s.forEach (clip, [&] (int px, int py, juce::Point<float> p)
    {
        const float inClip = clamp01 (insideRoundedRect (clip, clipCorner, p) * s.getScale() + 0.5f);
        const float dv = vertical.getWidth() * 0.5f - std::abs (p.x - vertical.getCentreX()) + wander (p.y, 0);
        const float dh = horizontal.getHeight() * 0.5f - std::abs (p.y - horizontal.getCentreY()) + wander (p.x, 1);
        const float d = juce::jmax (dv, dh);
        const float cover = clamp01 (d * s.getScale() + 0.5f) * inClip;

        if (cover <= 0.0f)
            return;

        // Which way the brush went here
        const bool upright = dv >= dh;
        const float stroke = upright ? upDown (px, py) : sideways (px, py);
        const float bristle = upright ? bristlesUpDown (px, py) : bristlesSideways (px, py);
        const float pooled = std::exp (-juce::square ((d - 0.5f) / 0.6f));

        auto& h = s.heightAt (px, py);
        h += cover * (0.05f + 0.03f * stroke + 0.01f * bristle + 0.015f * blotches (px, py) + 0.05f * pooled);

        s.setMaterial (px, py, paint, cover);
        // Thin paint lets the white coat glow through; thick paint and the
        // pool along the tape edge are darker
        const float thin = 0.09f * stroke + 0.035f * bristle + 0.05f * blotches (px, py) - 0.16f * pooled;
        s.scaleColour (px, py, 1.0f + thin * cover);
    });
}

void buildPanel (Surface& s, juce::Rectangle<float> bounds, const PanelLayout& layout, const PanelStyle& style)
{
    const auto panel = bounds.reduced (4.0f);
    const float corner = 9.0f;
    const float chamfer = 3.6f;

    // Chassis around the plate, and the plate itself: flat, with its edge
    // rounded over and the powder coat wrapping round it
    s.forEach (bounds, [&] (int px, int py, juce::Point<float> p)
    {
        s.setMaterial (px, py, materials::chassis, 1.0f);
        const float d = insideRoundedRect (panel, corner, p);
        const float cover = clamp01 (d * s.getScale() + 0.5f);

        if (cover <= 0.0f)
            return;

        s.heightAt (px, py) = cover * (panelHeight - 2.2f * (1.0f - roundOver (d / chamfer)));
        s.setMaterial (px, py, style.coat, cover);
    });

    // Powder coat is never perfectly flat: a faint orange peel
    {
        Map peel (s.getWidth(), s.getHeight());
        juce::Random random (0x504c4d50);

        for (auto& v : peel.data)
            v = random.nextFloat() - 0.5f;

        blur (peel, 1.4f * s.getScale());
        float energy = 0.0f;

        for (auto v : peel.data)
            energy += v * v;

        const float gain = 0.010f / std::sqrt (energy / (float) peel.data.size());

        s.forEach (panel.reduced (chamfer), [&] (int px, int py, juce::Point<float>)
        {
            s.heightAt (px, py) += peel (px, py) * gain;
        });
    }

    // The cross, painted over the coat
    const float t = layout.bandThickness;
    const juce::Rectangle<float> vertical (layout.crossCentre.x - t * 0.5f, panel.getY(), t, panel.getHeight());
    const juce::Rectangle<float> horizontal (panel.getX(), layout.crossCentre.y - t * 0.5f, panel.getWidth(), t);

    if (style.withCross)
        paintCross (s, panel.reduced (0.8f), corner - 0.8f, vertical, horizontal, style.cross);

    const float inset = 18.0f;
    buildScrew (s, { panel.getX() + inset, panel.getY() + inset }, 5.0f, 0.6f);
    buildScrew (s, { panel.getRight() - inset, panel.getY() + inset }, 5.0f, 2.2f);
    buildScrew (s, { panel.getX() + inset, panel.getBottom() - inset }, 5.0f, 1.4f);
    buildScrew (s, { panel.getRight() - inset, panel.getBottom() - inset }, 5.0f, 2.9f);
}

float buildLogo (Surface& s, float right, float top, const juce::String& model, const juce::String& tagline,
                 const Material& paint, const Material& badgeText)
{
    const float width = 240.0f;
    float y = top;

    // "Plumppu" engraved and filled, "Vox" engraved into a painted badge
    {
        const auto font = labelFont (24.0f, true, -0.01f);
        const float w1 = juce::GlyphArrangement::getStringWidth (font, "Plumppu");
        const float w2 = juce::GlyphArrangement::getStringWidth (font, "Vox");
        const float pad = 6.0f, gap = 4.0f, h = 30.0f;
        const float x = right - (w1 + gap + w2 + 2 * pad);
        engraveText (s, "Plumppu", font, { x, y, w1 + 2.0f, h }, juce::Justification::centredLeft, paint, 0.8f);
        const juce::Rectangle<float> badge (x + w1 + gap, y + 1.0f, w2 + 2 * pad, h - 2.0f);
        paintOn (s, s.mask ([&] (juce::Graphics& g) { g.fillRoundedRectangle (badge, 4.0f); }), paint);
        engraveText (s, "Vox", font, badge.translated (0.0f, -1.0f), juce::Justification::centred, badgeText, 0.8f);
        y += h + 2.0f;
    }

    // The model name in the black weight
    const float lineHeight = 44.0f;
    engraveText (s, model, modelFont (46.0f, 0.04f), { right - width, y + 2.0f, width, lineHeight },
                 juce::Justification::centredRight, paint, 0.8f);
    y += lineHeight + 6.0f;

    engraveText (s, tagline, labelFont (9.0f, true, 0.30f), { right - width, y, width, 11.0f },
                 juce::Justification::centredRight, paint);
    return y + 11.0f;
}

void paintOn (Surface& s, const Map& coverage, const Material& m)
{
    s.raise (coverage, 0.04f, 0.12f);
    s.apply (coverage, m);
}

void engrave (Surface& s, const Map& coverage, const Material& fill, float depth)
{
    // The cutter goes through the coat into the metal: the walls of each cut
    // are bare aluminium, and the paint fill settles on the floor.
    s.raise (coverage, -depth, 0.22f);

    Map cut = coverage, floor = coverage;
    blur (cut, 0.12f * s.getScale());

    for (size_t i = 0; i < cut.data.size(); ++i)
    {
        floor.data[i] = smooth ((cut.data[i] - 0.25f) / 0.35f);
        cut.data[i] = 0.7f * smooth ((cut.data[i] - 0.02f) / 0.2f);
    }

    s.apply (cut, materials::aluminium);
    s.apply (floor, fill);
}

void engraveText (Surface& s, const juce::String& text, const juce::Font& font, juce::Rectangle<float> area,
                  juce::Justification just, const Material& fill, float depth)
{
    engrave (s, s.mask ([&] (juce::Graphics& g)
    {
        g.setFont (font);
        g.drawText (text, area, just, false);
    }), fill, depth);
}

//==============================================================================
void buildDialPlate (Surface& s, juce::Point<float> centre, float radius,
                     float startAngle, float endAngle, const juce::StringArray& labels, int minorPerMajor)
{
    // A thin painted metal ring with a rounded edge
    const float thickness = 1.4f, chamfer = 2.2f;

    s.forEach (juce::Rectangle<float> (2 * radius, 2 * radius).withCentre (centre).expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const float r = p.getDistanceFrom (centre);
        const float d = radius - r;
        const float cover = clamp01 (d * s.getScale() + 0.5f);

        if (cover <= 0.0f)
            return;

        auto& h = s.heightAt (px, py);
        const float top = panelHeight + thickness * roundOver (d / chamfer);
        h += (top - h) * cover;
        s.setMaterial (px, py, materials::whiteMetalPaint, cover);
    });

    // Engraved scale, kept well inside the chamfer
    const int majors = juce::jmax (2, labels.size());
    const int steps = (majors - 1) * minorPerMajor;
    const float tickOuter = radius * 0.775f;
    const auto font = labelFont (radius * 0.112f, true);

    const auto scale = s.mask ([&] (juce::Graphics& g)
    {
        g.setFont (font);

        for (int i = 0; i <= steps; ++i)
        {
            const float angle = startAngle + (endAngle - startAngle) * (float) i / (float) steps;
            const bool major = i % minorPerMajor == 0;
            const float inner = tickOuter - radius * (major ? 0.10f : 0.05f);
            const auto a = onCircle (centre, inner, angle), b = onCircle (centre, tickOuter, angle);
            juce::Path tick;
            tick.addLineSegment ({ a, b }, major ? 1.6f : 0.9f);
            g.fillPath (tick);

            if (major)
            {
                const auto q = onCircle (centre, radius * 0.875f, angle);
                g.drawText (labels[i / minorPerMajor], juce::Rectangle<float> (q.x - 20.0f, q.y - 8.0f, 40.0f, 16.0f),
                            juce::Justification::centred, false);
            }
        }
    });

    engrave (s, scale, materials::engravingFill, 0.6f);

}

//==============================================================================
void buildReadoutBezel (Surface& s, juce::Rectangle<float> area)
{
    const auto outer = area.expanded (5.0f);

    s.forEach (outer.expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const float dOuter = insideRoundedRect (outer, 6.0f, p);
        const float dInner = insideRoundedRect (area, 3.0f, p);
        const float cover = clamp01 (dOuter * s.getScale() + 0.5f);

        if (cover <= 0.0f)
            return;

        s.heightAt (px, py) += 3.5f * roundOver (dOuter / 3.2f) - 5.0f * roundOver (dInner / 1.4f);
        s.setMaterial (px, py, materials::bakelite, cover);
        s.setMaterial (px, py, materials::displayGlass, clamp01 (dInner * s.getScale() + 0.5f));
    });
}

void drawReadout (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
{
    juce::Graphics::ScopedSaveState state (g);
    juce::Path window;
    window.addRoundedRectangle (area, 3.0f);
    g.reduceClipRegion (window);

    // Backlit figures, glowing softly through the smoked glass
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText (readoutFont (area.getHeight() * 0.62f), text, area.getX(), area.getY() + 1.0f,
                          area.getWidth(), area.getHeight(), juce::Justification::centred, 1);
    juce::Path figures;
    glyphs.createPath (figures);

    juce::DropShadow (juce::Colour (0xffa9c8ff).withAlpha (0.55f), 9, {}).drawForPath (g, figures);
    juce::DropShadow (juce::Colour (0xffe8f0ff).withAlpha (0.65f), 2, {}).drawForPath (g, figures);
    g.setColour (juce::Colour (0xfff6f8ff));
    g.fillPath (figures);

    // The glass: the bezel shades its top edge, and the window reflects as a
    // faint wash over the upper half
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                             juce::Colours::transparentBlack, 0.0f, area.getY() + 7.0f, false));
    g.fillRect (area);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), area.getX(), 0.0f,
                                             juce::Colours::transparentBlack, area.getX() + 6.0f, 0.0f, false));
    g.fillRect (area);

    juce::Path sheen;
    sheen.startNewSubPath (area.getX(), area.getY());
    sheen.lineTo (area.getX() + area.getWidth() * 0.62f, area.getY());
    sheen.lineTo (area.getX() + area.getWidth() * 0.38f, area.getBottom());
    sheen.lineTo (area.getX(), area.getBottom());
    sheen.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), area.getX(), area.getY(),
                                             juce::Colours::white.withAlpha (0.015f), area.getX() + area.getWidth() * 0.4f, area.getBottom(), false));
    g.fillPath (sheen);
}

//==============================================================================
namespace
{
struct LampGeometry
{
    float pitch, diameter;
    juce::Point<float> centre (juce::Rectangle<float> area, int i) const
    {
        return { area.getX() + pitch * ((float) i + 0.5f), area.getCentreY() };
    }
};

LampGeometry lampGeometry (juce::Rectangle<float> area, int lampCount)
{
    const float pitch = area.getWidth() / (float) lampCount;
    return { pitch, juce::jmin (area.getHeight(), pitch * 0.72f) };
}

// A domed glass jewel, dark or lit from behind.
juce::Image renderJewel (juce::Point<float> centre, float diameter, float scale, bool lit)
{
    const float r = diameter * 0.5f;
    const auto box = juce::Rectangle<float> (diameter + 2.0f, diameter + 2.0f).withCentre (centre);
    Surface s (box, scale);
    const Material glass { lit ? juce::Colour (0xff7fb2ff) : juce::Colour (0xff0a1d42), 0.05f, 0.08f };

    for (int y = 0; y < s.getHeight(); ++y)
        for (int x = 0; x < s.getWidth(); ++x)
        {
            const auto p = s.positionOf (x, y);
            const float d = p.getDistanceFrom (centre);
            const float cover = clamp01 ((r - d) * scale + 0.5f);
            s.alphaAt (x, y) = cover;

            if (cover <= 0.0f)
                continue;

            const float u = juce::jmin (1.0f, d / r);
            s.heightAt (x, y) = r * 0.6f * std::sqrt (1.0f - u * u);
            s.setMaterial (x, y, glass, 1.0f);

            if (lit)
            {
                const float core = 1.0f - 0.65f * u * u;
                s.addEmission (x, y, 0.35f * core, 0.55f * core, 1.05f * core);
            }
        }

    return s.render (false, false);
}

struct JewelCache
{
    juce::Image dark, lit;
    float diameter = 0.0f, scale = 0.0f;
};
}

void buildLampSockets (Surface& s, juce::Rectangle<float> area, int lampCount)
{
    const auto geometry = lampGeometry (area, lampCount);

    for (int i = 0; i < lampCount; ++i)
    {
        const auto c = geometry.centre (area, i);
        const float outer = geometry.diameter * 0.5f + 2.2f, inner = geometry.diameter * 0.5f + 0.2f;

        s.forEach (juce::Rectangle<float> (2 * outer, 2 * outer).withCentre (c).expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
        {
            const float r = p.getDistanceFrom (c);
            const float cover = clamp01 ((outer - r) * s.getScale() + 0.5f);

            if (cover <= 0.0f)
                return;

            s.heightAt (px, py) += 2.0f * roundOver ((outer - r) / 1.4f) - 1.6f * roundOver ((inner - r) / 0.6f);
            s.setMaterial (px, py, materials::bakelite, cover);
        });
    }
}

void drawLampRow (juce::Graphics& g, juce::Rectangle<float> area, int lampCount, float level)
{
    drawLamps (g, area, lampCount, [level] (int i) { return juce::jlimit (0.0f, 1.0f, level - (float) i); });
}

void drawLamps (juce::Graphics& g, juce::Rectangle<float> area, int lampCount,
                const std::function<float (int)>& brightness)
{
    static JewelCache cache;
    const auto geometry = lampGeometry (area, lampCount);
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (cache.diameter != geometry.diameter || cache.scale != scale)
    {
        const auto middle = geometry.centre (area, lampCount / 2);
        cache.dark = renderJewel (middle, geometry.diameter, scale, false);
        cache.lit = renderJewel (middle, geometry.diameter, scale, true);
        cache.diameter = geometry.diameter;
        cache.scale = scale;
    }

    const float size = geometry.diameter + 2.0f;

    for (int i = 0; i < lampCount; ++i)
    {
        const float lit = juce::jlimit (0.0f, 1.0f, brightness (i));
        const auto c = geometry.centre (area, i);
        const auto box = juce::Rectangle<float> (size, size).withCentre (c);

        g.setOpacity (1.0f);
        g.drawImage (cache.dark, box);

        if (lit > 0.01f)
        {
            g.setOpacity (lit);
            g.drawImage (cache.lit, box);
            g.setOpacity (1.0f);

            const float halo = geometry.diameter * 1.6f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8fbcff).withAlpha (0.30f * lit), c.x, c.y,
                                                     juce::Colour (0xff8fbcff).withAlpha (0.0f), c.x + halo, c.y, true));
            g.fillEllipse (juce::Rectangle<float> (2 * halo, 2 * halo).withCentre (c));
        }
    }
}

//==============================================================================
namespace
{
constexpr float toggleRingRadius = 16.0f;
constexpr float toggleLeverLength = 24.0f;
const Material creamPlastic { juce::Colour (0xfff3dca6), 0.05f, 0.30f };
const Material chrome { juce::Colour (0xffcfd2d6), 0.9f, 0.24f, true };

juce::Image renderLever (juce::Point<float> centre, float angle, float scale)
{
    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));
    const auto tip = centre + dir * toggleLeverLength;
    const float tipRadius = 6.8f, shaftRadius = 2.4f, nutRadius = 7.0f;
    const auto box = juce::Rectangle<float> (centre, tip).expanded (nutRadius + 2.0f);
    Surface s (box, scale);

    for (int y = 0; y < s.getHeight(); ++y)
        for (int x = 0; x < s.getWidth(); ++x)
        {
            const auto p = s.positionOf (x, y);
            float height = -1.0f, cover = 0.0f;
            const Material* m = nullptr;

            // Chrome bushing nut around the pivot
            const float dn = p.getDistanceFrom (centre);
            const float nutCover = clamp01 ((nutRadius - dn) * scale + 0.5f);
            if (nutCover > 0.0f)
            {
                height = 0.8f + 0.8f * std::sqrt (juce::jmax (0.0f, 1.0f - juce::square (dn / nutRadius)));
                cover = nutCover;
                m = &chrome;
            }

            // The shaft rises towards the viewer as it leans out
            const auto rel = p - centre;
            const float along = juce::jlimit (0.0f, toggleLeverLength, rel.x * dir.x + rel.y * dir.y);
            const float across = (rel - dir * along).getDistanceFromOrigin();
            const float shaftCover = clamp01 ((shaftRadius - across) * scale + 0.5f);
            if (shaftCover > 0.0f)
            {
                const float h = 1.6f + 1.4f * along / toggleLeverLength
                              + 0.8f * shaftRadius * std::sqrt (juce::jmax (0.0f, 1.0f - juce::square (across / shaftRadius)));
                if (h > height) { height = h; m = &chrome; }
                cover = juce::jmax (cover, shaftCover);
            }

            // Cream ball tip
            const float dt = p.getDistanceFrom (tip);
            const float tipCover = clamp01 ((tipRadius - dt) * scale + 0.5f);
            if (tipCover > 0.0f)
            {
                const float h = 3.0f + 0.6f * tipRadius * std::sqrt (juce::jmax (0.0f, 1.0f - juce::square (dt / tipRadius)));
                if (h > height) { height = h; m = &creamPlastic; }
                cover = juce::jmax (cover, tipCover);
            }

            s.alphaAt (x, y) = cover;
            if (m != nullptr)
            {
                s.heightAt (x, y) = height;
                s.setMaterial (x, y, *m, 1.0f);
            }
        }

    return s.render (false, false);
}
}

void buildToggleRing (Surface& s, juce::Point<float> c)
{
    // A ribbed ring nut screwed down on the panel, with the lever's slot
    const float r = toggleRingRadius, hole = 7.5f;
    s.forEach (juce::Rectangle<float> (2 * r, 2 * r).withCentre (c).expanded (1.0f), [&] (int px, int py, juce::Point<float> p)
    {
        const auto d = p - c;
        const float dist = d.getDistanceFromOrigin();
        const float cover = clamp01 ((r - dist) * s.getScale() + 0.5f) * clamp01 ((dist - hole) * s.getScale() + 0.5f);
        if (cover <= 0.0f)
            return;

        const float ribs = 0.12f * std::cos (std::atan2 (d.y, d.x) * 36.0f) * smooth ((dist - (r - 3.0f)) / 3.0f);
        const float dome = 1.6f * roundOver ((r - dist) / 2.0f) * roundOver ((dist - hole) / 1.5f);
        s.heightAt (px, py) += cover * (dome + ribs);
        s.setMaterial (px, py, creamPlastic, cover);
    });
}

void drawToggleLever (juce::Graphics& g, juce::Point<float> centre, float angle)
{
    struct Cache { float angle = -100.0f, scale = 0.0f; juce::Point<float> centre; juce::Image image; juce::Rectangle<float> box; };
    static Cache caches[3];
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const juce::Point<float> dir (std::sin (angle), -std::cos (angle));

    // The lever's shadow falls away from the window, longer at the tip
    juce::Path shadow;
    shadow.addEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (centre));
    const auto tip = centre + dir * toggleLeverLength;
    shadow.addEllipse (juce::Rectangle<float> (13.6f, 13.6f).withCentre (tip + juce::Point<float> (3.0f, 6.0f)));
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 6, { 2, 4 }).drawForPath (g, shadow);

    auto& c = caches[angle < -0.01f ? 0 : angle > 0.01f ? 2 : 1];   // one per position
    if (c.image.isNull() || c.angle != angle || c.scale != scale || c.centre != centre)
    {
        c.image = renderLever (centre, angle, scale);
        c.box = juce::Rectangle<float> (centre, tip).expanded (7.0f + 2.0f);
        c.angle = angle;
        c.scale = scale;
        c.centre = centre;
    }
    // The shadow left its colour (and so its opacity) in the context
    g.setOpacity (1.0f);
    g.drawImage (c.image, c.box);
}

//==============================================================================
namespace
{
constexpr int gearTeeth = 30;
constexpr float gearRoot = 0.88f;                    // root circle, as a fraction of the tip radius
constexpr float toothTip = 0.21f, toothRoot = 0.32f;  // half widths, as fractions of the pitch

// Phase within the tooth pitch at a given angle, in -0.5..0.5.
float toothPhase (float around)
{
    const float t = around * (float) gearTeeth / (2.0f * pi);
    return t - std::round (t);
}

// Half width of a tooth at radius r (fraction of the tip radius).
float toothHalfWidth (float r)
{
    return toothTip + (toothRoot - toothTip) * clamp01 ((1.0f - r) / (1.0f - gearRoot));
}

// A black Meccano gear wheel with a white enamelled dome nut on top; the
// tooth it points with is painted white.
juce::Image renderKnob (juce::Rectangle<float> bounds, juce::Point<float> centre, float radius, float angle, float scale)
{
    Surface s (bounds, scale);

    const float gearTop = radius * 0.10f, edgeRound = radius * 0.03f;
    const float domeRadius = radius * 0.69f, domeHeight = radius * 0.34f;
    const float sphere = (domeRadius * domeRadius + domeHeight * domeHeight) / (2.0f * domeHeight);
    const juce::Point<float> axis (std::sin (angle), -std::cos (angle));
    const Material blackGear { juce::Colour (0xff2c2c2e), 0.9f, 0.34f, true };
    const Material whitePaint { juce::Colour (0xfff3f1eb), 0.045f, 0.30f };

    for (int y = 0; y < s.getHeight(); ++y)
        for (int x = 0; x < s.getWidth(); ++x)
        {
            const auto p = s.positionOf (x, y);
            const auto d = p - centre;
            const float r = d.getDistanceFromOrigin();
            const float along = d.x * axis.x + d.y * axis.y;
            const float aside = d.x * axis.y - d.y * axis.x;

            // Distance inside the toothed outline
            const float u = r / radius;
            const float phase = std::abs (toothPhase (std::atan2 (d.x, -d.y) - angle));
            const float pitch = 2.0f * pi * r / (float) gearTeeth;
            const float inTooth = juce::jmin (radius - r, (toothHalfWidth (u) - phase) * pitch);
            const float edge = juce::jmax (radius * gearRoot - r, inTooth);
            const float cover = clamp01 (edge * scale + 0.5f);
            s.alphaAt (x, y) = cover;

            if (cover <= 0.0f)
                continue;

            float h = gearTop - radius * 0.04f * (1.0f - roundOver (edge / edgeRound));
            s.setMaterial (x, y, blackGear, 1.0f);

            // The tooth it points with is painted white
            const float offAxis = std::abs (std::remainder (std::atan2 (d.x, -d.y) - angle, 2.0f * pi)) * r;
            const float toothWidth = 2.0f * pi * radius * gearRoot / (float) gearTeeth * toothRoot;
            const float marked = clamp01 ((juce::jmax (toothWidth, (toothHalfWidth (u) + 0.02f) * pitch) - offAxis) * scale + 0.5f);
            h += radius * 0.008f * marked;
            s.setMaterial (x, y, whitePaint, marked);

            // White enamelled dome, with a small nub at its crown
            const float domeCover = clamp01 ((domeRadius - r) * scale + 0.5f);

            if (domeCover > 0.0f)
            {
                const float rr = juce::jmin (r, domeRadius);
                float dome = gearTop + std::sqrt (sphere * sphere - rr * rr) - (sphere - domeHeight);
                dome += radius * 0.02f * (1.0f - smooth ((domeRadius - r) / (radius * 0.03f)));  // rolled lip
                const float nub = radius * 0.07f;
                dome += radius * 0.025f * std::sqrt (juce::jmax (0.0f, 1.0f - juce::square (r / nub)));
                h += (dome - h) * domeCover;
                s.setMaterial (x, y, materials::whiteEnamel, domeCover);
            }

            s.heightAt (x, y) = h;
        }

    return s.render (false, false);
}

juce::Path wheelOutline (juce::Point<float> centre, float radius, float angle)
{
    // Tooth flanks: where the half width equals the phase
    juce::Path path;
    constexpr int steps = gearTeeth * 24;

    for (int i = 0; i <= steps; ++i)
    {
        const float a = angle + 2.0f * pi * (float) i / (float) steps;
        const float phase = std::abs (toothPhase (a - angle));
        const float u = 1.0f - (phase - toothTip) / (toothRoot - toothTip) * (1.0f - gearRoot);
        const auto q = onCircle (centre, radius * juce::jlimit (gearRoot, 1.0f, u), a);
        i == 0 ? path.startNewSubPath (q) : path.lineTo (q);
    }

    path.closeSubPath();
    return path;
}
}

void KnobLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                 float sliderPos, float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height);
    const auto local = bounds.toFloat();
    const auto centre = local.getCentre();
    const float radius = juce::jmin (local.getWidth(), local.getHeight()) * 0.5f - knobShadowMargin;
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    // Shadows on the dial plate: a tight contact shadow and a broad soft one
    // falling away from the window
    const auto outline = wheelOutline (centre, radius, angle);
    juce::DropShadow (juce::Colours::black.withAlpha (0.38f), 12, { 5, 9 }).drawForPath (g, outline);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 3, { 2, 3 }).drawForPath (g, outline);

    if (cached.isNull() || cachedAngle != angle || cachedScale != scale || cachedBounds != bounds)
    {
        // Render in editor coordinates so the knob is lit by the same window
        // as the panel.
        const auto offset = slider.getPosition().toFloat();
        cached = renderKnob (local + offset, centre + offset, radius, angle, scale);
        cachedAngle = angle;
        cachedScale = scale;
        cachedBounds = bounds;
    }

    g.setOpacity (1.0f);  // the shadows leave their alpha behind
    g.drawImage (cached, local);
}
}
