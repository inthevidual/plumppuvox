// Development tool: renders audio through the processor offline, checks the
// reported latency, tests the rider and writes a screenshot of the editor.
//
//   plumppulagom_tool snapshot out.png [lite|lagom|max]
//   plumppulagom_tool render in.wav out.wav [target LUFS] [block size] [lite|lagom|max]
//   plumppulagom_tool latency [sample rate] [block size]
//   plumppulagom_tool selfcheck

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <chrono>
#include <iostream>

namespace
{
int snapshot (const juce::File& out, const juce::String& mode)
{
    PlumppuLagomProcessor processor;
    {
        auto* param = processor.parameters.getParameter ("mode");
        const int index = mode == "lite" ? 0 : mode == "max" ? 2 : 1;
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);

    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    return stream.openedOk() && png.writeImageToStream (image, stream) ? 0 : 1;
}

void setTarget (PlumppuLagomProcessor& processor, float lufs)
{
    auto* param = processor.parameters.getParameter ("target");
    param->setValueNotifyingHost (param->convertTo0to1 (lufs));
}

int render (const juce::File& in, const juce::File& out, float target, int blockSize, const juce::String& mode)
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

    PlumppuLagomProcessor processor;
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    processor.setBusesLayout ({ { set }, { set } });
    setTarget (processor, target);
    {
        auto* param = processor.parameters.getParameter ("mode");
        const int index = mode == "lite" ? 0 : mode == "max" ? 2 : 1;
        param->setValueNotifyingHost (param->convertTo0to1 ((float) index));
    }
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
              << audioSeconds / seconds << "x real time), latency " << latency << " samples\n";

    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (out);
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (reader->sampleRate)
                                                   .withNumChannels (channels)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return 1;

    // Drop the latency so the output lines up with the input.
    writer->writeFromAudioSampleBuffer (result, latency, length);
    return 0;
}

// Before any speech has been heard the gain is 0 dB, so an impulse over
// faint noise comes out unchanged and shows the true latency.
int latency (double sampleRate, int blockSize)
{
    int failures = 0;

    for (int channels = 1; channels <= 2; ++channels)
    {
        PlumppuLagomProcessor processor;
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        processor.setBusesLayout ({ { set }, { set } });
        processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        const int impulseAt = 5000;
        const int total = impulseAt + processor.getLatencySamples() + 4 * 8192;
        juce::Random random (7);
        juce::MidiBuffer midi;
        int peakAt = -1;
        float peak = 0.0f;

        for (int pos = 0; pos < total; pos += blockSize)
        {
            juce::AudioBuffer<float> block (channels, blockSize);

            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < blockSize; ++i)
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
        const bool ok = measured == reported && peak > 0.7f;
        std::cout << sampleRate << " Hz, block " << blockSize << ", " << channels << " ch: reported " << reported
                  << ", measured " << measured << " (peak " << peak << ") " << (ok ? "OK" : "FAIL") << "\n";
        failures += ok ? 0 : 1;
    }

    return failures == 0 ? 0 : 1;
}

// Integrated loudness as a loudness meter shows it (BS.1770-4): K-weighted
// 400 ms blocks every 100 ms, gated at -70 LUFS and then 10 LU below the
// mean of the blocks that passed.
double integratedLoudness (const std::vector<float>& x, double sr)
{
    juce::IIRFilter shelf, highpass;
    {
        const double pi = juce::MathConstants<double>::pi;
        const double A = std::pow (10.0, 4.0 / 40.0), w0 = 2 * pi * 1500.0 / sr;
        const double c = std::cos (w0), alpha = std::sin (w0) / (2 / std::sqrt (2.0)), sa = 2 * std::sqrt (A) * alpha;
        shelf.setCoefficients (juce::IIRCoefficients (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
                                                      A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
                                                      2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa));
        const double w1 = 2 * pi * 38.0 / sr, c1 = std::cos (w1), a1 = std::sin (w1);
        highpass.setCoefficients (juce::IIRCoefficients ((1 + c1) / 2, -(1 + c1), (1 + c1) / 2, 1 + a1, -2 * c1, 1 - a1));
    }

    const int step = (int) (sr * 0.1);
    std::vector<double> quarter;   // mean square per 100 ms
    double acc = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double y = highpass.processSingleSampleRaw (shelf.processSingleSampleRaw (x[i]));
        acc += y * y;
        if ((int) (i % (size_t) step) == step - 1)
        {
            quarter.push_back (acc / step);
            acc = 0.0;
        }
    }

    std::vector<double> blocks;
    for (size_t i = 3; i < quarter.size(); ++i)
        blocks.push_back ((quarter[i] + quarter[i - 1] + quarter[i - 2] + quarter[i - 3]) / 4.0);

    const auto lufs = [] (double ms) { return -0.691 + 10.0 * std::log10 (ms + 1e-20); };
    const auto meanAbove = [&] (double gate)
    {
        double sum = 0.0;
        int n = 0;
        for (double b : blocks)
            if (lufs (b) > gate) { sum += b; ++n; }
        return n > 0 ? sum / n : 0.0;
    };
    return lufs (meanAbove (lufs (meanAbove (-70.0)) - 10.0));
}

