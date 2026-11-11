// Development tool: renders audio through the processor offline, checks the
// reported latency and writes a screenshot of the editor.
//
//   plumppuvox_tool snapshot out.png
//   plumppuvox_tool render in.wav out.wav [amount 0-100] [block size]
//   plumppuvox_tool latency [sample rate] [block size]

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <chrono>
#include <iostream>

namespace
{
int snapshot (const juce::File& out)
{
    PlumppuVoxProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream) ? 0 : 1;
}

void setAmount (PlumppuVoxProcessor& processor, float amount)
{
    auto* param = processor.parameters.getParameter ("amount");
    param->setValueNotifyingHost (param->convertTo0to1 (amount));
}

int render (const juce::File& in, const juce::File& out, float amount, int blockSize)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (in));

    if (reader == nullptr)
    {
        std::cerr << "cannot read " << in.getFullPathName() << "\n";
        return 1;
    }

    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) reader->lengthInSamples;

    PlumppuVoxProcessor processor;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    processor.setBusesLayout ({ { set }, { set } });
    setAmount (processor, amount);
    processor.setRateAndBufferSizeDetails (reader->sampleRate, blockSize);
    processor.prepareToPlay (reader->sampleRate, blockSize);
    const int latency = processor.getLatencySamples();

    juce::AudioBuffer<float> source (channels, length + latency + blockSize);
    source.clear();
    reader->read (&source, 0, length, 0, true, channels > 1);
    juce::AudioBuffer<float> result (channels, source.getNumSamples());
    result.clear();
    juce::MidiBuffer midi;

    const auto start = std::chrono::steady_clock::now();

    for (int pos = 0; pos + blockSize <= source.getNumSamples(); pos += blockSize)
    {
        juce::AudioBuffer<float> block (channels, blockSize);
        for (int ch = 0; ch < channels; ++ch)
            block.copyFrom (ch, 0, source, ch, pos, blockSize);
        processor.processBlock (block, midi);
        for (int ch = 0; ch < channels; ++ch)
            result.copyFrom (ch, pos, block, ch, 0, blockSize);
    }

    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    const double audioSeconds = source.getNumSamples() / reader->sampleRate;
    std::cout << "rendered " << audioSeconds << " s of audio in " << seconds << " s ("
              << audioSeconds / seconds << "x real time, " << 100.0 * seconds / audioSeconds
              << "% of one core), latency " << latency << " samples\n";

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (reader->sampleRate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return 1;

    // Drop the latency so the output lines up with the input.
    writer->writeFromAudioSampleBuffer (result, latency, length);
    return 0;
}

// With the dial at zero the plugin is a pure delay, so an impulse shows the
// true latency. It must equal what the plugin reports to the host.
int latency (double sampleRate, int blockSize)
{
    int failures = 0;

    for (int channels = 1; channels <= 2; ++channels)
    {
        PlumppuVoxProcessor processor;
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        processor.setBusesLayout ({ { set }, { set } });
        setAmount (processor, 0.0f);
        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const int impulseAt = 5000;
        const int total = 5000 + 4 * 8192;
        juce::Random random (7);
        juce::MidiBuffer midi;
        int peakAt = -1;
        float peak = 0.0f;

        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::AudioBuffer<float> block (channels, blockSize);

            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i) // low noise keeps the model from skipping silent frames
                    block.setSample (ch, i, (pos + i == impulseAt ? 0.8f : 0.0f) + (random.nextFloat() - 0.5f) * 0.004f);

            processor.processBlock (block, midi);

            for (int i = 0; i < blockSize; ++i)
                if (std::abs (block.getSample (0, i)) > peak)
                {
                    peak = std::abs (block.getSample (0, i));
                    peakAt = pos + i;
                }
        }

        const int measured = peakAt - impulseAt;
        const int reported = processor.getLatencySamples();
        const bool ok = std::abs (measured - reported) <= 1 && peak > 0.3f;
        failures += ok ? 0 : 1;
        std::cout << sampleRate << " Hz, block " << blockSize << ", " << channels << " ch: reported " << reported
                  << ", measured " << measured << " (peak " << peak << ") " << (ok ? "OK" : "MISMATCH") << "\n";
    }

    return failures;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args (argv + 1, argc - 1);
    const auto file = [] (const juce::String& path) { return juce::File::getCurrentWorkingDirectory().getChildFile (path); };

    if (args.size() >= 2 && args[0] == "snapshot")
        return snapshot (file (args[1]));

    if (args.size() >= 3 && args[0] == "render")
        return render (file (args[1]), file (args[2]),
                       args.size() > 3 ? args[3].getFloatValue() : 80.0f,
                       args.size() > 4 ? args[4].getIntValue() : 512);

    if (args.size() >= 1 && args[0] == "latency")
        return latency (args.size() > 1 ? args[1].getDoubleValue() : 48000.0,
                        args.size() > 2 ? args[2].getIntValue() : 512);

    std::cerr << "usage: plumppuvox_tool snapshot|render|latency ...\n";
    return 2;
}
