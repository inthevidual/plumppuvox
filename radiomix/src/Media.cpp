#include "Media.h"
#include "Resampler.h"

namespace radiomix
{
juce::File findTool (const juce::String& name)
{
    const auto exe = name + (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows ? ".exe" : "");

    // Bundled next to the executable (or in the app bundle's Resources)
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { self.getParentDirectory(), self.getParentDirectory().getSiblingFile ("Resources") })
        if (dir.getChildFile (exe).existsAsFile())
            return dir.getChildFile (exe);

    juce::StringArray dirs;
    dirs.addTokens (juce::SystemStats::getEnvironmentVariable ("PATH", {}),
                    juce::File::getSeparatorChar() == '\\' ? ";" : ":", {});
    dirs.addArray ({ "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin" });
    for (auto& d : dirs)
        if (d.isNotEmpty() && juce::File::isAbsolutePath (d) && juce::File (d).getChildFile (exe).existsAsFile())
            return juce::File (d).getChildFile (exe);
    return {};
}

bool isVideoExtension (const juce::File& f)
{
    return f.hasFileExtension ("mp4;mov;mkv;webm;m4v;avi");
}

bool isSupported (const juce::File& f)
{
    return isVideoExtension (f) || f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;aac");
}

namespace
{
bool readableByJuce (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::String run (const juce::StringArray& args, int timeoutMs = 60000)
{
    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdOut))
        return {};
    auto out = p.readAllProcessOutput();
    p.waitForProcessToFinish (timeoutMs);
    return out;
}
}

MediaInfo probe (const juce::File& file)
{
    MediaInfo info;
    if (! file.existsAsFile())
    {
        info.error = "missing";
        return info;
    }

    if (readableByJuce (file))
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (file) })
        {
            info.ok = true;
            info.channels = (int) r->numChannels;
            info.sampleRate = r->sampleRate;
            info.seconds = (double) r->lengthInSamples / r->sampleRate;
            return info;
        }
    }

    const auto ffprobe = findTool ("ffprobe");
    if (! ffprobe.existsAsFile())
    {
        info.error = "noffmpeg";
        return info;
    }
    const auto json = juce::JSON::parse (run ({ ffprobe.getFullPathName(), "-v", "error", "-print_format", "json",
                                                "-show_entries", "format=duration:stream=codec_type,channels,sample_rate:stream_tags=timecode",
                                                file.getFullPathName() }));
    if (! json.isObject())
    {
        info.error = "unreadable";
        return info;
    }
    bool haveAudio = false;
    if (auto* streams = json["streams"].getArray())
        for (auto& st : *streams)
        {
            const auto type = st["codec_type"].toString();
            if (type == "video")
            {
                info.isVideo = true;
                if (auto* tags = st["tags"].getDynamicObject(); tags != nullptr && tags->hasProperty ("timecode"))
                    info.timecode = tags->getProperty ("timecode").toString();
            }
            else if (type == "audio" && ! haveAudio)
            {
                haveAudio = true;
                info.channels = (int) st["channels"];
                info.sampleRate = st["sample_rate"].toString().getDoubleValue();
            }
            if (info.timecode.isEmpty())
                if (auto* tags = st["tags"].getDynamicObject(); tags != nullptr && tags->hasProperty ("timecode"))
                    info.timecode = tags->getProperty ("timecode").toString();
        }
    info.seconds = json["format"]["duration"].toString().getDoubleValue();
    if (! haveAudio)
    {
        info.error = "noaudio";
        return info;
    }
    info.ok = true;
    return info;
}

