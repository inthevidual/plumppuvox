#include "Rider.h"

#include <algorithm>
#include <cmath>

namespace plumppu
{
namespace
{
constexpr double pi = 3.14159265358979323846;

// The BS.1770 pre-filter (high shelf) and RLB high-pass, designed for any
// sample rate with the same formulas pyloudnorm uses.
void designKWeighting (double sr, double& b0, double& b1, double& b2, double& a1, double& a2, bool shelf)
{
    const double fc = shelf ? 1500.0 : 38.0;
    const double q = shelf ? 1.0 / std::sqrt (2.0) : 0.5;
    const double w0 = 2.0 * pi * fc / sr;
    const double c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    double a0;

    if (shelf)
    {
        const double A = std::pow (10.0, 4.0 / 40.0), sa = 2.0 * std::sqrt (A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * c + sa);
        b1 = -2 * A * ((A - 1) + (A + 1) * c);
        b2 = A * ((A + 1) + (A - 1) * c - sa);
        a0 = (A + 1) - (A - 1) * c + sa;
        a1 = 2 * ((A - 1) - (A + 1) * c);
        a2 = (A + 1) - (A - 1) * c - sa;
    }
    else
    {
        b0 = (1 + c) / 2;
        b1 = -(1 + c);
        b2 = (1 + c) / 2;
        a0 = 1 + alpha;
        a1 = -2 * c;
        a2 = 1 - alpha;
    }

    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
}

float toLufs (double meanSquare) noexcept
{
    return (float) (-0.691 + 10.0 * std::log10 (meanSquare + 1e-20));
}
}

void Rider::prepare (double sr, int numChannels)
{
    sampleRate = sr;
    channels = std::max (1, numChannels);
    hop = std::max (1, (int) std::lround (sr / 100.0));

    kFilters.assign ((size_t) channels, {});
    for (auto& pair : kFilters)
        for (int s = 0; s < 2; ++s)
        {
            auto& f = pair[(size_t) s];
            designKWeighting (sr, f.b0, f.b1, f.b2, f.a1, f.a2, s == 0);
        }

    delayLength = getLatencySamples();
    delay.assign ((size_t) channels, std::vector<float> ((size_t) delayLength, 0.0f));
    floorBins.assign ((size_t) floorFrames, 0);
    peaks.assign ((size_t) delayLength, 0.0f);
    limitLook = std::max (1, (int) std::lround (0.005 * sr));
    windowIndex.assign ((size_t) limitLook + 2, 0);
    windowPeak.assign ((size_t) limitLook + 2, 0.0f);
    limitAttack = (float) std::exp (-1.0 / (0.001 * sr));
    limitRelease = (float) std::exp (-1.0 / (0.080 * sr));
    smoothCoeff = (float) std::exp (-1.0 / (smoothSeconds * 100.0));
    reset();
}

void Rider::reset()
{
    for (auto& pair : kFilters)
        for (auto& f : pair)
            f.z1 = f.z2 = 0.0;

    for (auto& line : delay)
        std::fill (line.begin(), line.end(), 0.0f);

    std::fill (peaks.begin(), peaks.end(), 0.0f);
    windowHead = windowSize = 0;
    sampleIndex = 0;
    limitGain = 1.0f;
    currentLimitDb = 0.0f;
    delayPos = inFrame = 0;
    sumK = sumRaw = envelope = 0.0;
    framesSeen = 0;
    kEnergy.fill (0.0);
    loud.fill (false);
    speech.fill (false);
    histogram.fill (0);
    floorCount = floorPos = 0;
    longTerm = 0.0;
    speechFrames = 0;
    windowEnergy = 0.0;
    windowSpeech = 0;
    longTermDb = rideHeldDb = 0.0f;
    stHistogram.fill (0.0);
    stWeight = stEnergy = allEnergy3s = 0.0;
    strength = mode == Mode::lite ? 0.5f : mode == Mode::lagom ? 0.75f : 1.0f;
    smoothDb = 0.0f;
    rampFrom = rampTo = 1.0f;
    currentGainDb = currentRideDb = 0.0f;
    currentSpeech = false;
}

float Rider::floorPercentile() const noexcept
{
    const int rank = (int) (0.1f * (float) (floorCount - 1));
    int seen = 0;

    for (int b = 0; b < bins; ++b)
    {
        seen += histogram[(size_t) b];
        if (seen > rank)
            return binLowDb + ((float) b + 0.5f) * binWidthDb;
    }

    return binLowDb;
}

void Rider::chooseStrength() noexcept
{
    // Needs about 10 s of loudness history and a long-term speech level
    if (stWeight < 50.0 || speechFrames == 0)
        return;

    // EBU R128 loudness range: relative gate 20 LU below the mean, then the
    // spread from the 10th to the 95th percentile
    const float gate = toLufs (stEnergy / stWeight) - 20.0f;
    const int first = std::clamp ((int) std::ceil ((gate + 70.0f) * 10.0f), 0, stBins);
    double total = 0.0;
    for (int b = first; b < stBins; ++b)
        total += stHistogram[(size_t) b];
    if (total <= 0.0)
        return;

    float p10 = 0.0f, p95 = 0.0f;
    bool have10 = false;
    double seen = 0.0;
    for (int b = first; b < stBins; ++b)
    {
        seen += stHistogram[(size_t) b];
        const float value = -70.0f + ((float) b + 0.5f) * 0.1f;
        if (! have10 && seen >= 0.10 * total) { p10 = value; have10 = true; }
        if (seen >= 0.95 * total) { p95 = value; break; }
    }

    // Riding maps a level v to v - k * clamp (v - L), which keeps the order,
    // so the percentiles move with it
    const auto ride = [this] (float v) { return std::clamp (v - longTermDb, -rideLimitDb, rideLimitDb); };
    const float wanted = lraTarget (mode);
    float chosen = 1.0f;
    for (int step = 0; step <= 20; ++step)
    {
        const float k = 0.05f * (float) step;
        const float predicted = (p95 - p10) - k * (ride (p95) - ride (p10)) + rideLossLu * k * k;
        if (predicted <= wanted) { chosen = k; break; }
    }
    strength = chosen;
}

void Rider::frameUpdate() noexcept
{
    constexpr int mask = ring - 1;
    const long long n = framesSeen++;

    kEnergy[(size_t) (n & mask)] = sumK / hop;

    // 3 s loudness of everything (not only speech), as a loudness range meter
    allEnergy3s += kEnergy[(size_t) (n & mask)];
    if (n >= shortTermFrames)
        allEnergy3s -= kEnergy[(size_t) ((n - shortTermFrames) & mask)];

    if (n >= shortTermFrames - 1 && n % 10 == 0)
    {
        const double decay = std::exp (-0.1 / histogramSeconds);
        for (auto& b : stHistogram)
            b *= decay;
        stWeight *= decay;
        stEnergy *= decay;

        const double ms = std::max (allEnergy3s, 0.0) / shortTermFrames;
        const float st = toLufs (ms);
        if (st > -70.0f)
        {
            stHistogram[(size_t) std::clamp ((int) ((st + 70.0f) * 10.0f), 0, stBins - 1)] += 1.0;
            stWeight += 1.0;
            stEnergy += ms;
        }

        chooseStrength();
    }

    // 20 ms envelope of the plain signal, in dB, against the noise floor
    const double raw = sumRaw / ((double) hop * channels);
    envelope = raw + (envelope - raw) * std::exp (-0.5);
    const float envDb = (float) (10.0 * std::log10 (envelope + 1e-12));
    const int bin = std::clamp ((int) ((envDb - binLowDb) / binWidthDb), 0, bins - 1);

    if (floorCount == floorFrames)
        --histogram[(size_t) floorBins[(size_t) floorPos]];
    else
        ++floorCount;

    floorBins[(size_t) floorPos] = bin;
    ++histogram[(size_t) bin];
    floorPos = (floorPos + 1) % floorFrames;

    loud[(size_t) (n & mask)] = envDb > std::max (floorPercentile() + 10.0f, -55.0f);

    // Frame d is decided once the padding after it has arrived
    const long long d = n - padFrames;

    if (d >= 0)
    {
        bool isSpeech = false;
        for (long long i = std::max (0LL, d - padFrames); i <= n && ! isSpeech; ++i)
            isSpeech = loud[(size_t) (i & mask)];

        speech[(size_t) (d & mask)] = isSpeech;

        if (isSpeech)
        {
            ++speechFrames;
            const double alpha = std::max (1.0 / (double) speechFrames, 1.0 / (longTermSeconds * 100.0));
            longTerm += alpha * (kEnergy[(size_t) (d & mask)] - longTerm);
        }

        // The ride window: speech frames in the windowFrames ending at e
        const long long e = d - windowLead;
        if (e >= 0 && speech[(size_t) (e & mask)])
        {
            windowEnergy += kEnergy[(size_t) (e & mask)];
            ++windowSpeech;
        }

        const long long old = e - windowFrames;
        if (old >= 0 && speech[(size_t) (old & mask)])
        {
            windowEnergy -= kEnergy[(size_t) (old & mask)];
            --windowSpeech;
        }

        if (windowSpeech == 0)
            windowEnergy = 0.0;

        // Re-measured only while there is speech to measure; held otherwise
        if (windowSpeech >= 10 && speechFrames > 0)
        {
            longTermDb = toLufs (longTerm);
            const float shortTermDb = toLufs (windowEnergy / windowSpeech);
            rideHeldDb = strength * std::clamp (longTermDb - shortTermDb, -rideLimitDb, rideLimitDb);
        }
    }

    // The target applies at once, even in a pause
    const float heldDb = speechFrames > 0 && ! bypassed
        ? std::clamp (target - longTermDb, -maxCutDb, maxBoostDb) + rideHeldDb
        : 0.0f;
    smoothDb = heldDb + (smoothDb - heldDb) * smoothCoeff;

    rampFrom = rampTo;
    rampTo = std::pow (10.0f, smoothDb / 20.0f);
    currentGainDb = smoothDb;
    currentRideDb = rideHeldDb;
    const long long t = n - lookaheadFrames;
    currentSpeech = t >= 0 && speech[(size_t) (t & mask)];
}

void Rider::process (float* const* data, int numChannels, int numSamples) noexcept
{
    const int chans = std::min (numChannels, channels);
    const int cap = (int) windowPeak.size();
    float deepest = 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = rampFrom + (rampTo - rampFrom) * (float) (inFrame + 1) / (float) hop;

        // The sample leaving now entered delayLength samples ago; keep the
        // largest input peak among it and the limitLook samples behind it.
        const long long leaving = sampleIndex - delayLength;
        const long long entering = leaving + limitLook;

        if (entering >= 0)
        {
            const float v = peaks[(size_t) (entering % delayLength)];
            while (windowSize > 0 && windowPeak[(size_t) ((windowHead + windowSize - 1) % cap)] <= v)
                --windowSize;
            const int slot = (windowHead + windowSize) % cap;
            windowIndex[(size_t) slot] = entering;
            windowPeak[(size_t) slot] = v;
            ++windowSize;
        }

        while (windowSize > 0 && windowIndex[(size_t) windowHead] < leaving)
        {
            windowHead = (windowHead + 1) % cap;
            --windowSize;
        }

        const float ahead = windowSize > 0 ? windowPeak[(size_t) windowHead] * g : 0.0f;
        const float need = ! bypassed && ahead > ceiling ? ceiling / ahead : 1.0f;
        limitGain = need + (limitGain - need) * (need < limitGain ? limitAttack : limitRelease);
        deepest = std::min (deepest, limitGain);
        const float total = g * limitGain;

        float peakIn = 0.0f;

        for (int c = 0; c < chans; ++c)
        {
            const float x = data[c][i];
            auto& k = kFilters[(size_t) c];
            const double y = k[1].run (k[0].run (x));
            sumK += y * y;
            sumRaw += (double) x * x;
            peakIn = std::max (peakIn, std::abs (x));

            float& stored = delay[(size_t) c][(size_t) delayPos];
            data[c][i] = bypassed ? stored * total : std::clamp (stored * total, -ceiling, ceiling);
            stored = x;
        }

        peaks[(size_t) delayPos] = peakIn;
        delayPos = delayPos + 1 == delayLength ? 0 : delayPos + 1;
        ++sampleIndex;

        if (++inFrame == hop)
        {
            frameUpdate();
            inFrame = 0;
            sumK = sumRaw = 0.0;
        }
    }

    currentLimitDb = 20.0f * std::log10 (deepest);
}
}
