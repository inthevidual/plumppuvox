// Speech volume rider: brings speech to a loudness target and evens out its
// level phrase by phrase, while holding still through pauses.
//
// A streaming version of the levelling in cppod's mastering chain
// (cppod/chain.py: speech normalisation, then dsp.auto_level), which needs the
// whole file and an ASR transcript. Here:
//
//   * Speech is found by level: a 20 ms envelope more than 10 dB above the
//     10th percentile of the last 20 s (cppod's active-speech rule), and at
//     least -55 dBFS, padded by 150 ms on both sides like cppod's word mask.
//   * The long-term speech loudness (K-weighted, so in LUFS) is a running
//     mean over speech that forgets over about 30 s of talking. The gain
//     brings it to the target.
//   * On top of that, part of the difference between the long-term loudness
//     and the speech loudness over a 3 s window around the current moment,
//     limited to +-12 dB. How large a part (the strength) is chosen to meet
//     a loudness range (LRA) target: Lite 7 LU, Lagom 5 LU, Max 3 LU. Every
//     100 ms the 3 s loudness of the input is added to a histogram that
//     forgets over about 2 minutes; from its 10th and 95th percentiles and
//     the long-term loudness the LRA after riding is predicted for each
//     strength, and the lowest strength that meets the target is used. An
//     even recording is therefore left alone.
//   * In pauses the gain holds. It is smoothed with a 0.3 s one-pole.
//   * Raising quiet speech can push its transients past full scale, so a
//     safety limiter (5 ms lookahead, 1 ms attack, 80 ms release) keeps
//     sample peaks at -1 dBFS. It only acts on peaks.
//
// cppod centres its window and smooths forwards and backwards; a plugin
// cannot see the future, so the audio is delayed by 2 s and the gain is
// worked out from what lies ahead. Hosts compensate the delay.
#pragma once

#include <array>
#include <vector>

namespace plumppu
{
class Rider
{
public:
    static constexpr float minTarget = -30.0f, maxTarget = -10.0f, defaultTarget = -18.0f;

    void prepare (double sampleRate, int numChannels);
    void reset();

    void setTarget (float lufs) noexcept { target = lufs; }

    // The loudness range to aim for, in LU (see the top of this file).
    enum class Mode { lite, lagom, max };
    void setMode (Mode m) noexcept { mode = m; }
    static constexpr float lraTarget (Mode m) noexcept { return m == Mode::lite ? 7.0f : m == Mode::lagom ? 5.0f : 3.0f; }

    // Bypassed, the audio keeps its delay and the gain glides to 0 dB, while
    // the measurements keep running so switching back is seamless.
    void setBypassed (bool shouldBypass) noexcept { bypassed = shouldBypass; }

    // Processes in place. channels holds numChannels pointers.
    void process (float* const* channels, int numChannels, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return (lookaheadFrames + 1) * hop; }

    // For the meters (written on the audio thread; read with relaxed loads).
    float getGainDb() const noexcept { return currentGainDb; }
    float getRideDb() const noexcept { return currentRideDb; }
    float getLimitDb() const noexcept { return currentLimitDb; }   // peak limiting, <= 0
    float getStrength() const noexcept { return strength; }        // as chosen, 0..1
    bool isSpeech() const noexcept { return currentSpeech; }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void frameUpdate() noexcept;
    float floorPercentile() const noexcept;

    // Timing, in frames of `hop` samples (10 ms)
    static constexpr int lookaheadFrames = 200;   // 2 s
    static constexpr int windowFrames = 300;      // 3 s
    static constexpr int padFrames = 15;          // 150 ms
    static constexpr int floorFrames = 2000;      // 20 s
    static constexpr int ring = 1024;             // > window + pad, power of two
    static constexpr float longTermSeconds = 30.0f;
    static constexpr float smoothSeconds = 0.3f;
    static constexpr float rideLimitDb = 12.0f;
    // The window ends this many frames before the newest decided frame, so
    // that it is centred on the frame being played
    static constexpr int windowLead = lookaheadFrames - padFrames - windowFrames / 2;
    static_assert (windowLead >= 0, "window must not reach past the lookahead");
    static constexpr int shortTermFrames = 300;   // 3 s, as EBU R128 short-term loudness
    static constexpr float histogramSeconds = 120.0f;
    // Riding loses part of its correction to the smoothing; the measured LRA
    // ended up about 1.6 * strength^2 LU above the plain prediction.
    static constexpr float rideLossLu = 1.6f;
    static constexpr float maxBoostDb = 24.0f, maxCutDb = 30.0f;

    // Envelope histogram for the noise floor: 0.25 dB bins from -150 dBFS
    static constexpr int bins = 640;
    static constexpr float binLowDb = -150.0f, binWidthDb = 0.25f;

    double sampleRate = 48000.0;
    int hop = 480, channels = 0;
    float target = defaultTarget;
    bool bypassed = false;
    Mode mode = Mode::lagom;
    float strength = 0.75f;

    // Per-channel K-weighting and the delay line
    std::vector<std::array<Biquad, 2>> kFilters;
    std::vector<std::vector<float>> delay;
    int delayLength = 0, delayPos = 0;

    // Current frame accumulators
    int inFrame = 0;
    double sumK = 0.0, sumRaw = 0.0;
    double envelope = 0.0;
    long long framesSeen = 0;

    // Frame-rate history
    std::array<double, ring> kEnergy {};
    std::array<bool, ring> loud {};
    std::array<bool, ring> speech {};
    std::vector<int> floorBins;   // ring of floorFrames histogram bin indices
    std::array<int, bins> histogram {};
    int floorCount = 0, floorPos = 0;

    // Short-term loudness histogram (0.1 LU bins from -70 LUFS) for the LRA
    static constexpr int stBins = 800;
    std::array<double, stBins> stHistogram {};
    double stWeight = 0.0, stEnergy = 0.0, allEnergy3s = 0.0;
    void chooseStrength() noexcept;

    // Long-term speech loudness and the window sums
    double longTerm = 0.0;
    long long speechFrames = 0;
    double windowEnergy = 0.0;
    int windowSpeech = 0;

    // Safety limiter: input peaks (max over channels) in step with the delay
    // line, and a sliding maximum over the next `limitLook` samples to leave
    std::vector<float> peaks;
    std::vector<long long> windowIndex;
    std::vector<float> windowPeak;
    int windowHead = 0, windowSize = 0, limitLook = 240;
    long long sampleIndex = 0;
    float limitGain = 1.0f, limitAttack = 0.0f, limitRelease = 0.0f;
    static constexpr float ceiling = 0.89125094f;   // -1 dBFS

    // Gain
    float longTermDb = 0.0f, rideHeldDb = 0.0f;   // as last measured over speech
    float smoothDb = 0.0f, smoothCoeff = 0.0f;
    float rampFrom = 1.0f, rampTo = 1.0f;
    float currentGainDb = 0.0f, currentRideDb = 0.0f, currentLimitDb = 0.0f;
    bool currentSpeech = false;
};
}
