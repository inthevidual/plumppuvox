#include "Mixer.h"
#include "Automix.h"
#include "Rider.h"
#include "Strings.h"
#include "plumppuvox_engine.h"

#include <deque>

namespace radiomix
{
namespace
{
constexpr double rate = 48000.0;
// Every output is the mix (or a mic) on its own, delivered so that it measures
// the chosen target when it plays from two speakers: a stereo file with the
// same signal on both channels, or a mono track placed on a stereo track.
// Heard from two speakers a signal reads 3 dB louder than as mono, so the
// signal itself is levelled 3 dB under the target.
constexpr double stereoOffsetDb = 3.01;
constexpr float truePeakCeiling = -1.5f;  // dBTP

// The dial-to-dry-mix curve of PlumppuVox Ren (src/PluginProcessor.cpp)
float dryMixFor (float amount)
{
    static constexpr float dry[] = { 1.0f, 0.4167f, 0.2508f, 0.2060f, 0.1780f, 0.1500f,
                                     0.1080f, 0.0660f, 0.0360f, 0.0180f, 0.0f };
    const float pos = juce::jlimit (0.0f, 1.0f, amount) * 10.0f;
    const int i = juce::jmin (9, (int) pos);
    return dry[i] + (dry[i + 1] - dry[i]) * (pos - (float) i);
}

// Channels so alike (a mono recording stored as stereo) that mixing them
// would do nothing
bool channelsAreTheSame (const std::vector<std::vector<float>>& ch)
{
    if (ch.size() < 2)
        return true;
    for (size_t c = 1; c < ch.size(); ++c)
    {
        double xy = 0.0, xx = 0.0, yy = 0.0;
        const auto& a = ch[0];
        const auto& b = ch[c];
        const size_t n = std::min (a.size(), b.size());
        for (size_t i = 0; i < n; i += 4)
        {
            xy += (double) a[i] * b[i];
            xx += (double) a[i] * a[i];
            yy += (double) b[i] * b[i];
        }
        if (xx <= 0.0 || yy <= 0.0 || xy / std::sqrt (xx * yy) < 0.995)
            return false;
    }
    return true;
}

// True peak as a meter estimates it (BS.1770): the largest absolute value of
// the signal oversampled 4x. Here: the samples themselves and three points in
// between each pair, from a Hann-windowed sinc over 32 neighbours.
struct Interpolator
{
    static constexpr int half = 16;
    float taps[3][2 * half];
    Interpolator()
    {
        for (int ph = 0; ph < 3; ++ph)
        {
            const double f = 0.25 * (ph + 1);
            for (int k = -half + 1; k <= half; ++k)
            {
                const double t = f - k;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / (half + 0.5));
                taps[ph][k + half - 1] = (float) (sinc * w);
            }
        }
    }

    // Largest |value| between x[i] and x[i+1], both included
    float peakAfter (const std::vector<float>& x, size_t i) const
    {
        float peak = std::abs (x[i]);
        if (i + 1 < x.size())
            peak = std::max (peak, std::abs (x[i + 1]));
        if (i + 1 < (size_t) half || i + (size_t) half >= x.size())
            return peak;
        for (int ph = 0; ph < 3; ++ph)
        {
            float v = 0.0f;
            const float* tp = taps[ph];
            const float* xp = x.data() + (long) i - half + 1;
            for (int k = 0; k < 2 * half; ++k)
                v += tp[k] * xp[k];
            peak = std::max (peak, std::abs (v));
        }
        return peak;
    }
};

} // namespace

