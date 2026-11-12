#include "Console.h"
#include "Texts.h"

namespace radiomix
{
namespace
{
int cacheKey (bool down, int sel, bool replace) { return (down ? 100 : 0) + sel * 10 + (replace ? 1 : 0); }

juce::String duration (double seconds)
{
    const int s = (int) std::lround (seconds);
    return s >= 3600 ? juce::String::formatted ("%d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60)
                     : juce::String::formatted ("%02d:%02d", s / 60, s % 60);
}
}

Console::Console()
{
    setSize (Faceplate::width, Faceplate::height);
    setRepaintsOnMouseActivity (false);

    // The loudness is remembered between launches; -14 LUFS the first time
    juce::PropertiesFile::Options o;
    o.applicationName = "Plumppu Radiomix";
    o.filenameSuffix = ".settings";
    o.folderName = "Plumppu";
    o.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters (o);
    if (auto* p = settings.getUserSettings())
        loudness = juce::jlimit (0, Faceplate::loudnessStops - 1,
                                 18 + juce::roundToInt (p->getDoubleValue ("targetLufs", Mixer::defaultTarget)));

    // Render every state of the moving parts in the background, so pressing
    // the key or moving the selector never waits for the relief
    for (bool down : { false, true })
        for (int sel = 0; sel < 3; ++sel)
            for (bool replace : { true, false })
                renderers.addJob ([this, down, sel, replace]
                {
                    const auto key = cacheKey (down, sel, replace);
                    {
                        const juce::ScopedLock sl (cacheLock);
                        if (cache.count (key) != 0)
                            return;
                    }
                    auto img = faceplate.render (down, sel, replace);
                    const juce::ScopedLock sl (cacheLock);
                    cache.emplace (key, std::move (img));
                });
}

Console::~Console()
{
    renderers.removeAllJobs (true, 20000);
    if (mixer != nullptr)
        mixer->stopThread (20000);
}

juce::Image Console::faceplateFor (bool down, int sel, bool replace)
{
    const auto key = cacheKey (down, sel, replace);
    {
        const juce::ScopedLock sl (cacheLock);
        if (auto it = cache.find (key); it != cache.end())
            return it->second;
    }
    auto img = faceplate.render (down, sel, replace);
    const juce::ScopedLock sl (cacheLock);
    return cache.emplace (key, img).first->second;
}

bool Console::keyIsDown() const
{
    return pressing || phase == Phase::mixing || previewProgress >= 0.0f;
}

void Console::paint (juce::Graphics& g)
{
    const bool replace = phase == Phase::empty || info.isVideo;
    g.drawImage (faceplateFor (keyIsDown(), selector, replace), getLocalBounds().toFloat());
    faceplate.paintLoudnessTab (g, loudness);
    faceplate.paintDisplays (g, displayState());
}

DisplayState Console::displayState() const
{
    DisplayState d;
    if (phase == Phase::empty)
    {
        d.fileName = text::dropFile();
        d.fileIsPlaceholder = true;
        d.info = text::clickToChoose();
        d.status = statusText.isNotEmpty() ? statusText : text::chooseFileFirst();
        return d;
    }

    d.fileName = file.getFileName();
    d.info = (info.isVideo ? text::video() : text::audio()) + juce::String::fromUTF8 (" · ") + duration (info.seconds)
             + juce::String::fromUTF8 (" · ") + juce::String (info.channels) + " " + text::channels()
             + juce::String::fromUTF8 (" · ") + juce::String (juce::roundToInt (info.sampleRate / 1000.0)) + " KHZ";

    if (previewProgress >= 0.0f)
    {
        d.state = text::mixing();
        d.status = statusText + juce::String::fromUTF8 (" · ") + juce::String (juce::roundToInt (previewProgress * 100.0f)) + " %";
        d.statusAction = text::cancel();
        d.progress = previewProgress;
        return d;
    }

    switch (phase)
    {
        case Phase::ready:
            d.state = info.channels < 2 ? text::monoNothingToMix() : text::ready();
            d.status = text::pressMix();
            break;
        case Phase::mixing:
            d.state = text::mixing();
            d.status = mixer->getStage() + juce::String::fromUTF8 (" · ")
                       + juce::String (juce::roundToInt (mixer->getProgress() * 100.0f)) + " %";
            d.statusAction = text::cancel();
            d.progress = mixer->getProgress();
            break;
        case Phase::done:
            d.state = text::done();
            d.status = text::done();
            d.statusAction = text::showInFolder();
            d.progress = 1.0f;
            break;
        case Phase::failed:
            d.state = text::ready();
            d.status = statusText;
            break;
        case Phase::empty:
            break;
    }
    return d;
}

void Console::loadFile (const juce::File& f)
{
    if (phase == Phase::mixing)
        return;
    auto i = probe (f);
    if (! i.ok)
    {
        phase = file.existsAsFile() ? Phase::failed : Phase::empty;
        statusText = text::error (i.error);
        repaint();
        return;
    }
    file = f;
    info = i;
    phase = Phase::ready;
    statusText.clear();
    // a video defaults to replacing its sound; audio cannot
    selector = info.isVideo ? 0 : (selector == 0 ? 1 : selector);
    repaint();
}

void Console::startMix()
{
    if (phase != Phase::ready && phase != Phase::done && phase != Phase::failed)
        return;
    if (! file.existsAsFile())
        return;
    mixer = std::make_unique<Mixer> (file, (Output) selector, info, (float) (loudness - 18));
    mixer->startThread();
    phase = Phase::mixing;
    startTimerHz (20);
    repaint();
}

void Console::cancelMix()
{
    if (mixer != nullptr)
        mixer->signalThreadShouldExit();
}

void Console::timerCallback()
{
    if (mixer == nullptr)
    {
        stopTimer();
        return;
    }
    if (mixer->isFinished())
    {
        stopTimer();
        result = mixer->getResult();
        mixer->stopThread (2000);
        if (result.ok)
            phase = Phase::done;
        else
        {
            phase = Phase::failed;
            statusText = result.cancelled ? text::cancelled() : text::error (result.error);
        }
    }
    repaint();
}

void Console::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    if (Faceplate::keyArea().contains (p))
    {
        if (phase == Phase::ready || phase == Phase::done || phase == Phase::failed)
        {
            pressing = true;
            repaint();
        }
        return;
    }

