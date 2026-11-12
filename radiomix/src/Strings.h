// Every text in the interface, in English and Swedish. The app follows the
// system language: Swedish when it is "sv", English otherwise.
#pragma once

#include <juce_core/juce_core.h>

namespace radiomix
{
inline bool& swedishFlag()
{
    static bool sv = juce::SystemStats::getUserLanguage().startsWithIgnoreCase ("sv");
    return sv;
}

inline juce::String tr (const char* en, const char* sv)
{
    return juce::String::fromUTF8 (swedishFlag() ? sv : en);
}

namespace text
{
inline juce::String dropFile() { return tr ("DROP A FILE HERE", "SLÄPP EN FIL HÄR"); }
inline juce::String clickToChoose() { return tr ("OR CLICK TO CHOOSE", "ELLER KLICKA FÖR ATT VÄLJA"); }
inline juce::String video() { return tr ("VIDEO", "VIDEO"); }
inline juce::String audio() { return tr ("AUDIO", "LJUD"); }
inline juce::String channels() { return tr ("CH", "KAN"); }
inline juce::String ready() { return tr ("READY", "REDO ATT MIXA"); }
inline juce::String mixing() { return tr ("MIXING", "MIXAR"); }
inline juce::String done() { return tr ("DONE", "FÄRDIG"); }
inline juce::String monoNothingToMix() { return tr ("MONO – NOTHING TO MIX", "MONO – INGET ATT MIXA"); }
inline juce::String fileTypes()
{
    return tr ("MP4, MOV, MKV, WEBM  –  or audio: WAV, AIFF, FLAC, MP3, OGG, M4A",
               "MP4, MOV, MKV, WEBM  –  eller ljud: WAV, AIFF, FLAC, MP3, OGG, M4A");
}

inline juce::StringArray selector (int pos)
{
    switch (pos)
    {
        case 0:  return { tr ("REPLACE", "BYT UT"), tr ("SOUND", "LJUDET"), tr ("IN VIDEO", "I VIDEON") };
        case 1:  return { tr ("SAVE", "SPARA"), tr ("STEREO", "STEREO-"), tr ("TRACK", "SPÅR") };
        default: return { tr ("SAVE TWO", "SPARA TVÅ"), tr ("SEPARATE", "SEPARATA"), tr ("TRACKS", "SPÅR") };
    }
}

// The loudness slider
inline juce::String loudnessTitle() { return tr ("LOUDNESS, LUFS", "LJUDSTYRKA, LUFS"); }
inline juce::String loudnessStop (int pos) { return juce::String::fromUTF8 ("\u2212") + juce::String (18 - pos); }
inline juce::StringArray loudnessNote()
{
    return { tr ("-14 FOR YOUTUBE AND SPOTIFY", "-14 FÖR YOUTUBE OCH SPOTIFY"),
             tr ("SEPARATE TRACKS: -3 (MONO)", "SEPARATA SPÅR: -3 (MONO)") };
}

inline juce::String pressMix() { return tr ("PRESS MIX TO START", "TRYCK PÅ MIX FÖR ATT STARTA"); }
inline juce::String chooseFileFirst() { return tr ("CHOOSE A FILE FIRST", "VÄLJ EN FIL FÖRST"); }
inline juce::String cancel() { return tr ("CANCEL", "AVBRYT"); }
inline juce::String cancelled() { return tr ("CANCELLED", "AVBRUTET"); }
inline juce::String showInFolder() { return tr ("SHOW IN FOLDER", "VISA I MAPPEN"); }

// Stages of a mix
inline juce::String reading() { return tr ("READING", "LÄSER IN"); }
inline juce::String cleaning (int mic, int of)
{
    return tr ("CLEANING MIC", "RENSAR MIK") + " " + juce::String (mic) + " " + tr ("OF", "AV") + " " + juce::String (of);
}
inline juce::String cleaningMono() { return tr ("CLEANING", "RENSAR"); }
inline juce::String levelling() { return tr ("LEVELLING", "NIVÅJUSTERAR"); }
inline juce::String mixingStage() { return tr ("MIXING", "MIXAR"); }
inline juce::String finalLevel() { return tr ("FINAL LEVEL", "SLUTNIVÅ"); }
inline juce::String writing() { return tr ("WRITING", "SKRIVER"); }

inline juce::String chooserTitle() { return tr ("Choose an audio or video file", "Välj en ljud- eller videofil"); }

inline juce::String error (const juce::String& code)
{
    if (code == "noffmpeg")    return tr ("FFMPEG NOT FOUND – VIDEO CAN'T BE READ", "FFMPEG SAKNAS – VIDEO KAN INTE LÄSAS");
    if (code == "noaudio")     return tr ("NO SOUND IN THIS FILE", "INGET LJUD I FILEN");
    if (code == "novideo")     return tr ("NO VIDEO – CAN'T REPLACE ITS SOUND", "INGEN VIDEO – KAN INTE BYTA UT LJUDET");
    if (code == "writefailed") return tr ("COULDN'T WRITE THE RESULT", "KUNDE INTE SKRIVA RESULTATET");
    if (code == "engine")      return tr ("THE CLEANER FAILED", "RENSNINGEN MISSLYCKADES");
    return tr ("CAN'T READ THIS FILE", "KAN INTE LÄSA FILEN");
}
}
}
