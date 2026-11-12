#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Rider.h"

class PlumppuLagomProcessor final : public juce::AudioProcessor
{
public:
    PlumppuLagomProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PlumppuVox Lagom"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    // For the editor's meters
    std::atomic<float> gainDb { 0.0f }, rideDb { 0.0f }, strength { 0.75f };
    std::atomic<bool> speech { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run (juce::AudioBuffer<float>&, bool bypassed);

    std::atomic<float>* targetParam = nullptr;
    std::atomic<float>* modeParam = nullptr;
    plumppu::Rider rider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlumppuLagomProcessor)
};