    if (Faceplate::selectorArea().contains (p) && phase != Phase::mixing)
    {
        draggingSelector = true;
        mouseDrag (e);
        return;
    }

    if (Faceplate::loudnessArea().contains (p) && phase != Phase::mixing)
    {
        draggingLoudness = true;
        mouseDrag (e);
        return;
    }

    if (Faceplate::statusActionArea().contains (p))
    {
        if (phase == Phase::mixing)
            cancelMix();
        else if (phase == Phase::done && ! result.files.isEmpty())
            result.files.getFirst().revealToUser();
        return;
    }

    if (Faceplate::fileDisplayArea().contains (p) && phase != Phase::mixing)
    {
        chooser = std::make_unique<juce::FileChooser> (text::chooserTitle(), juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                       "*.mp4;*.mov;*.mkv;*.webm;*.m4v;*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.aac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                      loadFile (fc.getResult());
                              });
    }
}

void Console::setLoudnessPosition (int pos, bool remember)
{
    pos = juce::jlimit (0, Faceplate::loudnessStops - 1, pos);
    if (pos == loudness)
        return;
    loudness = pos;
    if (auto* p = remember ? settings.getUserSettings() : nullptr)
        p->setValue ("targetLufs", loudness - 18);
    repaint();
}

void Console::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingLoudness)
    {
        setLoudnessPosition (Faceplate::loudnessPositionAt (e.position.x));
        return;
    }
    if (! draggingSelector)
        return;
    int pos = Faceplate::selectorPositionAt (e.position.x);
    if (pos == 0 && phase != Phase::empty && ! info.isVideo)
        pos = 1;   // REPLACE SOUND is blocked without a video
    if (pos != selector)
    {
        selector = pos;
        repaint();
    }
}

void Console::mouseUp (const juce::MouseEvent& e)
{
    draggingSelector = false;
    draggingLoudness = false;
    if (pressing)
    {
        pressing = false;
        if (Faceplate::keyArea().contains (e.position))
            startMix();
        repaint();
    }
}

void Console::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const bool clickable = Faceplate::keyArea().contains (p) || Faceplate::selectorArea().contains (p)
                           || Faceplate::loudnessArea().contains (p)
                           || Faceplate::fileDisplayArea().contains (p)
                           || (Faceplate::statusActionArea().contains (p) && (phase == Phase::mixing || phase == Phase::done));
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

bool Console::isInterestedInFileDrag (const juce::StringArray& files)
{
    return phase != Phase::mixing && files.size() == 1 && isSupported (juce::File (files[0]));
}

void Console::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty())
        loadFile (juce::File (files[0]));
}

void Console::showPreviewOfMixing (float progress, const juce::String& stage)
{
    previewProgress = progress;
    statusText = stage;
    repaint();
}
}