bool decode (const juce::File& file, std::vector<std::vector<float>>& channels, juce::String& error,
             const std::function<void (float)>& progress, const std::function<bool()>& shouldStop)
{
    juce::File source = file;
    std::unique_ptr<juce::TemporaryFile> temp;

    if (! readableByJuce (file))
    {
        // ffmpeg to a temporary float WAV at 48 kHz
        const auto ffmpeg = findTool ("ffmpeg");
        if (! ffmpeg.existsAsFile())
        {
            error = "noffmpeg";
            return false;
        }
        temp = std::make_unique<juce::TemporaryFile> (".wav");
        juce::ChildProcess p;
        if (! p.start ({ ffmpeg.getFullPathName(), "-v", "error", "-y", "-i", file.getFullPathName(), "-map", "0:a:0",
                         "-ar", "48000", "-c:a", "pcm_f32le", temp->getFile().getFullPathName() }, 0))
        {
            error = "unreadable";
            return false;
        }
        while (p.isRunning())
        {
            if (shouldStop())
            {
                p.kill();
                error = "cancelled";
                return false;
            }
            juce::Thread::sleep (50);
        }
        if (p.getExitCode() != 0)
        {
            error = "unreadable";
            return false;
        }
        source = temp->getFile();
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (source) };
    if (reader == nullptr)
    {
        error = "unreadable";
        return false;
    }

    const int nch = (int) reader->numChannels;
    const auto length = reader->lengthInSamples;
    channels.assign ((size_t) nch, {});
    for (auto& c : channels)
        c.resize ((size_t) length);

    const int block = 1 << 16;
    juce::AudioBuffer<float> buf (nch, block);
    for (juce::int64 pos = 0; pos < length; pos += block)
    {
        if (shouldStop())
        {
            error = "cancelled";
            return false;
        }
        const int n = (int) std::min ((juce::int64) block, length - pos);
        reader->read (&buf, 0, n, pos, true, true);
        for (int c = 0; c < nch; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + n, channels[(size_t) c].begin() + pos);
        progress ((float) (pos + n) / (float) length);
    }

    // The engine runs at 48 kHz
    if (std::abs (reader->sampleRate - 48000.0) > 0.5)
    {
        for (auto& c : channels)
        {
            SincResampler rs;
            rs.prepare (reader->sampleRate, 48000.0);
            SampleFifo out;
            rs.process (c.data(), (int) c.size(), out);
            std::vector<float> tail ((size_t) rs.latency() * 2 + 64, 0.0f);
            rs.process (tail.data(), (int) tail.size(), out);
            std::vector<float> resampled (out.size());
            out.pop (resampled.data(), resampled.size());
            // drop the filter delay, keep the length the new rate implies
            const size_t skip = (size_t) std::lround (rs.latency() * 48000.0 / reader->sampleRate);
            const size_t want = (size_t) std::llround ((double) c.size() * 48000.0 / reader->sampleRate);
            std::vector<float> aligned (want, 0.0f);
            for (size_t i = 0; i < want && i + skip < resampled.size(); ++i)
                aligned[i] = resampled[i + skip];
            c = std::move (aligned);
        }
    }
    return true;
}

bool replaceSound (const juce::File& video, const juce::File& audio, const juce::String& timecode,
                   const juce::File& out, juce::String& error)
{
    const auto ffmpeg = findTool ("ffmpeg");
    if (! ffmpeg.existsAsFile())
    {
        error = "noffmpeg";
        return false;
    }
    const bool webm = video.hasFileExtension ("webm");
    juce::StringArray args { ffmpeg.getFullPathName(), "-v", "error", "-n", "-i", video.getFullPathName(),
                             "-i", audio.getFullPathName(), "-map", "0:v:0", "-map", "1:a:0",
                             "-c:v", "copy", "-c:a", webm ? "libopus" : "aac", "-b:a", webm ? "160k" : "192k",
                             "-map_metadata", "0" };
    if (timecode.isNotEmpty())
        args.addArray ({ "-timecode", timecode });
    if (video.hasFileExtension ("mp4;mov;m4v"))
        args.addArray ({ "-movflags", "+faststart" });
    args.add (out.getFullPathName());

    juce::ChildProcess p;
    if (! p.start (args, juce::ChildProcess::wantStdErr) )
    {
        error = "writefailed";
        return false;
    }
    const auto log = p.readAllProcessOutput();
    p.waitForProcessToFinish (-1);
    if (p.getExitCode() != 0 || ! out.existsAsFile())
    {
        DBG (log);
        error = "writefailed";
        return false;
    }
    return true;
}

juce::File outputFile (const juce::File& input, const juce::String& suffix, const juce::String& extension)
{
    const auto dir = input.getParentDirectory();
    const auto base = input.getFileNameWithoutExtension() + "-radiomix" + suffix;
    auto f = dir.getChildFile (base + "." + extension);
    for (int n = 2; f.exists(); ++n)
        f = dir.getChildFile (base + "-" + juce::String (n) + "." + extension);
    return f;
}
}
