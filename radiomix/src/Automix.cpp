#include "Automix.h"

#include <algorithm>
#include <cmath>

namespace radiomix
{
namespace
{
// np.percentile with linear interpolation
float percentile (std::vector<float> v, float q)
{
    if (v.empty())
        return 0.0f;
    std::sort (v.begin(), v.end());
    const double pos = (double) q / 100.0 * (double) (v.size() - 1);
    const size_t lo = (size_t) std::floor (pos), hi = std::min (lo + 1, v.size() - 1);
    return (float) (v[lo] + (v[hi] - v[lo]) * (pos - (double) lo));
}

float median (std::vector<float> v)
{
    return percentile (std::move (v), 50.0f);
}

// scipy's "reflect" boundary: ... c b a | a b c ... | ... x y z | z y x ...
long reflect (long i, long n)
{
    while (i < 0 || i >= n)
        i = i < 0 ? -i - 1 : 2 * n - i - 1;
    return i;
}
}

std::vector<float> automix (const std::vector<std::vector<float>>& tracks, double sr,
                            const AutomixSettings& cfg, AutomixReport* report)
{
    const size_t N = tracks.size();
    if (N == 0)
        return {};
    const double hop = 0.01;   // gain frame length (s)
    const size_t hopN = (size_t) (hop * sr);
    size_t n = tracks[0].size();
    for (auto& t : tracks)
        n = std::min (n, t.size());
    const size_t F = n / hopN;
    if (F < 2)
        return std::vector<float> (tracks[0].begin(), tracks[0].begin() + (long) n);

    // Frame RMS per mic, lightly smoothed (3 frames)
    std::vector<std::vector<float>> E (N, std::vector<float> (F));
    for (size_t i = 0; i < N; ++i)
    {
        std::vector<float> raw (F);
        for (size_t f = 0; f < F; ++f)
        {
            double sum = 0.0;
            for (size_t k = f * hopN; k < (f + 1) * hopN; ++k)
                sum += (double) tracks[i][k] * tracks[i][k];
            raw[f] = (float) std::sqrt (sum / (double) hopN + 1e-14);
        }
        for (size_t f = 0; f < F; ++f)
            E[i][f] = (raw[(size_t) reflect ((long) f - 1, (long) F)] + raw[f] + raw[(size_t) reflect ((long) f + 1, (long) F)]) / 3.0f;
    }

    std::vector<float> floors (N);
    for (size_t i = 0; i < N; ++i)
        floors[i] = percentile (E[i], 10.0f);

    // Coupling C[i][j]: how loudly mic i hears speaker j, on frames where mic
    // j is clearly dominant
    std::vector<size_t> dom (F);
    for (size_t f = 0; f < F; ++f)
    {
        size_t best = 0;
        for (size_t i = 1; i < N; ++i)
            if (E[i][f] > E[best][f])
                best = i;
        dom[f] = best;
    }
    std::vector<std::vector<float>> C (N, std::vector<float> (N, 0.0f));
    for (size_t j = 0; j < N; ++j)
    {
        const float p80 = percentile (E[j], 80.0f);
        std::vector<size_t> sel;
        for (size_t f = 0; f < F; ++f)
            if (dom[f] == j && E[j][f] > p80)
                sel.push_back (f);
        if (sel.empty())
            continue;
        for (size_t i = 0; i < N; ++i)
        {
            if (i == j)
                continue;
            std::vector<float> ratio;
            ratio.reserve (sel.size());
            for (auto f : sel)
                ratio.push_back (E[i][f] / E[j][f]);
            C[i][j] = std::clamp (median (std::move (ratio)), 0.0f, 0.7f);
        }
    }

    // Bleed-corrected evidence and power-normalised shares
    std::vector<std::vector<float>> g (N, std::vector<float> (F));
    for (size_t f = 0; f < F; ++f)
    {
        double sumW = 0.0;
        std::vector<double> W (N);
        for (size_t i = 0; i < N; ++i)
        {
            double bleed = 0.0;
            for (size_t j = 0; j < N; ++j)
                bleed += (double) C[i][j] * E[j][f];
            const double V = std::max ((double) E[i][f] - bleed, 0.0) + floors[i];
            W[i] = std::pow (V, (double) cfg.sharpness);
            sumW += W[i];
        }
        for (size_t i = 0; i < N; ++i)
            g[i][f] = (float) std::sqrt (W[i] / std::max (sumW, 1e-30));
    }

    // Lookahead: a maximum over the next frames (scipy maximum_filter1d with
    // size la and origin -(la // 2), as cppod)
    const long la = std::max ((long) (cfg.lookahead / hop), 1L);
    const long centre = la / 2 - la / 2;   // la // 2 + origin
    for (size_t i = 0; i < N; ++i)
    {
        std::vector<float> m (F);
        for (long f = 0; f < (long) F; ++f)
        {
            float best = 0.0f;
            for (long k = 0; k < la; ++k)
                best = std::max (best, g[i][(size_t) reflect (f - centre + k, (long) F)]);
            m[(size_t) f] = best;
        }
        g[i] = std::move (m);
    }

    // Asymmetric one-pole smoothing: fast rise, slow fall
    const float aAtt = (float) std::exp (-hop / std::max ((double) cfg.attack, 1e-4));
    const float aRel = (float) std::exp (-hop / std::max ((double) cfg.release, 1e-4));
    for (size_t i = 0; i < N; ++i)
    {
        float y = g[i][0];
        for (size_t f = 0; f < F; ++f)
        {
            const float x = g[i][f];
            y = x + (y - x) * (x > y ? aAtt : aRel);
            g[i][f] = y;
        }
    }

    // Never more than unit total power open
    for (size_t f = 0; f < F; ++f)
    {
        double tot = 0.0;
        for (size_t i = 0; i < N; ++i)
            tot += (double) g[i][f] * g[i][f];
        const float scale = 1.0f / (float) std::max (std::sqrt (tot), 1.0);
        for (size_t i = 0; i < N; ++i)
            g[i][f] *= scale;
    }

    // Apply, with the gains interpolated per sample between frame centres
    std::vector<float> mix (n, 0.0f);
    for (size_t i = 0; i < N; ++i)
        for (size_t s = 0; s < n; ++s)
        {
            const double pos = ((double) s - (double) hopN * 0.5) / (double) hopN;
            float gi;
            if (pos <= 0.0)
                gi = g[i][0];
            else if (pos >= (double) (F - 1))
                gi = g[i][F - 1];
            else
            {
                const size_t f = (size_t) pos;
                const float t = (float) (pos - (double) f);
                gi = g[i][f] + (g[i][f + 1] - g[i][f]) * t;
            }
            mix[s] += tracks[i][s] * gi;
        }

    float peak = 0.0f;
    for (auto v : mix)
        peak = std::max (peak, std::abs (v));
    float scaleDb = 0.0f;
    if (peak > 0.99f)
    {
        const float k = 0.99f / peak;
        for (auto& v : mix)
            v *= k;
        scaleDb = 20.0f * std::log10 (k);
    }

    if (report != nullptr)
    {
        report->couplingDb.assign (N, std::vector<float> (N, 0.0f));
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j < N; ++j)
                report->couplingDb[i][j] = 20.0f * std::log10 (std::max (C[i][j], 1e-4f));
        report->openPercent.assign (N, 0.0f);
        for (size_t i = 0; i < N; ++i)
        {
            size_t open = 0;
            for (auto v : g[i])
                open += v > 0.5f ? 1 : 0;
            report->openPercent[i] = 100.0f * (float) open / (float) F;
        }
        report->peakScaleDb = scaleDb;
    }
    return mix;
}
}
