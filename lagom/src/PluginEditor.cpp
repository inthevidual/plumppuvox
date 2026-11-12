#include "PluginEditor.h"

namespace
{
constexpr int editorWidth = 420;
constexpr int editorHeight = 440;

// Same layout as PlumppuVox: the cross sits left of centre, as on the flag,
// and the knob sits on the crossing.
const plumppu::PanelLayout panelLayout { { 150.0f, 238.0f }, 50.0f };
constexpr float dialRadius = 104.0f;
constexpr float knobRadius = 60.0f;
constexpr float angleStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float angleEnd = juce::MathConstants<float>::pi * 2.75f;

// Right-hand column
constexpr float columnCentre = 331.0f;
// The readout sits below the two-line logo (buildLogo ends at y = 121)
constexpr float logoTop = 28.0f;
const juce::Rectangle<float> readoutArea { columnCentre - 46.0f, 163.0f, 92.0f, 36.0f };
const juce::Rectangle<float> lampArea { columnCentre - 70.0f, 300.0f, 140.0f, 13.0f };

// Ride meter: centre zero, two dB per lamp each way
constexpr int lampCount = 8;
constexpr float dbPerLamp = 2.0f;

// Mode switch below the meter: Lite, Lagom and Max, leaning left, up, right
const juce::Point<float> switchCentre { 331.0f, 384.0f };
constexpr float switchAngles[] = { -1.0f, 0.0f, 1.0f };
const juce::Rectangle<float> switchArea { switchCentre.x - 70.0f, switchCentre.y - 52.0f, 140.0f, 72.0f };

// The Swedish flag in the deep blue it had before the 1906 flag law: blue
// powder coat, yellow cross, yellow legends
namespace swedish
{
const juce::Colour blue { 0xff003a6b }, yellow { 0xfffecc02 };
}

plumppu::Material coat() { return { swedish::blue, 0.045f, 0.40f }; }
plumppu::Material crossPaint() { return { swedish::yellow, 0.05f, 0.16f }; }
plumppu::Material yellowPaint() { return { swedish::yellow, 0.045f, 0.34f }; }
plumppu::Material bluePaint() { return { swedish::blue, 0.045f, 0.34f }; }
}

PlumppuLagomEditor::PlumppuLagomEditor (PlumppuLagomProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      attachment (p.parameters, "target", knob),
      modeSwitch (*p.parameters.getParameter ("mode"), [this] { repaint (switchArea.toNearestInt()); })
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setRotaryParameters (angleStart, angleEnd, true);
    knob.setDoubleClickReturnValue (true, plumppu::Rider::defaultTarget);
    knob.setMouseDragSensitivity (220);
    knob.setLookAndFeel (&knobLook);
    knob.setTitle ("Target loudness");
    knob.onValueChange = [this] { repaint(); };
    addAndMakeVisible (knob);
    modeSwitch.setTitle ("Mode");
    addAndMakeVisible (modeSwitch);

    setSize (editorWidth, editorHeight);
    startTimerHz (30);
}

PlumppuLagomEditor::~PlumppuLagomEditor()
{
    knob.setLookAndFeel (nullptr);
}

void PlumppuLagomEditor::resized()
{
    const int size = juce::roundToInt ((knobRadius + plumppu::knobShadowMargin) * 2.0f);
    knob.setBounds (juce::roundToInt (panelLayout.crossCentre.x) - size / 2,
                    juce::roundToInt (panelLayout.crossCentre.y) - size / 2, size, size);
    modeSwitch.setBounds (switchArea.toNearestInt());
    renderFaceplate();
}

void PlumppuLagomEditor::timerCallback()
{
    const float target = processor.rideDb.load (std::memory_order_relaxed);
    const float next = shownRide + (target - shownRide) * 0.3f;

    if (std::abs (next - shownRide) > 0.02f)
    {
        shownRide = next;
        repaint (lampArea.expanded (20.0f).toNearestInt());
    }
}

