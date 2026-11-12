// The Radiomix window's one component: the faceplate, and what it does.
#pragma once

#include "Faceplate.h"
#include "Mixer.h"

namespace radiomix
{
class Console final : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    Console();
    ~Console() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    void loadFile (const juce::File&);
    // For screenshots: show a mix in progress without running one
    void showPreviewOfMixing (float progress, const juce::String& stage);
    void setLoudnessPosition (int pos, bool remember = true);

private:
    enum class Phase { empty, ready, mixing, done, failed };

    void timerCallback() override;
    void startMix();
    void cancelMix();
    juce::Image faceplateFor (bool keyDown, int selector, bool replaceAvailable);
    DisplayState displayState() const;
    bool keyIsDown() const;

    Faceplate faceplate;
    std::map<int, juce::Image> cache;
    juce::CriticalSection cacheLock;
    juce::ThreadPool renderers { 2 };

    juce::File file;
    MediaInfo info;
    Phase phase = Phase::empty;
    int selector = 1;
    int loudness = Faceplate::loudnessStops - 1;   // -14 LUFS
    bool pressing = false;           // the key held down by the mouse
    bool draggingSelector = false;
    bool draggingLoudness = false;
    juce::ApplicationProperties settings;
    std::unique_ptr<Mixer> mixer;
    MixResult result;
    juce::String statusText;
    float previewProgress = -1.0f;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Console)
};
}
