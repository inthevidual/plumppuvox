// Plumppu Radiomix: drop a recording with one speaker per channel, press MIX.
//
// Command line (no window):
//   PlumppuRadiomix --mix <file> [--output replace|stereo|separate] [--target -14..-18]
//   PlumppuRadiomix --snapshot <out.png> [--state empty|ready|mixing] [--file <f>] [--lang en|sv] [--loudness -14..-18]
//   PlumppuRadiomix --selfcheck
#include "Console.h"
#include "Texts.h"

#include <iostream>

namespace radiomix
{
namespace
{
std::vector<std::vector<float>> readWav (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (f) };
    if (r == nullptr)
        return {};
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    std::vector<std::vector<float>> out ((size_t) b.getNumChannels());
    for (int c = 0; c < b.getNumChannels(); ++c)
        out[(size_t) c].assign (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples());
    return out;
}

// Two mics, one speaker each, taking turns; speaker 2 is 12 dB quieter and
// each mic hears the other speaker 18 dB down. The voices are shaped noise,
// which Ren would (rightly) remove, so Ren's dial is at 0 here: this checks
// the levelling, the automix and the outputs around it.
int selfcheck()
{
    const double sr = 48000.0;
    const int seconds = 90, n = (int) sr * seconds;
    std::vector<float> spk[2] { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    std::vector<bool> who ((size_t) n);
    juce::Random random (4242);
    juce::IIRFilter band[2];
    band[0].setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 600.0, 0.7));
    band[1].setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 900.0, 0.7));
    for (int i = 0, turn = 0; i < n; turn ^= 1)
    {
        const int len = (int) (sr * (2.0 + 3.0 * random.nextDouble()));
        const float level = turn == 0 ? 0.25f : 0.25f * 0.2512f;   // -12 dB
        for (int k = 0; k < len && i < n; ++k, ++i)
        {
            const bool spurt = std::fmod ((double) k / sr, 0.9) < 0.6;   // syllable groups with short gaps
            spk[turn][(size_t) i] = spurt ? level * band[turn].processSingleSampleRaw (random.nextFloat() * 2.0f - 1.0f) : 0.0f;
            who[(size_t) i] = turn == 1;
        }
    }
    std::vector<float> mic[2] { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    for (size_t i = 0; i < (size_t) n; ++i)
    {
        const float hiss0 = 0.0004f * (random.nextFloat() - 0.5f), hiss1 = 0.0004f * (random.nextFloat() - 0.5f);
        mic[0][i] = spk[0][i] + 0.126f * spk[1][i] + hiss0;
        mic[1][i] = spk[1][i] + 0.126f * spk[0][i] + hiss1;
    }

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("radiomix-selfcheck");
    dir.deleteRecursively();
    dir.createDirectory();
    const auto input = dir.getChildFile ("twomics.wav");
    {
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (input);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        const float* ptrs[] = { mic[0].data(), mic[1].data() };
        w->writeFromFloatArrays (ptrs, 2, n);
    }

    int failures = 0;
    const auto check = [&failures] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "OK   " : "FAIL ") << what << "\n";
        failures += ok ? 0 : 1;
    };

    // Stereo track
    auto r = Mixer::mixNow (input, Output::stereoTrack, Mixer::defaultTarget, {}, 0.0f);
    check (r.ok && r.files.size() == 1, "stereo track written");
    if (r.ok)
    {
        auto out = readWav (r.files[0]);
        check (out.size() == 2 && (int) out[0].size() == n, "stereo track: 2 channels, same length as the input");
        if (out.size() == 2)
        {
            const std::vector<float> late (out[0].begin() + n / 3, out[0].end());
            const double lufs = integratedLoudness (late, sr) + 3.01;   // both channels of the dual-mono file
            check (std::abs (lufs + 14.0) < 1.5, "loudness " + juce::String (lufs, 1) + " LUFS (target -14)");
            const float tp = std::max (truePeakDb (out[0]), truePeakDb (out[1]));
            check (tp <= -1.5f, "true peak " + juce::String (tp, 2) + " dBTP (ceiling -1.5)");
            // the quiet speaker brought up to the loud one
            double e[2] = { 0, 0 }; size_t c[2] = { 0, 0 };
            for (size_t i = (size_t) n / 3; i < (size_t) n; ++i)
                if (std::abs (spk[0][i]) + std::abs (spk[1][i]) > 0.0f)
                {
                    e[who[i] ? 1 : 0] += (double) out[0][i] * out[0][i];
                    ++c[who[i] ? 1 : 0];
                }
            const double gap = 10.0 * std::log10 ((e[0] / (double) c[0]) / (e[1] / (double) c[1]));
            check (std::abs (gap) < 4.0, "speakers within " + juce::String (std::abs (gap), 1) + " dB of each other (12 dB in the input)");
        }
    }

    // Separate tracks
    r = Mixer::mixNow (input, Output::separate, Mixer::defaultTarget, {}, 0.0f);
    check (r.ok && r.files.size() == 2, "two separate tracks written");
    for (auto& f : r.files)
    {
        auto out = readWav (f);
        if (out.size() != 1)
            continue;
        const float tp = truePeakDb (out[0]);
        const std::vector<float> late (out[0].begin() + n / 3, out[0].end());
        const double lufs = integratedLoudness (late, sr) + 3.01;   // on a stereo track
        check (tp <= -1.5f && std::abs (lufs + 14.0) < 1.5,
               f.getFileName() + ": " + juce::String (lufs, 1) + " LUFS, true peak " + juce::String (tp, 2) + " dBTP");
    }

    // Replace sound needs a video
    r = Mixer::mixNow (input, Output::replaceSound, Mixer::defaultTarget, {}, 0.0f);
    check (! r.ok && r.error == "novideo", "replace sound refused for an audio file");

    dir.deleteRecursively();
    return failures == 0 ? 0 : 1;
}
}

