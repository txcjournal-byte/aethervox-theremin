#pragma once
// =============================================================================
//  ThereminEngine - complete monophonic theremin voice + FX chain
//
//  Signal flow
//  -----------
//   target pitch --> Scale quantizer --> Glide (log domain) --> Vibrato/Humanize
//        |                                                         |
//        v                                                         v
//   target amp --> Antenna envelope (attack/release) --> Tremolo   Heterodyne osc
//                                                         |        (2 RF osc + mixer)
//                                                         |        |
//                                                         v        v
//                                     Waveform morph (sine -> triangle -> saw, BLEP)
//                                                         |
//                Tube saturation (4x OS) -> Formant resonator -> Brightness LPF
//                                                         |
//                          Tape delay (stereo) -> FDN reverb -> Output gain
// =============================================================================
#include "DspUtils.h"
#include "HeterodyneOscillator.h"
#include "PitchQuantizer.h"
#include "TubeSaturator.h"
#include "FormantFilter.h"
#include "TapeDelay.h"
#include "FDNReverb.h"
#include <array>

namespace av
{
struct EngineParams
{
    // pitch
    float glideMs      = 60.0f;
    float quantAmount  = 0.0f;
    int   scale        = 0;
    int   root         = 0;
    // volume / expression
    float attackMs     = 15.0f;
    float releaseMs    = 120.0f;
    // oscillator
    float coupling     = 0.25f;
    float drift        = 0.2f;
    // timbre
    float morph        = 0.0f;
    float drive        = 0.15f;
    float brightness   = 0.75f;
    int   formant      = FormantFilter::VintageTube;
    float formantMix   = 0.5f;
    // modulation
    float vibDepth     = 12.0f;   // cents
    float vibRate      = 5.5f;    // Hz
    float tremolo      = 0.15f;
    float humanize     = 0.3f;
    // fx
    float delayMs      = 380.0f;
    float delayFb      = 0.35f;
    float delayMix     = 0.2f;
    float reverbSize   = 0.7f;
    float reverbMix    = 0.3f;
    float outputDb     = -6.0f;
    // layering
    float layer        = 0.0f;    // 0..1  detuned unison copies
    float sub          = 0.0f;    // 0..1  octave-down layer
};

class ThereminEngine
{
public:
    static constexpr int maxVoices = 8;

    void prepare (double sampleRate);
    void reset();
    void setParameters (const EngineParams& p);

    /** Target pitch (fractional MIDI note) and amplitude (0..1) of one voice. */
    void setVoiceTarget (int voice, float midiNote, float amplitude);
    /** Convenience for mono use: voice 0. */
    void setTarget (float midiNote, float amplitude) { setVoiceTarget (0, midiNote, amplitude); }

    /** MPE "slide"/timbre 0..1, or < 0 for "not used". */
    void setTimbreModulation (float t) { timbreMod = t; }

    /** Voice used for the displays (pitch, hands, tuner). */
    void setLeadVoice (int v) { lead = clampv (v, 0, maxVoices - 1); }

    /** Renders and sums all voices (oscillators + envelopes + modulation), mono. */
    void renderVoice (float* dest, int numSamples);

    /** Tone shaping + FX. mono is processed in place, then written to L/R. */
    void processPost (float* mono, float* left, float* right, int numSamples);

    int   getLatencySamples() const { return saturator.getLatencySamples(); }
    float getCurrentHz()      const { return voices[(size_t) lead].hz; }
    float getCurrentMidi()    const { return voices[(size_t) lead].curMidi; }
    float getEnvelope()       const { return voices[(size_t) lead].env; }
    float getVoiceEnvelope (int v) const { return voices[(size_t) v].env; }

private:
    static float triangle (double p, double dt);
    static float sawtooth (double p, double dt);

    struct Voice
    {
        // osc[0] main, osc[1..2] detuned layers, osc[3] sub octave
        HeterodyneOscillator osc[4];
        DCBlocker dc;
        float targetMidiRaw = 60.0f, targetMidi = 60.0f, targetAmp = 0.0f;
        float curMidi = 60.0f, env = 0.0f, hz = 261.6f;
        double vibPhase = 0.0;
        float vibEnv = 0.0f;
        SmoothNoise nJitter, nWander, nDepth, nDrift, nAmp;
        bool idle() const { return env < 1.0e-5f && targetAmp <= 0.0f; }
    };

    double fs = 48000.0;
    EngineParams params;

    std::array<Voice, maxVoices> voices;
    PitchQuantizer quantizer;
    TubeSaturator saturator;
    FormantFilter formant;
    TapeDelay delay;
    FDNReverb reverb;
    TptSvf toneFilter;

    int lead = 0;
    float timbreMod = -1.0f, polyGain = 1.0f;
    // portamento across voices: a new note glides from the last note that sounded
    float lastSoundMidi = 60.0f; int silentSamples = 1 << 30;
    float glideCoef = 1.0f, attackCoef = 1.0f, releaseCoef = 1.0f, vibOnsetCoef = 0.0f;

    LinearSmoother morph, brightness, outGain, layerAmt, subAmt;
};
} // namespace av
