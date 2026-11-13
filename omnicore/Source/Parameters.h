#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// =============================================================================
//  OmniCore 3000 - every knob, switch and mode is an automatable APVTS parameter
// =============================================================================
namespace PID
{
    // play
    inline constexpr auto mode      = "mode";
    inline constexpr auto chordType = "chordType";
    inline constexpr auto bass      = "bass";
    inline constexpr auto hold      = "hold";
    inline constexpr auto strumMs   = "strumMs";
    inline constexpr auto pluck     = "pluck";
    inline constexpr auto arpRate   = "arpRate";
    inline constexpr auto arpPat    = "arpPattern";
    inline constexpr auto arpOct    = "arpOctaves";
    inline constexpr auto arpGate   = "arpGate";
    // oscillators
    inline constexpr auto oscA      = "oscAShape";
    inline constexpr auto oscBWave  = "oscBWave";
    inline constexpr auto oscBBits  = "oscBBits";
    inline constexpr auto oscBSemi  = "oscBSemi";
    inline constexpr auto oscMix    = "oscMix";
    inline constexpr auto detune    = "detune";
    inline constexpr auto noise     = "noise";
    inline constexpr auto glide     = "glide";
    // filter + envelopes
    inline constexpr auto cutoff    = "cutoff";
    inline constexpr auto reso      = "resonance";
    inline constexpr auto envAmt    = "envAmt";
    inline constexpr auto keyTrack  = "keyTrack";
    inline constexpr auto drive     = "drive";
    inline constexpr auto attack    = "attack";
    inline constexpr auto decay     = "decay";
    inline constexpr auto sustain   = "sustain";
    inline constexpr auto release   = "release";
    inline constexpr auto fDecay    = "filterDecay";
    // fx
    inline constexpr auto tapeMix   = "tapeMix";
    inline constexpr auto tapeRate  = "tapeRate";
    inline constexpr auto tapeDepth = "tapeDepth";
    inline constexpr auto tapeWow   = "tapeWow";
    inline constexpr auto crushBits = "crushBits";
    inline constexpr auto crushDown = "crushDown";
    inline constexpr auto crushMix  = "crushMix";
    inline constexpr auto dlySync   = "delaySync";
    inline constexpr auto dlyFb     = "delayFeedback";
    inline constexpr auto dlyMix    = "delayMix";
    inline constexpr auto dlyTone   = "delayTone";
    inline constexpr auto spcSize   = "spaceSize";
    inline constexpr auto spcDamp   = "spaceDamp";
    inline constexpr auto spcMix    = "spaceMix";
    inline constexpr auto output    = "output";
    // cosmic
    inline constexpr auto shimmer   = "shimmer";
    inline constexpr auto warpMix   = "warpMix";
    inline constexpr auto warpHz    = "warpHz";
    inline constexpr auto mutAmt    = "mutantAmount";
    inline constexpr auto mutRate   = "mutantRate";
    inline constexpr auto orbitMix  = "orbitMix";
    inline constexpr auto orbitRate = "orbitRate";
}

