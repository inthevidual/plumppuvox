#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Minimal single-channel FIFO. Capacity is reserved up front so that pushing
// and popping on the audio thread does not allocate in normal operation.
class SampleFifo
{
public:
    void reset (size_t capacity)
    {
        data.clear();
        data.reserve (capacity);
    }

    void push (const float* samples, size_t count) { data.insert (data.end(), samples, samples + count); }
    void push (float sample) { data.push_back (sample); }
    void pushZeros (size_t count) { data.insert (data.end(), count, 0.0f); }

    // Pops up to `count` samples; anything missing is filled with silence.
    void pop (float* dest, size_t count)
    {
        const size_t n = std::min (count, data.size());
        std::copy (data.begin(), data.begin() + (std::ptrdiff_t) n, dest);
        std::fill (dest + n, dest + count, 0.0f);
        data.erase (data.begin(), data.begin() + (std::ptrdiff_t) n);
    }

    size_t size() const { return data.size(); }

private:
    std::vector<float> data;
};

// Streaming windowed-sinc sample rate converter. Input driven: feed any number
// of samples and it appends whatever output has become available to a FIFO.
// The delay is exactly `latency()` input samples.
class SincResampler
{
public:
    void prepare (double inputRate, double outputRate)
    {
        step = inputRate / outputRate;
        // Low-pass at the lower of the two Nyquist frequencies.
        const double cutoff = std::min (1.0, outputRate / inputRate) * 0.96;
        halfWidth = (int) std::ceil (zeroCrossings / cutoff);

        const int tableSize = 2 * halfWidth * tableResolution + 2;
        table.assign ((size_t) tableSize, 0.0f);
        const double pi = 3.14159265358979323846;

        for (int i = 0; i < tableSize; ++i)
        {
            const double t = (double) i / tableResolution - halfWidth; // input samples from centre
            const double x = t / halfWidth;

            if (std::abs (x) >= 1.0)
                continue;

            const double sincArg = pi * cutoff * t;
            const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (sincArg) / sincArg;
            // Blackman-Harris window
            const double w = 0.35875 + 0.48829 * std::cos (pi * x)
                           + 0.14128 * std::cos (2.0 * pi * x) + 0.01168 * std::cos (3.0 * pi * x);
            table[(size_t) i] = (float) (cutoff * sinc * w);
        }

        history.clear();
        history.reserve (65536);
        reset();
    }

    void reset()
    {
        history.assign ((size_t) (2 * halfWidth), 0.0f);
        position = (double) halfWidth;
    }

    int latency() const { return halfWidth; }

    void process (const float* input, int count, SampleFifo& output)
    {
        history.insert (history.end(), input, input + count);
        const int available = (int) history.size();

        while ((int) position + halfWidth < available)
        {
            const int centre = (int) position;
            const double frac = position - centre;
            float sum = 0.0f;

            // Taps centre-halfWidth+1 .. centre+halfWidth
            for (int k = -halfWidth + 1; k <= halfWidth; ++k)
            {
                // Distance from the output instant to tap k, shifted into table space.
                const double tablePos = (frac - k + halfWidth) * tableResolution;
                const int idx = (int) tablePos;
                const float a = (float) (tablePos - idx);
                const float h = table[(size_t) idx] + a * (table[(size_t) idx + 1] - table[(size_t) idx]);
                sum += history[(size_t) (centre + k)] * h;
            }

            output.push (sum);
            position += step;
        }

        const int drop = std::max (0, (int) position - halfWidth);
        history.erase (history.begin(), history.begin() + drop);
        position -= drop;
    }

private:
    static constexpr int zeroCrossings = 16;
    static constexpr int tableResolution = 256;

    double step = 1.0;
    double position = 0.0;
    int halfWidth = zeroCrossings;
    std::vector<float> table;
    std::vector<float> history;
};
