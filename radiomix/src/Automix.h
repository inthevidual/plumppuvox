// Crosstalk-aware automix of per-speaker mic tracks, ported from cppod's
// automix.py (Dugan-style gain sharing on bleed-corrected speech evidence):
//
//  - the coupling matrix (how loudly mic i hears speaker j) is measured from
//    the material: on frames where mic j clearly dominates, the median ratio
//    of mic i's level to mic j's, clipped to 0..0.7
//  - each mic's evidence is its level minus the bleed the others predict in
//    it, plus its own floor, so bleed alone cannot hold a mic open and room
//    tone stays constant in the pauses
//  - gains are power-normalised evidence shares (exponent `sharpness`): a solo
//    speaker gets ~0 dB, two at once ~-3 dB each
//  - a short lookahead opens a mic before the first syllable; a fast attack
//    and slow release keep word tails
//
// The tracks should already be denoised and levelled to a common loudness.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace radiomix
{
struct AutomixSettings
{
    float sharpness = 2.0f;   // evidence exponent; higher ducks harder
    float lookahead = 0.03f;  // s
    float attack = 0.012f;    // s, gain rise time constant
    float release = 0.35f;    // s, gain fall time constant
};

struct AutomixReport
{
    std::vector<std::vector<float>> couplingDb;   // [i][j]: how loudly mic i hears speaker j
    std::vector<float> openPercent;               // share of the time each mic is above -6 dB
    float peakScaleDb = 0.0f;                     // peak-safe scaling applied (<= 0)
};

// Mixes equal-length mono tracks into one; returns the mix.
std::vector<float> automix (const std::vector<std::vector<float>>& tracks, double sampleRate,
                            const AutomixSettings& = {}, AutomixReport* report = nullptr);
}
