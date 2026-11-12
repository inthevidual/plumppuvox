#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Resampler.h"
#include "plumppuvox_engine.h"

class PlumppuVoxProcessor final : public juce::AudioProcessor
{
public:
    PlumppuVoxProcessor();
    ~PlumppuVoxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PlumppuVox Ren"; }
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

    // How much level the plugin is currently removing, in dB (>= 0). For the meter.
    std::atomic<float> reductionDb { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void run (juce::AudioBuffer<float>& buffer, float amount);

    std::atomic<float>* amountParam = nullptr;

    PvEngine* engine = nullptr;
    int engineChannels = 0;
    int hop = 480;
    bool resampling = false;

    std::vector<SincResampler> toModel, fromModel;
    std::vector<SampleFifo> modelIn, hostOut;
    std::vector<float> frameIn, frameOut;

    float lastAmount = -1.0f;
    double meterIn = 0.0, meterOut = 0.0;
    double meterCoeff = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PlumppuVoxProcessor)
};