double integratedLoudness (const std::vector<float>& x, double sr)
{
    juce::IIRFilter shelf, highpass;
    const double pi = juce::MathConstants<double>::pi;
    const double A = std::pow (10.0, 4.0 / 40.0), w0 = 2 * pi * 1500.0 / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2 / std::sqrt (2.0)), sa = 2 * std::sqrt (A) * alpha;
    shelf.setCoefficients (juce::IIRCoefficients (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c),
                                                  A * ((A + 1) + (A - 1) * c - sa), (A + 1) - (A - 1) * c + sa,
                                                  2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa));
    const double w1 = 2 * pi * 38.0 / sr, c1 = std::cos (w1), a1 = std::sin (w1);
    highpass.setCoefficients (juce::IIRCoefficients ((1 + c1) / 2, -(1 + c1), (1 + c1) / 2, 1 + a1, -2 * c1, 1 - a1));
    const int step = (int) (sr * 0.1);
    std::vector<double> q;
    double acc = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double y = highpass.processSingleSampleRaw (shelf.processSingleSampleRaw (x[i]));
        acc += y * y;
        if ((int) (i % (size_t) step) == step - 1) { q.push_back (acc / step); acc = 0.0; }
    }
    std::vector<double> blocks;
    for (size_t i = 3; i < q.size(); ++i)
        blocks.push_back ((q[i] + q[i - 1] + q[i - 2] + q[i - 3]) / 4.0);
    const auto lufs = [] (double ms) { return -0.691 + 10.0 * std::log10 (ms + 1e-20); };
    const auto meanAbove = [&] (double gate)
    {
        double sum = 0.0; int n = 0;
        for (double b : blocks) if (lufs (b) > gate) { sum += b; ++n; }
        return n > 0 ? sum / n : 0.0;
    };
    return lufs (meanAbove (lufs (meanAbove (-70.0)) - 10.0));
}

float truePeakDb (const std::vector<float>& x)
{
    static const Interpolator interp;
    float peak = 0.0f;
    for (size_t i = 0; i < x.size(); ++i)
        peak = std::max (peak, interp.peakAfter (x, i));
    return 20.0f * std::log10 (std::max (peak, 1e-9f));
}

namespace
{

// Holds the true peak at or under ceilingDb. The gain needed at each sample
// is spread over +-lookahead by a sliding minimum and then a moving average
// of the same width, which can only lower it further, so the ceiling holds
// while the gain moves smoothly; it recovers with an 80 ms release. A second
// pass catches what the estimate missed.
void limitTruePeak (std::vector<float>& x, float ceilingDb)
{
    static const Interpolator interp;
    const size_t n = x.size();
    if (n == 0)
        return;
    const long L = (long) (0.0015 * rate);                 // 1.5 ms each way
    const float release = (float) std::exp (-1.0 / (0.080 * rate));
    for (int pass = 0; pass < 3; ++pass)
    {
        const float ceiling = std::pow (10.0f, (ceilingDb - 0.05f * (float) pass) / 20.0f);
        std::vector<float> need (n, 1.0f);
        bool any = false;
        for (size_t i = 0; i < n; ++i)
        {
            const float p = interp.peakAfter (x, i);
            if (p > ceiling)
            {
                const float g = ceiling / p;
                need[i] = std::min (need[i], g);
                if (i + 1 < n)
                    need[i + 1] = std::min (need[i + 1], g);
                any = true;
            }
        }
        if (! any)
            return;

        // sliding minimum over [i - L, i + L]
        std::vector<float> lo (n);
        std::deque<size_t> q;
        for (long i = 0; i < (long) n + L; ++i)
        {
            if (i < (long) n)
            {
                while (! q.empty() && need[q.back()] >= need[(size_t) i])
                    q.pop_back();
                q.push_back ((size_t) i);
            }
            const long c = i - L;
            if (c < 0)
                continue;
            while (! q.empty() && (long) q.front() < c - L)
                q.pop_front();
            lo[(size_t) c] = need[q.front()];
        }
        // moving average of the same width (1 outside the track), then the release
        const auto at = [&] (long j) { return j >= 0 && j < (long) n ? (double) lo[(size_t) j] : 1.0; };
        double sum = 0.0;
        for (long j = -L; j <= L; ++j)
            sum += at (j);
        std::vector<float> gain (n);
        for (long i = 0; i < (long) n; ++i)
        {
            gain[(size_t) i] = (float) (sum / (double) (2 * L + 1));
            sum += at (i + L + 1) - at (i - L);
        }
        float y = 1.0f;
        for (size_t i = 0; i < n; ++i)
        {
            y = gain[i] < y ? gain[i] : gain[i] + (y - gain[i]) * release;
            x[i] *= std::min (y, 1.0f);
        }
    }
}

// The delivery: limiting to the true-peak ceiling costs some loudness, so
// the lost level is put back and the result limited again, until it settles
void deliver (std::vector<float>& x, double targetLufs, float ceilingDb)
{
    for (int round = 0; round < 10; ++round)
    {
        const double err = targetLufs - integratedLoudness (x, rate);
        if (round > 0 && std::abs (err) < 0.05)
            break;
        const float g = (float) std::pow (10.0, err / 20.0);
        for (auto& v : x)
            v *= g;
        limitTruePeak (x, ceilingDb);
    }
}

bool writeWav (const juce::File& f, const std::vector<std::vector<float>>& channels)
{
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (f);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                   .withSampleRate (rate)
                                                   .withNumChannels ((int) channels.size())
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return false;
    std::vector<const float*> ptrs;
    for (auto& c : channels)
        ptrs.push_back (c.data());
    return writer->writeFromFloatArrays (ptrs.data(), (int) ptrs.size(), (int) channels[0].size());
}
}

