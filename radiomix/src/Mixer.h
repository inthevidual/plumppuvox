// The mix itself, run off the message thread:
//
//   read (any rate -> 48 kHz) -> per mic: Ren at 80, Lagom (LAGOM)
//   -> automix (cppod's) -> Lagom (LAGOM) on the mix
//   -> true-peak limit at -1.5 dBTP, level made up to the target -> write
//
// The target (-14 LUFS by default) is what every output measures played
// from two speakers; the signal itself sits 3 dB under it.
//
// A mono file, or one whose channels are the same, skips the automix.
#pragma once

#include <juce_core/juce_core.h>
#include "Media.h"

namespace radiomix
{
enum class Output { replaceSound = 0, stereoTrack = 1, separate = 2 };

struct MixResult
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;          // a code for text::error
    juce::Array<juce::File> files;
};

class Mixer final : public juce::Thread
{
public:
    // targetLufs: the loudness every output measures as heard from two
    // speakers (-18..-14); see Mixer.cpp
    Mixer (juce::File input, Output output, MediaInfo info, float targetLufs = defaultTarget);

    static constexpr float defaultTarget = -14.0f, quietestTarget = -18.0f, loudestTarget = -14.0f;
    ~Mixer() override;

    void run() override;

    // For the display, from any thread
    float getProgress() const noexcept { return progress.load(); }
    juce::String getStage() const;
    bool isFinished() const noexcept { return finished.load(); }
    MixResult getResult() const;

    // Runs the whole chain on the calling thread (command line, tests)
    // renAmount is Ren's dial (0..1); the app always uses 0.8, the self-check
    // turns it off because its synthetic voices are noise to the model
    static MixResult mixNow (const juce::File& input, Output, float targetLufs = defaultTarget,
                             std::function<void (float, const juce::String&)> report = {}, float renAmount = 0.8f);

private:
    MixResult process();
    void setStage (const juce::String&, float from, float to);
    void setProgress (float withinStage);

    juce::File input;
    Output output;
    float targetLufs;
    float renAmount = 0.8f;
    float monoTarget() const noexcept { return targetLufs - 3.01f; }
    MediaInfo info;

    std::atomic<float> progress { 0.0f };
    std::atomic<bool> finished { false };
    float stageFrom = 0.0f, stageTo = 0.0f;
    mutable juce::CriticalSection lock;
    juce::String stage;
    MixResult result;
    std::function<void (float, const juce::String&)> listener;
};

// Ren (the PlumppuVox engine) at a given dial setting, on one 48 kHz mono
// track, latency removed. Returns false if the engine fails.
bool cleanTrack (std::vector<float>& track, float amount, const std::function<bool (float)>& progress);

// Lagom on one 48 kHz mono track at the given loudness, LAGOM mode, latency removed.
void levelTrack (std::vector<float>& track, float monoTargetLufs, const std::function<bool (float)>& progress);

// Integrated loudness of a mono track as a meter shows it (BS.1770-4, gated), in LUFS
double integratedLoudness (const std::vector<float>&, double sampleRate);

// The true peak of a 48 kHz track (4x oversampled estimate), in dBTP
float truePeakDb (const std::vector<float>&);
}