namespace Params
{
using namespace juce;

inline const StringArray modeNames      { "NOTES", "CHORD", "STRUM", "ARP" };
inline const StringArray chordTypeNames { "MAJ", "MIN", "7", "MAJ7", "MIN7", "SUS4", "ADD9", "DIM" };
inline const StringArray arpRateNames   { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline const StringArray arpPatNames    { "UP", "DOWN", "UP/DN", "RAND" };
inline const StringArray waveNames      { "PURE", "NES TRI", "RAMP", "PULSE 12", "PULSE 25", "ORGAN", "FM BELL", "BUZZ" };
inline const StringArray delaySyncNames { "1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2" };
inline double delayBeats (int i) { static const double b[6] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 }; return b[jlimit (0, 5, i)]; }

inline AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto sec = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                 { return v < 1.0f ? String (roundToInt (v * 1000.0f)) + " ms" : String (v, 2) + " s"; });
    auto hz  = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                 { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; });
    auto f   = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, AudioParameterFloatAttributes a)
                 { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, a)); };
    auto skew = [] (float lo, float hi, float centre) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (centre); return r; };

    // --- play
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::mode, 1 }, "Play Mode", modeNames, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::chordType, 1 }, "Key Chord Type", chordTypeNames, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { PID::bass, 1 }, "Bass Note", true));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID { PID::hold, 1 }, "Chord Hold", false));
    f (PID::strumMs, "Strum Speed", skew (5.0f, 120.0f, 30.0f), 28.0f, AudioParameterFloatAttributes().withLabel ("ms"));
    f (PID::pluck,   "Pluck Length", skew (0.05f, 2.0f, 0.4f), 0.35f, sec);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::arpRate, 1 }, "Arp Rate", arpRateNames, 3));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::arpPat, 1 }, "Arp Pattern", arpPatNames, 2));
    l.add (std::make_unique<AudioParameterInt>    (ParameterID { PID::arpOct, 1 }, "Arp Octaves", 1, 3, 2));
    f (PID::arpGate, "Arp Gate", { 0.05f, 1.0f }, 0.55f, pct);

    // --- oscillators
    f (PID::oscA, "OSC A Shape", { 0.0f, 1.0f }, 0.62f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v < 0.17f ? "SINE" : v < 0.5f ? "TRI" : v < 0.83f ? "SAW" : "SQUARE"; }));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::oscBWave, 1 }, "OSC B 8-bit Wave", waveNames, 3));
    f (PID::oscBBits, "OSC B Bits", { 2.0f, 8.0f, 1.0f }, 6.0f, AudioParameterFloatAttributes().withLabel ("bit"));
    f (PID::oscBSemi, "OSC B Pitch", { -24.0f, 24.0f, 1.0f }, 12.0f, AudioParameterFloatAttributes().withLabel ("st"));
    f (PID::oscMix,   "OSC Mix A/B", { 0.0f, 1.0f }, 0.35f, pct);
    f (PID::detune,   "Analog Drift", { 0.0f, 1.0f }, 0.3f, pct);
    f (PID::noise,    "Noise", { 0.0f, 1.0f }, 0.0f, pct);
    f (PID::glide,    "Glide", skew (0.0f, 1.0f, 0.12f), 0.0f, sec);

    // --- filter + env
    f (PID::cutoff,   "Cutoff", skew (40.0f, 18000.0f, 1200.0f), 2200.0f, hz);
    f (PID::reso,     "Resonance", { 0.0f, 1.0f }, 0.28f, pct);
    f (PID::envAmt,   "Filter Env", { -1.0f, 1.0f }, 0.42f, pct);
    f (PID::keyTrack, "Key Track", { 0.0f, 1.0f }, 0.5f, pct);
    f (PID::drive,    "Warmth", { 0.0f, 1.0f }, 0.3f, pct);
    f (PID::attack,   "Attack",  skew (0.001f, 4.0f, 0.15f), 0.006f, sec);
    f (PID::decay,    "Decay",   skew (0.01f, 4.0f, 0.4f), 0.4f, sec);
    f (PID::sustain,  "Sustain", { 0.0f, 1.0f }, 0.72f, pct);
    f (PID::release,  "Release", skew (0.01f, 6.0f, 0.6f), 0.5f, sec);
    f (PID::fDecay,   "Filter Decay", skew (0.01f, 4.0f, 0.4f), 0.45f, sec);

    // --- fx
    f (PID::tapeMix,   "Tape Mix", { 0.0f, 1.0f }, 0.35f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 0.98f ? String ("VIBRATO") : String (roundToInt (v * 100.0f)) + "%"; }));
    f (PID::tapeRate,  "Tape Rate", { 0.0f, 1.0f }, 0.3f, pct);
    f (PID::tapeDepth, "Tape Depth", { 0.0f, 1.0f }, 0.4f, pct);
    f (PID::tapeWow,   "Wow & Flutter", { 0.0f, 1.0f }, 0.3f, pct);
    f (PID::crushBits, "Crush Bits", { 2.0f, 16.0f, 1.0f }, 10.0f, AudioParameterFloatAttributes().withLabel ("bit"));
    f (PID::crushDown, "Crush Rate", skew (1.0f, 40.0f, 6.0f), 4.0f, AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return String (roundToInt (48000.0f / v)) + " Hz"; }));
    f (PID::crushMix,  "Crush Mix", { 0.0f, 1.0f }, 0.0f, pct);
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { PID::dlySync, 1 }, "Delay Time", delaySyncNames, 2));
    f (PID::dlyFb,     "Delay Feedback", { 0.0f, 0.95f }, 0.38f, pct);
    f (PID::dlyMix,    "Delay Mix", { 0.0f, 1.0f }, 0.22f, pct);
    f (PID::dlyTone,   "Delay Tone", { 0.0f, 1.0f }, 0.5f, pct);
    f (PID::spcSize,   "Space Size", { 0.0f, 1.0f }, 0.72f, pct);
    f (PID::spcDamp,   "Space Damp", { 0.0f, 1.0f }, 0.45f, pct);
    f (PID::spcMix,    "Space Mix", { 0.0f, 1.0f }, 0.28f, pct);
    f (PID::output,    "Output", { -36.0f, 6.0f }, -6.0f, AudioParameterFloatAttributes().withLabel ("dB"));

    // --- cosmic
    f (PID::shimmer,   "Shimmer", { 0.0f, 1.0f }, 0.0f, pct);
    f (PID::warpMix,   "Warp", { 0.0f, 1.0f }, 0.0f, pct);
    f (PID::warpHz,    "Warp Freq", skew (5.0f, 2000.0f, 200.0f), 180.0f, hz);
    f (PID::mutAmt,    "Mutant", { 0.0f, 1.0f }, 0.0f, pct);
    f (PID::mutRate,   "Mutant Rate", { 0.0f, 1.0f }, 0.35f, pct);
    f (PID::orbitMix,  "Orbit", { 0.0f, 1.0f }, 0.0f, pct);
    f (PID::orbitRate, "Orbit Rate", { 0.0f, 1.0f }, 0.25f, pct);
    return l;
}
} // namespace Params
