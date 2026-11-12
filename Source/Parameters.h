#pragma once
// =============================================================================
//  Parameter IDs + layout (AudioProcessorValueTreeState)
// =============================================================================
#include <juce_audio_processors/juce_audio_processors.h>

namespace ParamIDs
{
    // PITCH
    inline constexpr auto pitchTune   = "pitchTune";
    inline constexpr auto pitchSens   = "pitchSens";
    inline constexpr auto glide       = "glide";
    inline constexpr auto quantAmount = "quantAmount";
    inline constexpr auto scale       = "scale";
    inline constexpr auto root        = "root";
    // VOLUME & EXPRESSION
    inline constexpr auto volSens     = "volSens";
    inline constexpr auto attack      = "attack";
    inline constexpr auto release     = "release";
    // OSCILLATOR (heterodyne)
    inline constexpr auto coupling    = "coupling";
    inline constexpr auto drift       = "drift";
    // TIMBRE
    inline constexpr auto morph       = "morph";
    inline constexpr auto drive       = "drive";
    inline constexpr auto brightness  = "brightness";
    inline constexpr auto formant     = "formant";
    inline constexpr auto formantMix  = "formantMix";
    // MODULATION
    inline constexpr auto vibDepth    = "vibDepth";
    inline constexpr auto vibRate     = "vibRate";
    inline constexpr auto tremolo     = "tremolo";
    inline constexpr auto humanize    = "humanize";
    // FX
    inline constexpr auto delayTime   = "delayTime";
    inline constexpr auto delayFb     = "delayFeedback";
    inline constexpr auto delayMix    = "delayMix";
    inline constexpr auto revSize     = "reverbSize";
    inline constexpr auto revMix      = "reverbMix";
    inline constexpr auto output      = "output";
    // MIDI
    inline constexpr auto mpe         = "mpe";
    inline constexpr auto pbRange     = "pbRange";
    // VOICES / LAYERING
    inline constexpr auto playMode    = "playMode";
    inline constexpr auto layer       = "layer";
    inline constexpr auto sub         = "sub";
    // HANDS (mouse performance, automatable so the DAW can record it)
    inline constexpr auto handPitch   = "handPitch";
    inline constexpr auto handVol     = "handVol";
}