//==============================================================================
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("Plumppu Radiomix", juce::Colour (0xff202124), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new Console(), true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class RadiomixApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Plumppu Radiomix"; }
    const juce::String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String& commandLine) override
    {
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const auto value = [&args] (const juce::String& name, const juce::String& fallback = {})
        {
            const int i = args.indexOf (name);
            return i >= 0 && i + 1 < args.size() ? args[i + 1].unquoted() : fallback;
        };
        if (args.contains ("--lang"))
            swedishFlag() = value ("--lang") == "sv";

        if (args.contains ("--selfcheck"))
            return finish (selfcheck());

        if (args.contains ("--mix"))
        {
            const juce::File in (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--mix")));
            const auto o = value ("--output", "stereo");
            const auto output = o == "replace" ? Output::replaceSound : o == "separate" ? Output::separate : Output::stereoTrack;
            juce::String last;
            const float target = juce::jlimit (Mixer::quietestTarget, Mixer::loudestTarget,
                                               value ("--target", juce::String (Mixer::defaultTarget)).getFloatValue());
            const auto r = Mixer::mixNow (in, output, target, [&last] (float p, const juce::String& stage)
            {
                const auto line = stage + " " + juce::String (juce::roundToInt (p * 100.0f)) + " %";
                if (line != last) { std::cout << "\r" << line << "        " << std::flush; last = line; }
            });
            std::cout << "\n";
            if (! r.ok)
                std::cerr << text::error (r.error) << "\n";
            for (auto& f : r.files)
                std::cout << f.getFullPathName() << "\n";
            return finish (r.ok ? 0 : 1);
        }

        if (args.contains ("--snapshot"))
        {
            Console console;
            const auto state = value ("--state", "ready");
            if (state != "empty")
                console.loadFile (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--file")));
            if (args.contains ("--loudness"))
                console.setLoudnessPosition (18 + value ("--loudness").getIntValue(), false);
            if (state == "mixing")
                console.showPreviewOfMixing (0.58f, text::cleaning (2, 2));
            const auto img = console.createComponentSnapshot (console.getLocalBounds(), true, 2.0f);
            const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--snapshot")));
            out.deleteFile();
            juce::FileOutputStream st (out);
            juce::PNGImageFormat().writeImageToStream (img, st);
            return finish (0);
        }

        window = std::make_unique<MainWindow>();
    }

    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    void finish (int code)
    {
        setApplicationReturnValue (code);
        quit();
    }

    std::unique_ptr<MainWindow> window;
};
}

START_JUCE_APPLICATION (radiomix::RadiomixApp)
