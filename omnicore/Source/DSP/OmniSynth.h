#pragma once
// =============================================================================
//  OmniCore 3000 - voice engine + "brutal" FX rack (pure C++17)
//
//  Voice:  OSC A  analog morph (sine > triangle > saw > square, polyBLEP)
//          OSC B  8-bit wavetable (8 shapes, bit-depth + step crunch), +-24 st
//          noise, 4-pole warm low-pass (2 x SVF, saturated) with envelope + key track
//          amp ADSR, filter ADSR, velocity
//  FX:     tape chorus / vibrato (wow + flutter) > bitcrusher / SR reducer
//          > analog BBD-style delay (filtered, saturated feedback, ping-pong)
//          > space reverb (8-line FDN)
// =============================================================================
#include "OmniDsp.h"

namespace oc
{
struct SynthParams
{
    // oscillators
    float oscAShape = 0.65f;     // 0 sine .. 0.33 tri .. 0.66 saw .. 1 square
    int   oscBWave  = 3;         // 0..7 lofi tables
    float oscBBits  = 6.0f;      // 2..8 bits
    float oscBSemi  = 12.0f;     // -24..24
    float oscMix    = 0.35f;     // 0 = A only, 1 = B only
    float detune    = 0.25f;     // 0..1 analog drift / unison spread
    float noise     = 0.0f;      // 0..1
    float glide     = 0.0f;      // seconds
    // filter
    float cutoff    = 2400.0f;   // Hz
    float resonance = 0.25f;     // 0..1
    float envAmt    = 0.45f;     // -1..1 (octaves * 6)
    float keyTrack  = 0.5f;      // 0..1
    float drive     = 0.25f;     // 0..1
    // envelopes (seconds / level)
    float attack = 0.005f, decay = 0.35f, sustain = 0.7f, release = 0.45f;
    float fAttack = 0.003f, fDecay = 0.4f, fSustain = 0.25f, fRelease = 0.4f;
    // fx
    float tapeMix = 0.35f, tapeRate = 0.6f, tapeDepth = 0.4f, tapeWow = 0.3f;
    float crushBits = 16.0f, crushDown = 1.0f, crushMix = 0.0f;
    float delayTime = 0.375f, delayFb = 0.38f, delayMix = 0.22f, delayTone = 0.55f;
    float spaceSize = 0.72f, spaceDamp = 0.45f, spaceMix = 0.28f;
    float outputDb = -6.0f;
    // cosmic
    float shimmer = 0.0f;                              // octave-up feedback inside the space reverb
    float warpMix = 0.0f, warpHz = 220.0f;             // ring modulator
    float mutAmt = 0.0f, mutRate = 0.3f;               // random sample & hold on filter + pitch
    float orbitMix = 0.0f, orbitRate = 0.25f;          // phaser + auto-pan
    // set by the synth while rendering (mutant S&H)
    float mutCutOct = 0.0f, mutPitch = 0.0f;
};

// -----------------------------------------------------------------------------
class Voice
{
public:
    void prepare (float sampleRate) { fs = sampleRate; dc.prepare (fs, 8.0f); reset(); }
    void reset() { amp.kill(); fenv.kill(); f1.reset(); f2.reset(); note = -1; }
    void start (int midiNote, float velocity, const SynthParams& p, bool legato, uint32_t order);
    void stop() { amp.gateOff(); fenv.gateOff(); held = false; }
    bool active() const { return amp.active(); }
    void render (float* out, int n, const SynthParams& p, const LofiTables& tables);

