#include "PluginEditor.h"

namespace
{
constexpr int editorWidth = 420;
constexpr int editorHeight = 440;

// The cross sits left of centre, as on the flag; the knob sits on the crossing.
const plumppu::PanelLayout panelLayout { { 150.0f, 238.0f }, 50.0f };
constexpr float dialRadius = 104.0f;
constexpr float knobRadius = 60.0f;
constexpr float angleStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float angleEnd = juce::MathConstants<float>::pi * 2.75f;

// Right-hand column
constexpr float columnCentre = 331.0f;
const juce::Rectangle<float> readoutArea { columnCentre - 46.0f, 138.0f, 92.0f, 36.0f };
const juce::Rectangle<float> lampArea { columnCentre - 70.0f, 300.0f, 140.0f, 13.0f };
constexpr int lampCount = 10;
constexpr float dbPerLamp = 3.0f;
}

PlumppuVoxEditor::PlumppuVoxEditor (PlumppuVoxProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      attachment (p.parameters, "amount", knob)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (angleStart, angleEnd, true);
    knob.setDoubleClickReturnValue (true, 80.0);
    knob.setMouseDragSensitivity (220);
    knob.setLookAndFeel (&knobLook);
    knob.setTitle ("Amount");
    knob.onValueChange = [this] { repaint(); };
    addAndMakeVisible (knob);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

PlumppuVoxEditor::~PlumppuVoxEditor()
{
    knob.setLookAndFeel (nullptr);
}

void PlumppuVoxEditor::resized()
{
    const int size = juce::roundToInt ((knobRadius + plumppu::knobShadowMargin) * 2.0f);
    knob.setBounds (juce::roundToInt (panelLayout.crossCentre.x) - size / 2,
                    juce::roundToInt (panelLayout.crossCentre.y) - size / 2, size, size);
    renderFaceplate();
}

void PlumppuVoxEditor::timerCallback()
{
    const float target = processor.reductionDb.load (std::memory_order_relaxed);
    const float next = target > shownReduction ? target : shownReduction + (target - shownReduction) * 0.25f;

    if (std::abs (next - shownReduction) > 0.05f)
    {
        shownReduction = next;
        repaint (lampArea.expanded (20.0f).toNearestInt());
    }
}

// Everything that never changes is built once as a lit relief.
void PlumppuVoxEditor::renderFaceplate()
{
    using namespace plumppu;
    Surface s ({ 0.0f, 0.0f, (float) editorWidth, (float) editorHeight }, 2.0f);

    buildPanel (s, { 0.0f, 0.0f, (float) editorWidth, (float) editorHeight }, panelLayout);
    buildDialPlate (s, panelLayout.crossCentre, dialRadius, angleStart, angleEnd,
                    { "0", "20", "40", "60", "80", "100" });

    // Logo: "Plumppu" engraved and filled blue; "Vox" on a painted blue badge,
    // engraved and filled with white paint
    {
        const auto font = juce::Font (juce::FontOptions (30.0f, juce::Font::bold)).withExtraKerningFactor (-0.01f);
        const juce::String first ("Plumppu"), second ("Vox");
        const float w1 = juce::GlyphArrangement::getStringWidth (font, first);
        const float w2 = juce::GlyphArrangement::getStringWidth (font, second);
        const float pad = 7.0f, gap = 5.0f;
        const float right = (float) editorWidth - 34.0f;
        const float x = right - (w1 + gap + w2 + 2 * pad);
        const float y = 30.0f, h = 38.0f;

        engraveText (s, first, font, { x, y, w1 + 2.0f, h }, juce::Justification::centredLeft, materials::bluePaint, 0.8f);

        const juce::Rectangle<float> badge (x + w1 + gap, y + 1.0f, w2 + 2 * pad, h - 2.0f);
        paintOn (s, s.mask ([&] (juce::Graphics& g) { g.fillRoundedRectangle (badge, 5.0f); }), materials::bluePaint);
        engraveText (s, second, font, badge.translated (0.0f, -1.0f), juce::Justification::centred, materials::whiteInk, 0.8f);

        engraveText (s, "VOICE ISOLATOR", labelFont (9.5f, true, 0.30f), { x, y + h + 3.0f, right - x, 12.0f },
                     juce::Justification::centredRight);
        engrave (s, s.mask ([&] (juce::Graphics& g) { g.fillRect (x, y + h + 20.0f, right - x, 1.0f); }),
                 materials::engravingFill, 0.6f);
    }

    const auto heading = labelFont (10.0f, true, 0.24f);
    engraveText (s, "AMOUNT", heading, { columnCentre - 70.0f, readoutArea.getY() - 23.0f, 140.0f, 12.0f }, juce::Justification::centred);
    engraveText (s, "REDUCTION", heading, { columnCentre - 70.0f, lampArea.getY() - 21.0f, 140.0f, 12.0f }, juce::Justification::centred);

    buildReadoutBezel (s, readoutArea);
    buildLampSockets (s, lampArea, lampCount);

    // Lamp scale in dB
    const float pitch = lampArea.getWidth() / (float) lampCount;

    for (int i = 0; i < lampCount; i += 3)
        engraveText (s, juce::String ((i + 1) * (int) dbPerLamp), labelFont (8.5f, true),
                     { lampArea.getX() + pitch * ((float) i + 0.5f) - 12.0f, lampArea.getBottom() + 6.0f, 24.0f, 10.0f },
                     juce::Justification::centred);

    // Type plate
    engraveText (s, "TYPE", labelFont (8.5f, true, 0.2f), { 34.0f, 384.0f, 80.0f, 10.0f }, juce::Justification::centredLeft);
    engraveText (s, "PV-1", labelFont (13.0f, true, 0.08f), { 34.0f, 395.0f, 80.0f, 15.0f }, juce::Justification::centredLeft);

    faceplate = s.render (true, true);
}

void PlumppuVoxEditor::paint (juce::Graphics& g)
{
    g.drawImage (faceplate, getLocalBounds().toFloat());
    plumppu::drawReadout (g, readoutArea, juce::String (juce::roundToInt (knob.getValue())));
    plumppu::drawLampRow (g, lampArea, lampCount, shownReduction / dbPerLamp);
}
