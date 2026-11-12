#include "PluginProcessor.h"
#include "PluginEditor.h"

PlumppuLagomProcessor::PlumppuLagomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PlumppuLagom", createLayout())
{
    targetParam = parameters.getRawParameterValue ("target");
    modeParam = parameters.getRawParameterValue ("mode");
}

juce::AudioProcessorValueTreeState::ParameterLayout PlumppuLagomProcessor::createLayout()
{
    using plumppu::Rider;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "target", 1 },
        "Target",
        juce::NormalisableRange<float> (Rider::minTarget, Rider::maxTarget, 1.0f),
        Rider::defaultTarget,
        juce::AudioParameterFloatAttributes()
            .withLabel ("LUFS")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "mode", 1 },
        "Mode",
        juce::StringArray { "Lite", "Lagom", "Max" },
        1));
    return layout;
}

bool PlumppuLagomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == layouts.getMainInputChannelSet()
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void PlumppuLagomProcessor::prepareToPlay (double sampleRate, int)
{
    rider.prepare (sampleRate, juce::jlimit (1, 2, getTotalNumInputChannels()));
    setLatencySamples (rider.getLatencySamples());
}

void PlumppuLagomProcessor::run (juce::AudioBuffer<float>& buffer, bool bypassed)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    rider.setTarget (targetParam->load (std::memory_order_relaxed));
    rider.setMode ((plumppu::Rider::Mode) juce::jlimit (0, 2, juce::roundToInt (modeParam->load (std::memory_order_relaxed))));
    rider.setBypassed (bypassed);
    rider.process (buffer.getArrayOfWritePointers(),
                   juce::jmin (buffer.getNumChannels(), getTotalNumInputChannels()),
                   buffer.getNumSamples());

    gainDb.store (rider.getGainDb(), std::memory_order_relaxed);
    rideDb.store (rider.getRideDb(), std::memory_order_relaxed);
    strength.store (rider.getStrength(), std::memory_order_relaxed);
    speech.store (rider.isSpeech(), std::memory_order_relaxed);
}

void PlumppuLagomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    run (buffer, false);
}

void PlumppuLagomProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep the delay, so bypassing does not shift timing against the latency
    run (buffer, true);
}

juce::AudioProcessorEditor* PlumppuLagomProcessor::createEditor()
{
    return new PlumppuLagomEditor (*this);
}

void PlumppuLagomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PlumppuLagomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlumppuLagomProcessor();
}