namespace Params
{
inline juce::StringArray scaleNames()   { return { "Chromatic", "Major", "Minor", "Pentatonic Maj", "Pentatonic Min" }; }
inline juce::StringArray rootNames()    { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
inline juce::StringArray formantNames() { return { "Off", "Ethereal / Sci-Fi", "Soprano Voice", "Vintage Tube" }; }

/** Base note of the pitch field (proximity 0) before tuning: C1 = 32.7 Hz.
    With the default 6-octave sensitivity the field spans C1 .. C7. */
inline constexpr float pitchFieldBaseMidi = 24.0f;

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    using Fmt = std::function<String (float)>;
    auto addFloat = [&layout] (const char* id, const String& name, NormalisableRange<float> range, float def, Fmt fmt)
    {
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id, 1 }, name, range, def,
            AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); })));
    };

    const Fmt pct   = [] (float v) { return String (roundToInt (v * 100.0f)) + " %"; };
    const Fmt ms    = [] (float v) { return v >= 1000.0f ? String (v / 1000.0f, 2) + " s" : String (roundToInt (v)) + " ms"; };
    const Fmt hz    = [] (float v) { return String (v, 1) + " Hz"; };
    const Fmt db    = [] (float v) { return String (v, 1) + " dB"; };
    const Fmt semis = [] (float v) { return (v > 0 ? "+" : "") + String (v, 2) + " st"; };
    const Fmt oct   = [] (float v) { return String (v, 1) + " oct"; };
    const Fmt cents = [] (float v) { return String (roundToInt (v)) + " ct"; };

    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    // PITCH
    addFloat (ParamIDs::pitchTune,   "Pitch Tuning",      { -12.0f, 12.0f, 0.01f }, 0.0f, semis);
    addFloat (ParamIDs::pitchSens,   "Pitch Sensitivity", { 1.0f, 8.0f, 0.01f }, 6.0f, oct);
    addFloat (ParamIDs::glide,       "Glide",             skewed (0.0f, 2000.0f, 150.0f), 85.0f, ms);
    addFloat (ParamIDs::quantAmount, "Pitch Quantize",    { 0.0f, 1.0f }, 0.0f, pct);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::scale, 1 }, "Scale", scaleNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::root, 1 },  "Scale Root", rootNames(), 0));

    // VOLUME & EXPRESSION
    addFloat (ParamIDs::volSens, "Volume Sensitivity", { 0.0f, 1.0f }, 0.5f, pct);
    addFloat (ParamIDs::attack,  "Attack",             skewed (1.0f, 1000.0f, 60.0f), 25.0f, ms);
    addFloat (ParamIDs::release, "Release",            skewed (5.0f, 3000.0f, 250.0f), 180.0f, ms);

    // OSCILLATOR
    addFloat (ParamIDs::coupling, "RF Coupling", { 0.0f, 1.0f }, 0.35f, pct);
    addFloat (ParamIDs::drift,    "RF Drift",    { 0.0f, 1.0f }, 0.2f, pct);

    // TIMBRE
    addFloat (ParamIDs::morph,      "Waveform Morph", { 0.0f, 1.0f }, 0.0f, [] (float v)
    {
        if (v < 0.02f) return String ("Sine");
        if (std::abs (v - 0.5f) < 0.02f) return String ("Triangle");
        if (v > 0.98f) return String ("Saw");
        return v < 0.5f ? "Sin>Tri " + String (roundToInt (v * 200.0f)) + "%"
                        : "Tri>Saw " + String (roundToInt ((v - 0.5f) * 200.0f)) + "%";
    });
    addFloat (ParamIDs::drive,      "Tube Drive",  { 0.0f, 1.0f }, 0.22f, pct);
    addFloat (ParamIDs::brightness, "Brightness",  { 0.0f, 1.0f }, 0.62f, [] (float v)
    {
        const float f = 250.0f * std::pow (72.0f, v);
        return f >= 1000.0f ? String (f / 1000.0f, 1) + " kHz" : String (roundToInt (f)) + " Hz";
    });
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::formant, 1 }, "Formant Resonator", formantNames(), 3));
    addFloat (ParamIDs::formantMix, "Formant Mix", { 0.0f, 1.0f }, 0.3f, pct);

    // MODULATION
    addFloat (ParamIDs::vibDepth, "Vibrato Depth", { 0.0f, 100.0f, 0.1f }, 22.0f, cents);
    addFloat (ParamIDs::vibRate,  "Vibrato Rate",  skewed (0.5f, 12.0f, 5.0f), 5.8f, hz);
    addFloat (ParamIDs::tremolo,  "Tremolo",       { 0.0f, 1.0f }, 0.10f, pct);
    addFloat (ParamIDs::humanize, "Humanize",      { 0.0f, 1.0f }, 0.3f, pct);

    // FX
    addFloat (ParamIDs::delayTime, "Delay Time",     skewed (20.0f, 1500.0f, 350.0f), 380.0f, ms);
    addFloat (ParamIDs::delayFb,   "Delay Feedback", { 0.0f, 0.95f }, 0.28f, pct);
    addFloat (ParamIDs::delayMix,  "Delay Mix",      { 0.0f, 1.0f }, 0.10f, pct);
    addFloat (ParamIDs::revSize,   "Reverb Size",    { 0.0f, 1.0f }, 0.62f, pct);
    addFloat (ParamIDs::revMix,    "Reverb Mix",     { 0.0f, 1.0f }, 0.28f, pct);
    addFloat (ParamIDs::output,    "Master Output",  { -36.0f, 6.0f, 0.1f }, -6.0f, db);

    // MIDI
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::mpe, 1 }, "MPE Mode", false));
    layout.add (std::make_unique<AudioParameterInt>  (ParameterID { ParamIDs::pbRange, 1 }, "Pitch Bend Range", 1, 24, 2,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (v) + " st"; })));

    // VOICES / LAYERING
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::playMode, 1 }, "Play Mode",
                                                        StringArray { "Poly", "Mono" }, 0));
    addFloat (ParamIDs::layer, "Layer", { 0.0f, 1.0f }, 0.0f, pct);
    addFloat (ParamIDs::sub,   "Sub Octave", { 0.0f, 1.0f }, 0.0f, pct);

    // HANDS: the mouse writes these, the host records them as automation and plays them back
    addFloat (ParamIDs::handPitch, "Hand Pitch",  { 0.0f, 1.0f }, 0.5f, pct);
    addFloat (ParamIDs::handVol,   "Hand Volume", { 0.0f, 1.0f }, 0.0f, pct);

    return layout;
}
} // namespace Params