bool cleanTrack (std::vector<float>& track, float amount, const std::function<bool (float)>& progress)
{
    PvEngine* e = plumppuvox_create (1);
    if (e == nullptr)
        return false;
    plumppuvox_set_dry_mix (e, dryMixFor (amount));
    const size_t hop = plumppuvox_hop_size (e), latency = plumppuvox_latency (e);
    const size_t n = track.size();
    std::vector<float> out;
    out.reserve (n + latency + hop);
    std::vector<float> in (hop), frame (hop);
    bool ok = true;
    for (size_t pos = 0; pos < n + latency; pos += hop)
    {
        for (size_t i = 0; i < hop; ++i)
            in[i] = pos + i < n ? track[pos + i] : 0.0f;
        if (plumppuvox_process (e, in.data(), frame.data()) == 0)
        {
            ok = false;
            break;
        }
        out.insert (out.end(), frame.begin(), frame.end());
        if ((pos / hop) % 200 == 0 && ! progress ((float) pos / (float) (n + latency)))
        {
            ok = false;
            break;
        }
    }
    plumppuvox_free (e);
    if (! ok)
        return false;
    std::copy (out.begin() + (long) latency, out.begin() + (long) (latency + n), track.begin());
    return true;
}

void levelTrack (std::vector<float>& track, float monoTargetLufs, const std::function<bool (float)>& progress)
{
    plumppu::Rider rider;
    rider.prepare (rate, 1);
    rider.setTarget (monoTargetLufs);
    rider.setMode (plumppu::Rider::Mode::lagom);
    const size_t latency = (size_t) rider.getLatencySamples(), n = track.size();
    std::vector<float> buf (track);
    buf.resize (n + latency, 0.0f);
    const size_t block = 1 << 15;
    for (size_t pos = 0; pos < buf.size(); pos += block)
    {
        float* p = buf.data() + pos;
        rider.process (&p, 1, (int) std::min (block, buf.size() - pos));
        if (! progress ((float) pos / (float) buf.size()))
            return;
    }
    std::copy (buf.begin() + (long) latency, buf.begin() + (long) (latency + n), track.begin());
}

//==============================================================================
Mixer::Mixer (juce::File in, Output out, MediaInfo i, float target)
    : juce::Thread ("Radiomix"), input (std::move (in)), output (out), targetLufs (target), info (std::move (i))
{
}

Mixer::~Mixer()
{
    stopThread (10000);
}

juce::String Mixer::getStage() const
{
    const juce::ScopedLock sl (lock);
    return stage;
}

MixResult Mixer::getResult() const
{
    const juce::ScopedLock sl (lock);
    return result;
}

void Mixer::setStage (const juce::String& s, float from, float to)
{
    {
        const juce::ScopedLock sl (lock);
        stage = s;
    }
    stageFrom = from;
    stageTo = to;
    progress = from;
    if (listener)
        listener (from, s);
}

void Mixer::setProgress (float within)
{
    progress = stageFrom + (stageTo - stageFrom) * juce::jlimit (0.0f, 1.0f, within);
    if (listener)
        listener (progress.load(), getStage());
}

void Mixer::run()
{
    auto r = process();
    {
        const juce::ScopedLock sl (lock);
        result = r;
    }
    finished = true;
}

MixResult Mixer::mixNow (const juce::File& input, Output output, float targetLufs,
                         std::function<void (float, const juce::String&)> report, float renAmount)
{
    Mixer m (input, output, probe (input), targetLufs);
    m.listener = std::move (report);
    m.renAmount = renAmount;
    return m.process();
}

