#pragma once

#include "PluginProcessor.h"
#include "plumppu/PlumppuLook.h"

class PlumppuLagomEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit PlumppuLagomEditor (PlumppuLagomProcessor&);
    ~PlumppuLagomEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // The three-way mode switch: an invisible hit area over the lever, which
    // the editor paints in panel coordinates so it shares the panel's light
    class ModeSwitch final : public juce::Component
    {
    public:
        ModeSwitch (juce::RangedAudioParameter&, std::function<void()> onChange);
        int getPosition() const noexcept { return position; }
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        void pick (juce::Point<float> local);
        juce::ParameterAttachment attachment;
        std::function<void()> changed;
        int position = 1;
    };

    void timerCallback() override;
    void renderFaceplate();

    PlumppuLagomProcessor& processor;
    plumppu::KnobLook knobLook;
    juce::Slider knob;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    ModeSwitch modeSwitch;
    juce::Image faceplate;
    float shownRide = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlumppuLagomEditor)
};
