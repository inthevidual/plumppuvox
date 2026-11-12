// Reading audio and video files, and writing the mix back into a video.
// Plain audio formats are read with JUCE; video (and M4A/AAC) goes through
// ffmpeg, run as a separate program.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace radiomix
{
struct MediaInfo
{
    bool ok = false;
    juce::String error;
    bool isVideo = false;
    double seconds = 0.0;
    int channels = 0;
    double sampleRate = 0.0;
    juce::String timecode;   // from the video stream, e.g. 01:00:00:00
};

// ffmpeg / ffprobe next to the app, on PATH, or in the usual Homebrew places
juce::File findTool (const juce::String& name);

bool isVideoExtension (const juce::File&);
bool isSupported (const juce::File&);

MediaInfo probe (const juce::File&);

// Decodes the first audio stream to 48 kHz float, one vector per channel.
// progress (0..1) is called now and then; returns false (with error) on failure
// or when shouldStop returns true.
bool decode (const juce::File&, std::vector<std::vector<float>>& channels, juce::String& error,
             const std::function<void (float)>& progress, const std::function<bool()>& shouldStop);

// Writes `out` from `video` with its video stream copied untouched and `audio`
// (a WAV) as the only audio stream; metadata and timecode kept.
bool replaceSound (const juce::File& video, const juce::File& audio, const juce::String& timecode,
                   const juce::File& out, juce::String& error);

// <name>-radiomix<suffix>.<ext> next to the input, never an existing file
juce::File outputFile (const juce::File& input, const juce::String& suffix, const juce::String& extension);
}
