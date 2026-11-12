// The Radiomix console: a white plastic panel with the sticker logo, a raised
// black-glass file display with a Dymo label under it, the output selector (a
// 1970s slide switch) and the MIX key (a 1970s latching transport key in a
// well), and a raised black-glass status display with a fluorescent bar.
//
// The relief is rendered with the shared Plumppu Surface, once per state of
// the parts that move (key up/down, selector position, whether REPLACE SOUND
// is available); the displays are drawn live on top.
#pragma once

#include "plumppu/PlumppuLook.h"

namespace radiomix
{
struct DisplayState
{
    juce::String fileName;      // big line of the file display
    juce::String info;          // small line, left
    juce::String state;         // small line, right
    juce::String status;        // status display, left
    juce::String statusAction;  // status display, right (CANCEL, SHOW IN FOLDER)
    float progress = -1.0f;     // 0..1 lights the bar; < 0 leaves it dark
    bool fileIsPlaceholder = false;
};

class Faceplate
{
public:
    static constexpr int width = 480, height = 530;
    static constexpr int loudnessStops = 5;   // -18 .. -14 LUFS

    Faceplate();

    // The lit relief (2x), for one combination of moving parts. The loudness
    // slider's tab is left out (loudnessTab < 0) and drawn live with
    // paintLoudnessTab, from a sprite cut out of one render with it in place.
    juce::Image render (bool keyDown, int selector, bool replaceAvailable, int loudnessTab = -1) const;
    void paintLoudnessTab (juce::Graphics&, int position) const;

    // The live layer: display texts and the progress bar
    void paintDisplays (juce::Graphics&, const DisplayState&) const;

    // Hit areas, in logical coordinates
    static juce::Rectangle<float> fileDisplayArea();
    static juce::Rectangle<float> keyArea();
    static juce::Rectangle<float> selectorArea();
    static int selectorPositionAt (float x);
    static juce::Rectangle<float> loudnessArea();
    static int loudnessPositionAt (float x);
    static juce::Rectangle<float> statusActionArea();

private:
    struct MixArt { juce::Path outline, shadow, letters; };
    MixArt mix;
    std::unique_ptr<juce::Drawable> logo;
    juce::Typeface::Ptr matrixPrint, matrixMonoSC;
    mutable juce::Image tabSprite;
    mutable juce::CriticalSection spriteLock;

    void drawDotText (juce::Graphics&, const juce::String&, juce::Typeface::Ptr, float height,
                      juce::Rectangle<float>, juce::Justification, float alpha) const;
};
}