    int note = -1; bool held = false; uint32_t age = 0; float vel = 0.0f;
    float level() const { return amp.v; }

private:
    float fs = 48000.0f;
    float curMidi = 60.0f, tgtMidi = 60.0f;
    double phA = 0.0, phB = 0.0, phA2 = 0.31;
    float drift = 0.0f, driftTgt = 0.0f; int driftCount = 0;
    ADSR amp, fenv; SVF f1, f2; DCBlock dc; Noise nz;
    int filterTick = 0;
};

// -----------------------------------------------------------------------------
class TapeChorus
{
public:
    void prepare (float sampleRate);
    void process (float* L, float* R, int n, const SynthParams& p);
private:
    float fs = 48000.0f; std::vector<float> bufL, bufR; int w = 0;
    double lfo = 0.0, wow = 0.0; float flut = 0.0f; Noise nz; OnePole flutLp;
    float read (const std::vector<float>& b, float delaySamples) const;
};

class Crusher
{
public:
    void process (float* L, float* R, int n, const SynthParams& p);
private:
    float holdL = 0.0f, holdR = 0.0f, phase = 1.0f;
};

class AnalogDelay
{
public:
    void prepare (float sampleRate);
    void process (float* L, float* R, int n, const SynthParams& p);
private:
    float fs = 48000.0f; std::vector<float> bufL, bufR; int w = 0;
    Smoothed time; OnePole lpL, lpR; SVF hpL, hpR; double mod = 0.0;
};

/** Octave-up pitch shifter (two crossfaded read heads) for the shimmer. */
class OctaveUp
{
public:
    void prepare (float sampleRate) { fs = sampleRate; buf.assign (4096, 0.0f); w = 0; ph = 0.0f; }
    float process (float x)
    {
        const int sz = (int) buf.size();
        buf[(size_t) w] = x;
        const float win = 2048.0f;
        ph += 1.0f; if (ph >= win) ph -= win;                     // read heads run 2x -> +12 st
        auto tap = [&] (float d) { float rp = (float) w - d; while (rp < 0) rp += (float) sz; const int i0 = (int) rp; const float fr = rp - (float) i0;
                                   return buf[(size_t) i0] + (buf[(size_t) ((i0 + 1) % sz)] - buf[(size_t) i0]) * fr; };
        const float d1 = win - ph, d2 = std::fmod (win - ph + win * 0.5f, win);
        const float g1 = std::sin (kPi * (d1 / win)), g2 = std::sin (kPi * (d2 / win));
        if (++w >= sz) w = 0;
        return tap (d1 + 2.0f) * g1 + tap (d2 + 2.0f) * g2;
    }
private:
    float fs = 48000.0f; std::vector<float> buf; int w = 0; float ph = 0.0f;
};

/** Ring modulator ("WARP") */
class Warp
{
public:
    void prepare (float sampleRate) { fs = sampleRate; }
    void process (float* L, float* R, int n, const SynthParams& p)
    {
        if (p.warpMix <= 0.001f) return;
        for (int i = 0; i < n; ++i)
        {
            ph += p.warpHz / fs; if (ph >= 1.0) ph -= 1.0;
            const float m = (float) std::sin (kTwoPi * ph);
            const float g = 1.0f - p.warpMix + p.warpMix * m * 1.3f;
            L[i] *= g; R[i] *= g;
        }
    }
private:
    float fs = 48000.0f; double ph = 0.0;
};

/** "ORBIT": 6-stage stereo phaser + slow auto-pan around the head. */
class Orbit
{
public:
    void prepare (float sampleRate) { fs = sampleRate; zl.fill (0.0f); zr.fill (0.0f); }
    void process (float* L, float* R, int n, const SynthParams& p)
    {
        if (p.orbitMix <= 0.001f) return;
        const float rate = 0.03f + p.orbitRate * 1.6f;
        for (int i = 0; i < n; ++i)
        {
            ph += rate / fs; if (ph >= 1.0) ph -= 1.0;
            const float s1 = (float) std::sin (kTwoPi * ph), s2 = (float) std::sin (kTwoPi * ph + 1.9);
            auto coef = [&] (float lfo) { const float hz = 180.0f * std::exp2 (3.6f * (0.5f + 0.5f * lfo)); const float t = std::tan (kPi * hz / fs); return (t - 1.0f) / (t + 1.0f); };
            const float al = coef (s1), ar = coef (s2);
            float xl = L[i] + fbl * 0.55f, xr = R[i] + fbr * 0.55f;
            for (int k = 0; k < 6; ++k)
            {
                const float yl = al * xl + zl[(size_t) k]; zl[(size_t) k] = xl - al * yl; xl = yl;
                const float yr = ar * xr + zr[(size_t) k]; zr[(size_t) k] = xr - ar * yr; xr = yr;
            }
            fbl = xl; fbr = xr;
            const float pan = 0.5f + 0.5f * (float) std::sin (kTwoPi * ph * 0.5);
            const float m = p.orbitMix;
            const float ol = (L[i] + xl) * 0.5f, orr = (R[i] + xr) * 0.5f;
            L[i] = L[i] * (1.0f - m) + m * ol * (0.4f + 1.2f * (1.0f - pan));
            R[i] = R[i] * (1.0f - m) + m * orr * (0.4f + 1.2f * pan);
        }
    }
private:
    float fs = 48000.0f; double ph = 0.0; std::array<float, 6> zl {}, zr {}; float fbl = 0.0f, fbr = 0.0f;
};

class SpaceReverb
{
public:
    void prepare (float sampleRate);
    void process (float* L, float* R, int n, const SynthParams& p);
private:
    OctaveUp shimmerShift; float shimmerFb = 0.0f; OnePole shimmerLp;
    static constexpr int N = 8;
    float fs = 48000.0f;
    std::array<std::vector<float>, N> lines; std::array<int, N> len {}, pos {};
    std::array<float, N> damp {};
    std::vector<float> pre; int prePos = 0;
    std::array<std::vector<float>, 2> ap; std::array<int, 2> apPos {};
};

// -----------------------------------------------------------------------------
class OmniSynth
{
public:
    static constexpr int maxVoices = 12;

    void prepare (double sampleRate, int maxBlock);
    void setParams (const SynthParams& p) { params = p; }
    const SynthParams& getParams() const { return params; }

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff();

    /** Renders voices + FX for n samples (stereo, overwrite). */
    void render (float* L, float* R, int n);

    /** For the display: recent mono output (ring buffer of 512) and peak. */
    std::array<float, 512> scope {}; int scopePos = 0;
    float peak = 0.0f;
    int activeVoices() const { int c = 0; for (auto& v : voices) if (v.active()) ++c; return c; }
    /** out[note] = true for every note that is held or still ringing. */
    void soundingNotes (bool* out128) const
    {
        for (int i = 0; i < 128; ++i) out128[i] = false;
        for (auto& v : voices) if (v.active() && v.note >= 0 && v.note < 128 && (v.held || v.level() > 0.05f)) out128[v.note] = true;
    }

private:
    double fs = 48000.0;
    SynthParams params;
    LofiTables tables;
    std::array<Voice, maxVoices> voices;
    uint32_t order = 0;
    std::vector<float> mono;
    TapeChorus tape; Crusher crush; Warp warp; Orbit orbit; AnalogDelay delay; SpaceReverb space;
    Noise mutNoise; double mutPhase = 0.0; float mutA = 0.0f, mutB = 0.0f, mutSmA = 0.0f, mutSmB = 0.0f;
    Smoothed out;
};
} // namespace oc
