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
};

class ThereminEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParameters (const EngineParams& p);

    /** Target pitch (fractional MIDI note) and amplitude (0..1). */
    void setTarget (float midiNote, float amplitude);

    /** MPE "slide"/timbre 0..1, or < 0 for "not used". */
    void setTimbreModulation (float t) { timbreMod = t; }

    /** Renders the raw voice (oscillator + envelope + modulation), mono. */
    void renderVoice (float* dest, int numSamples);

    /** Tone shaping + FX. mono is processed in place, then written to L/R. */
    void processPost (float* mono, float* left, float* right, int numSamples);

    int   getLatencySamples() const { return saturator.getLatencySamples(); }
    float getCurrentHz()      const { return currentHz; }
    float getCurrentMidi()    const { return curMidi; }
    float getEnvelope()       const { return env; }

private:
    static float triangle (double p, double dt);
    static float sawtooth (double p, double dt);

    double fs = 48000.0;
    EngineParams params;

    HeterodyneOscillator osc;
    PitchQuantizer quantizer;
    TubeSaturator saturator;
    FormantFilter formant;
    TapeDelay delay;
    FDNReverb reverb;
    TptSvf toneFilter;
    DCBlocker voiceDc;

    // control state
    float targetMidiRaw = 60.0f, targetMidi = 60.0f, targetAmp = 0.0f;
    float curMidi = 60.0f, env = 0.0f, currentHz = 261.6f, timbreMod = -1.0f;
    float glideCoef = 1.0f, attackCoef = 1.0f, releaseCoef = 1.0f, vibOnsetCoef = 0.0f;

    // humanize
    double vibPhase = 0.0;
    float  vibEnv = 0.0f;
    SmoothNoise nJitter { 11 }, nWander { 23 }, nDepth { 37 }, nDrift { 41 }, nAmp { 53 };

    LinearSmoother morph, brightness, outGain;
};
} // namespace av