// Speech-like test signal: bursts of filtered noise (talk spurts with
// pauses), measured before and after the rider.
int selfcheck()
{
    using namespace plumppu;
    int failures = 0;

    const double sr = 48000.0;
    const int seconds = 60;
    const int n = (int) sr * seconds;
    std::vector<float> x ((size_t) n);
    juce::Random random (1234);
    juce::IIRFilter band;
    band.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 700.0, 0.7));

    // Spurts of 0.2-1.5 s, pauses of 0.1-0.8 s, over a faint noise floor
    for (int i = 0; i < n;)
    {
        const int on = (int) (sr * (0.2 + 1.3 * random.nextDouble()));
        const int off = (int) (sr * (0.1 + 0.7 * random.nextDouble()));
        for (int k = 0; k < on && i < n; ++k, ++i)
        {
            x[(size_t) i] = 0.25f * band.processSingleSampleRaw (random.nextFloat() * 2.0f - 1.0f);
        }
        for (int k = 0; k < off && i < n; ++k, ++i)
            x[(size_t) i] = 0.0003f * (random.nextFloat() * 2.0f - 1.0f);
    }

    const double before = integratedLoudness (x, sr);

    for (float target : { -24.0f, -18.0f, -12.0f })
    {
        Rider rider;
        rider.prepare (sr, 1);
        rider.setTarget (target);
        std::vector<float> y (x);
        y.resize ((size_t) (n + rider.getLatencySamples()), 0.0f);
        for (int pos = 0; pos < (int) y.size(); pos += 512)
        {
            float* ptr = y.data() + pos;
            rider.process (&ptr, 1, juce::jmin (512, (int) y.size() - pos));
        }
        y.erase (y.begin(), y.begin() + rider.getLatencySamples());

        // Judge the second half, after the long-term estimate has settled
        const std::vector<float> late (y.begin() + n / 2, y.begin() + n);
        const double after = integratedLoudness (late, sr);
        float peak = 0.0f;
        for (float v : y)
            peak = juce::jmax (peak, std::abs (v));
        const bool ok = std::abs (after - target) < 1.0 && peak <= 0.8913f;
        std::cout << "speech-like input at " << juce::String (before, 1) << " LUFS, target " << target << ": output "
                  << juce::String (after, 1) << " LUFS, peak " << juce::String (20.0 * std::log10 (peak), 1)
                  << " dBFS " << (ok ? "OK" : "FAIL") << "\n";
        failures += ok ? 0 : 1;
    }

    // The interface must contain the blue and the yellow of the flag
    PlumppuLagomProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    int blue = 0, yellow = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x2 = 0; x2 < image.getWidth(); ++x2)
        {
            const auto c = image.getPixelAt (x2, y);
            blue += c.getBlue() > 80 && c.getBlue() > c.getRed() + 60 ? 1 : 0;
            yellow += c.getRed() > 150 && c.getGreen() > 110 && c.getBlue() < 80 ? 1 : 0;
        }
    const int area = image.getWidth() * image.getHeight();
    const bool coloursOk = blue > area / 5 && yellow > area / 50;
    std::cout << "interface: " << blue << " blue, " << yellow << " yellow pixels " << (coloursOk ? "OK" : "FAIL") << "\n";
    failures += coloursOk ? 0 : 1;

    return failures == 0 ? 0 : 1;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::StringArray args (argv + 1, argc - 1);
    const auto file = [] (const juce::String& path) { return juce::File::getCurrentWorkingDirectory().getChildFile (path); };

    if (args.size() >= 2 && args[0] == "snapshot")
        return snapshot (file (args[1]), args.size() > 2 ? args[2] : juce::String ("lagom"));

    if (args.size() >= 3 && args[0] == "render")
        return render (file (args[1]), file (args[2]),
                       args.size() > 3 ? args[3].getFloatValue() : plumppu::Rider::defaultTarget,
                       args.size() > 4 ? args[4].getIntValue() : 512,
                       args.size() > 5 ? args[5] : juce::String ("lagom"));

    if (args.size() >= 1 && args[0] == "latency")
        return latency (args.size() > 1 ? args[1].getDoubleValue() : 48000.0,
                        args.size() > 2 ? args[2].getIntValue() : 512);

    if (args.size() >= 1 && args[0] == "selfcheck")
        return selfcheck();

    std::cerr << "usage: plumppulagom_tool snapshot|render|latency|selfcheck ...\n";
    return 2;
}