// Everything that never changes is built once as a lit relief.
void PlumppuLagomEditor::renderFaceplate()
{
    using namespace plumppu;
    Surface s ({ 0.0f, 0.0f, (float) editorWidth, (float) editorHeight }, 2.0f);
    const auto yellow = yellowPaint();

    buildPanel (s, { 0.0f, 0.0f, (float) editorWidth, (float) editorHeight }, panelLayout, { coat(), crossPaint() });
    buildDialPlate (s, panelLayout.crossCentre, dialRadius, angleStart, angleEnd,
                    { "-30", "-26", "-22", "-18", "-14", "-10" }, 4);

    buildLogo (s, (float) editorWidth - 34.0f, logoTop, "LAGOM", "VOLUME RIDER", yellow, bluePaint());

    const auto heading = labelFont (10.0f, true, 0.24f);
    engraveText (s, "TARGET LUFS", heading, { columnCentre - 70.0f, readoutArea.getY() - 23.0f, 140.0f, 12.0f },
                 juce::Justification::centred, yellow);
    engraveText (s, "RIDE", heading, { columnCentre - 70.0f, lampArea.getY() - 21.0f, 140.0f, 12.0f },
                 juce::Justification::centred, yellow);

    buildReadoutBezel (s, readoutArea);
    buildLampSockets (s, lampArea, lampCount);

    // Ride scale in dB, at the lamp boundaries
    const float pitch = lampArea.getWidth() / (float) lampCount;

    for (int k = 0; k <= lampCount; k += 2)
    {
        const int db = (int) ((float) (k - lampCount / 2) * dbPerLamp);
        const auto text = db > 0 ? "+" + juce::String (db) : juce::String (db);
        engraveText (s, text, labelFont (8.5f, true),
                     { lampArea.getX() + pitch * (float) k - 12.0f, lampArea.getBottom() + 6.0f, 24.0f, 10.0f },
                     juce::Justification::centred, yellow);
    }

    // Mode switch: the ring nut and the three positions around it
    buildToggleRing (s, switchCentre);
    const char* modeNames[] = { "LITE", "LAGOM", "MAX" };
    for (int i = 0; i < 3; ++i)
    {
        const float a = switchAngles[i], r = i == 1 ? 40.0f : 47.0f;
        const auto at = switchCentre + juce::Point<float> (std::sin (a) * r, -std::cos (a) * r);
        engraveText (s, modeNames[i], labelFont (8.5f, true, 0.18f), juce::Rectangle<float> (50.0f, 10.0f).withCentre (at),
                     juce::Justification::centred, yellow);
    }

    // Type plate
    engraveText (s, "TYPE", labelFont (8.5f, true, 0.2f), { 34.0f, 384.0f, 80.0f, 10.0f },
                 juce::Justification::centredLeft, yellow);
    engraveText (s, "PL-1", labelFont (13.0f, true, 0.08f), { 34.0f, 395.0f, 80.0f, 15.0f },
                 juce::Justification::centredLeft, yellow);

    faceplate = s.render (true, true);
}

void PlumppuLagomEditor::paint (juce::Graphics& g)
{
    g.drawImage (faceplate, getLocalBounds().toFloat());
    plumppu::drawReadout (g, readoutArea, juce::String (juce::roundToInt (knob.getValue())));
    plumppu::drawToggleLever (g, switchCentre, switchAngles[modeSwitch.getPosition()]);

    // Lamps light outwards from the centre: right for boost, left for cut
    const float ride = shownRide / dbPerLamp;
    const int half = lampCount / 2;
    plumppu::drawLamps (g, lampArea, lampCount, [ride, half] (int i)
    {
        return i >= half ? ride - (float) (i - half) : -ride - (float) (half - 1 - i);
    });
}

//==============================================================================
PlumppuLagomEditor::ModeSwitch::ModeSwitch (juce::RangedAudioParameter& parameter, std::function<void()> onChange)
    : attachment (parameter, [this] (float value)
      {
          position = juce::jlimit (0, 2, juce::roundToInt (value));
          if (changed)
              changed();
      }),
      changed (std::move (onChange))
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void PlumppuLagomEditor::ModeSwitch::pick (juce::Point<float> local)
{
    // Left of the pivot is Lite, right of it Max, in between Lagom
    const float dx = local.x + (float) getX() - switchCentre.x;
    const int wanted = dx < -12.0f ? 0 : dx > 12.0f ? 2 : 1;
    if (wanted != position)
        attachment.setValueAsCompleteGesture ((float) wanted);
}

void PlumppuLagomEditor::ModeSwitch::mouseDown (const juce::MouseEvent& e) { pick (e.position); }
void PlumppuLagomEditor::ModeSwitch::mouseDrag (const juce::MouseEvent& e) { pick (e.position); }
