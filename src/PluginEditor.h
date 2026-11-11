#pragma once

#include "PluginProcessor.h"
#include "plumppu/PlumppuLook.h"

class PlumppuVoxEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit PlumppuVoxEditor (PlumppuVoxProcessor&);
    ~PlumppuVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void renderFaceplate();

    PlumppuVoxProcessor& processor;
    plumppu::KnobLook knobLook;
    juce::Slider knob;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
    juce::Image faceplate;
    float shownReduction = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlumppuVoxEditor)
};