MixResult Mixer::process()
{
    MixResult r;
    const auto stop = [this] { return threadShouldExit(); };
    const auto cancelled = [&r] { r.cancelled = true; r.error = "cancelled"; return r; };

    if (! info.ok)
    {
        r.error = info.error.isNotEmpty() ? info.error : "unreadable";
        return r;
    }
    if (output == Output::replaceSound && ! info.isVideo)
    {
        r.error = "novideo";
        return r;
    }

    // Read
    setStage (text::reading(), 0.0f, 0.06f);
    std::vector<std::vector<float>> mics;
    juce::String error;
    if (! decode (input, mics, error, [this] (float p) { setProgress (p); }, stop))
    {
        if (error == "cancelled")
            return cancelled();
        r.error = error;
        return r;
    }

    const bool mono = mics.size() < 2 || channelsAreTheSame (mics);
    if (mono)
        mics.resize (1);
    const int count = (int) mics.size();

    // Each mic: Ren, then Lagom
    const float cleanShare = 0.80f;
    for (int m = 0; m < count; ++m)
    {
        const float from = 0.06f + cleanShare * (float) m / (float) count;
        const float to = 0.06f + cleanShare * (float) (m + 1) / (float) count;
        setStage (mono ? text::cleaningMono() : text::cleaning (m + 1, count), from, from + (to - from) * 0.9f);
        if (! cleanTrack (mics[(size_t) m], renAmount, [this] (float p) { setProgress (p); return ! threadShouldExit(); }))
        {
            if (threadShouldExit())
                return cancelled();
            r.error = "engine";
            return r;
        }
        setStage (text::levelling(), from + (to - from) * 0.9f, to);
        levelTrack (mics[(size_t) m], monoTarget(), [this] (float p) { setProgress (p); return ! threadShouldExit(); });
        if (threadShouldExit())
            return cancelled();
    }

    // Separate tracks are written as they are: cleaned and levelled, not
    // mixed; mono, 3 dB under the target, so each measures the target once
    // it is on a stereo track
    if (output == Output::separate)
    {
        setStage (text::writing(), 0.90f, 1.0f);
        for (int m = 0; m < count; ++m)
        {
            deliver (mics[(size_t) m], monoTarget(), truePeakCeiling);
            const auto f = outputFile (input, count > 1 ? "-mic" + juce::String (m + 1) : juce::String(), "wav");
            if (! writeWav (f, { mics[(size_t) m] }))
            {
                r.error = "writefailed";
                return r;
            }
            r.files.add (f);
        }
        setProgress (1.0f);
        r.ok = true;
        return r;
    }

    // Mix, then the final level
    std::vector<float> mix;
    if (count > 1)
    {
        setStage (text::mixingStage(), 0.86f, 0.89f);
        mix = automix (mics, rate);
    }
    else
    {
        mix = std::move (mics[0]);
    }
    mics.clear();
    if (threadShouldExit())
        return cancelled();

    setStage (text::finalLevel(), 0.89f, 0.94f);
    levelTrack (mix, monoTarget(), [this] (float p) { setProgress (p); return ! threadShouldExit(); });
    if (threadShouldExit())
        return cancelled();

    setStage (text::writing(), 0.94f, 1.0f);
    if (output == Output::stereoTrack)
    {
        deliver (mix, monoTarget(), truePeakCeiling);
        const auto f = outputFile (input, {}, "wav");
        if (! writeWav (f, { mix, mix }))
        {
            r.error = "writefailed";
            return r;
        }
        r.files.add (f);
    }
    else
    {
        // The video gets the mix as stereo, the same on both channels (a
        // mono track would play 3 dB louder than it measures). AAC encoding
        // can lift true peaks a little: leave it some room.
        deliver (mix, monoTarget(), truePeakCeiling - 0.7f);
        juce::TemporaryFile temp (".wav");
        if (! writeWav (temp.getFile(), { mix, mix }))
        {
            r.error = "writefailed";
            return r;
        }
        const auto f = outputFile (input, {}, input.getFileExtension().trimCharactersAtStart ("."));
        if (! replaceSound (input, temp.getFile(), info.timecode, f, r.error))
            return r;
        r.files.add (f);
    }
    setProgress (1.0f);
    r.ok = true;
    return r;
}
}
