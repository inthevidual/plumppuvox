#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr double modelRate = 48000.0;

// Maps the 0..1 dial onto the share of untouched input in the output. The
// table is the curve of the original one-knob tool, measured from its renders
// at 0, 10, ..., 100: every setting in between was an exact mix of its fully
// processed output with the input. Matching it makes 80, the default and the
// setting for everyday use, sound like 80 did there. Between the measured
// points the curve is interpolated linearly.
float dryMixFor (float amount)
{
    static constexpr float dry[] = { 1.0f,   0.4167f, 0.2508f, 0.2060f, 0.1780f, 0.1500f,
                                     0.1080f, 0.0660f, 0.0360f, 0.0180f, 0.0f };
    const float pos = juce::jlimit (0.0f, 1.0f, amount) * 10.0f;
    const int i = juce::jmin (9, (int) pos);
    return dry[i] + (dry[i + 1] - dry[i]) * (pos - (float) i);
}
}

PlumppuVoxProcessor::PlumppuVoxProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PlumppuVox", createLayout())
{
    amountParam = parameters.getRawParameterValue ("amount");
}

PlumppuVoxProcessor::~PlumppuVoxProcessor()
{
    if (engine != nullptr)
        plumppuvox_free (engine);
}

juce::AudioProcessorValueTreeState::ParameterLayout PlumppuVoxProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 },
        "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        80.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    return layout;
}

bool PlumppuVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == layouts.getMainInputChannelSet()
        && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void PlumppuVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jlimit (1, 2, getTotalNumInputChannels());

    if (engine == nullptr || engineChannels != channels)
    {
        if (engine != nullptr)
            plumppuvox_free (engine);

        engine = plumppuvox_create ((uint32_t) channels);
        engineChannels = channels;
    }
    else
    {
        plumppuvox_reset (engine);
    }

    hop = engine != nullptr ? (int) plumppuvox_hop_size (engine) : 480;
    const int engineLatency = engine != nullptr ? (int) plumppuvox_latency (engine) : 0;
    lastAmount = -1.0f;

    const double ratio = sampleRate / modelRate; // host samples per model sample
    resampling = std::abs (sampleRate - modelRate) > 1.0;

    toModel.assign ((size_t) channels, {});
    fromModel.assign ((size_t) channels, {});
    modelIn.assign ((size_t) channels, {});
    hostOut.assign ((size_t) channels, {});
    frameIn.assign ((size_t) (channels * hop), 0.0f);
    frameOut.assign ((size_t) (channels * hop), 0.0f);

    // The model consumes fixed frames while the host hands us arbitrary block
    // sizes, so the output queue starts with a cushion that covers one frame.
    const int cushion = resampling ? (int) std::ceil (hop * ratio) + 4 : hop;
    double pipelineDelay = engineLatency * ratio;

    for (int ch = 0; ch < channels; ++ch)
    {
        const auto c = (size_t) ch;

        if (resampling)
        {
            toModel[c].prepare (sampleRate, modelRate);
            fromModel[c].prepare (modelRate, sampleRate);
        }

        const auto capacity = (size_t) (4 * (samplesPerBlock + cushion) + 8192);
        modelIn[c].reset (capacity);
        hostOut[c].reset (capacity);
        hostOut[c].pushZeros ((size_t) cushion);
    }

    if (resampling)
        pipelineDelay += toModel[0].latency() + fromModel[0].latency() * ratio;

    setLatencySamples (juce::roundToInt (pipelineDelay) + cushion);

    meterIn = meterOut = 0.0;
    meterCoeff = std::exp (-(double) samplesPerBlock / (0.25 * sampleRate));
}

void PlumppuVoxProcessor::run (juce::AudioBuffer<float>& buffer, float amount)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int channels = juce::jmin (engineChannels, buffer.getNumChannels(), (int) hostOut.size());

    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (channels == 0 || numSamples == 0)
        return;

    if (engine != nullptr && ! juce::approximatelyEqual (amount, lastAmount))
    {
        plumppuvox_set_dry_mix (engine, dryMixFor (amount));
        lastAmount = amount;
    }

    double energyIn = 0.0, energyOut = 0.0;

    for (int ch = 0; ch < channels; ++ch)
    {
        const float* in = buffer.getReadPointer (ch);
        const auto c = (size_t) ch;

        for (int i = 0; i < numSamples; ++i)
            energyIn += (double) in[i] * in[i];

        if (resampling)
            toModel[c].process (in, numSamples, modelIn[c]);
        else
            modelIn[c].push (in, (size_t) numSamples);
    }

    // With two channels, resampler rounding is identical for both, so the
    // queues always hold the same number of samples.
    while (modelIn[0].size() >= (size_t) hop)
    {
        for (int ch = 0; ch < channels; ++ch)
            modelIn[(size_t) ch].pop (frameIn.data() + ch * hop, (size_t) hop);

        if (engine == nullptr || plumppuvox_process (engine, frameIn.data(), frameOut.data()) == 0)
            frameOut = frameIn;

        for (int ch = 0; ch < channels; ++ch)
        {
            const auto c = (size_t) ch;
            const float* frame = frameOut.data() + ch * hop;

            if (resampling)
                fromModel[c].process (frame, hop, hostOut[c]);
            else
                hostOut[c].push (frame, (size_t) hop);
        }
    }

    for (int ch = 0; ch < channels; ++ch)
    {
        float* out = buffer.getWritePointer (ch);
        hostOut[(size_t) ch].pop (out, (size_t) numSamples);

        for (int i = 0; i < numSamples; ++i)
            energyOut += (double) out[i] * out[i];
    }

    const double n = (double) numSamples * channels;
    meterIn = meterCoeff * meterIn + (1.0 - meterCoeff) * (energyIn / n);
    meterOut = meterCoeff * meterOut + (1.0 - meterCoeff) * (energyOut / n);

    float reduction = 0.0f;

    if (meterIn > 1e-9)
        reduction = (float) juce::jlimit (0.0, 60.0, 10.0 * std::log10 (meterIn / juce::jmax (meterOut, 1e-12)));

    reductionDb.store (reduction, std::memory_order_relaxed);
}

void PlumppuVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    run (buffer, amountParam->load (std::memory_order_relaxed) / 100.0f);
}

void PlumppuVoxProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Keep the audio flowing through the same delay so that bypassing does not
    // shift timing against the reported latency.
    run (buffer, 0.0f);
}

juce::AudioProcessorEditor* PlumppuVoxProcessor::createEditor()
{
    return new PlumppuVoxEditor (*this);
}

void PlumppuVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PlumppuVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PlumppuVoxProcessor();
}
