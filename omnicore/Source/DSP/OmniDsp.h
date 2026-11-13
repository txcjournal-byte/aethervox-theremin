#pragma once
// =============================================================================
//  OmniCore 3000 - DSP building blocks (pure C++17, no JUCE, testable offline)
// =============================================================================
#include <array>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace oc
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

template <typename T> inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float midiToHz (float m)  { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

/** Fast, smooth soft clipper (tanh-like, cheaper, exact at +-1). */
inline float softClip (float x)
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// -----------------------------------------------------------------------------
struct Smoothed
{
    void reset (double fs, double seconds, float v) { coef = (float) (1.0 - std::exp (-1.0 / (seconds * fs))); cur = tgt = v; }
    void set (float v) { tgt = v; }
    float next() { cur += (tgt - cur) * coef; return cur; }
    float cur = 0.0f, tgt = 0.0f, coef = 1.0f;
};

struct DCBlock
{
    void prepare (double fs, float hz = 10.0f) { R = (float) std::exp (-kTwoPi * hz / fs); x1 = y1 = 0.0f; }
    float process (float x) { const float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
    float R = 0.995f, x1 = 0.0f, y1 = 0.0f;
};

/** Cheap white noise (xorshift). */
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
};

// -----------------------------------------------------------------------------
//  Zero-delay-feedback state variable filter (Cytomic / Simper)
// -----------------------------------------------------------------------------
struct SVF
{
    void reset() { ic1 = ic2 = 0.0f; }
    void set (float hz, float q, float fs)
    {
        g  = std::tan (kPi * clampv (hz, 15.0f, fs * 0.45f) / fs);
        k  = 1.0f / clampv (q, 0.3f, 25.0f);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void process (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float lowpass (float x) { float l, b, h; process (x, l, b, h); return l; }
    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
};

/** One pole low pass. */
struct OnePole
{
    void set (float hz, float fs) { a = std::exp (-kTwoPi * clampv (hz, 5.0f, fs * 0.49f) / fs); }
    float process (float x) { z = x + a * (z - x); return z; }
    float a = 0.0f, z = 0.0f;
};

// -----------------------------------------------------------------------------
//  ADSR (exponential-ish segments, click free)
// -----------------------------------------------------------------------------
struct ADSR
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    void set (float a, float d, float s, float r, float fs)
    {
        aCoef = rate (a, fs); dCoef = rate (d, fs); rCoef = rate (r, fs); sus = clampv (s, 0.0f, 1.0f);
    }
    void gateOn (bool retrigger)  { stage = Attack; if (retrigger) v = std::min (v, 0.0f); }
    void gateOff() { if (stage != Idle) stage = Release; }
    void kill() { stage = Idle; v = 0.0f; }
    bool active() const { return stage != Idle; }
    float next()
    {
        switch (stage)
        {
            case Attack:  v += (1.25f - v) * aCoef; if (v >= 1.0f) { v = 1.0f; stage = Decay; } break;
            case Decay:   v += (sus - v) * dCoef; if (std::fabs (v - sus) < 1.0e-4f) { v = sus; stage = Sustain; } break;
            case Sustain: v = sus; break;
            case Release: v += (0.0f - v) * rCoef; if (v < 1.0e-5f) { v = 0.0f; stage = Idle; } break;
            default: break;
        }
        return v;
    }
    static float rate (float seconds, float fs) { return 1.0f - std::exp (-1.0f / (std::max (0.0005f, seconds) * fs * 0.25f)); }
    Stage stage = Idle; float v = 0.0f, aCoef = 1.0f, dCoef = 1.0f, rCoef = 1.0f, sus = 1.0f;
};

// -----------------------------------------------------------------------------
//  polyBLEP helpers for the analog oscillator
// -----------------------------------------------------------------------------
inline float polyBlep (float t, float dt)
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/** 8-bit style wavetables: 32 steps, classic console / toy keyboard shapes. */
struct LofiTables
{
    static constexpr int numWaves = 8, len = 32;
    std::array<std::array<float, len>, numWaves> t {};
    LofiTables()
    {
        for (int i = 0; i < len; ++i)
        {
            const float p = (float) i / len;
            t[0][(size_t) i] = std::sin (kTwoPi * p);                                        // pure
            t[1][(size_t) i] = p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;                 // NES triangle
            t[2][(size_t) i] = 2.0f * p - 1.0f;                                              // ramp
            t[3][(size_t) i] = p < 0.125f ? 1.0f : -0.6f;                                    // 12.5% pulse
            t[4][(size_t) i] = p < 0.25f ? 1.0f : -0.6f;                                     // 25% pulse
            t[5][(size_t) i] = std::sin (kTwoPi * p) * 0.6f + std::sin (kTwoPi * 3 * p) * 0.4f; // organ
            t[6][(size_t) i] = std::sin (kTwoPi * p + 2.5f * std::sin (kTwoPi * 2 * p));     // FM bell
            t[7][(size_t) i] = (i % 8 < 4 ? 1.0f : -1.0f) * (1.0f - p);                     // buzz
        }
        for (auto& w : t)        // normalise
        {
            float pk = 0.0f; for (float v : w) pk = std::max (pk, std::fabs (v));
            for (auto& v : w) v /= (pk > 0.0f ? pk : 1.0f);
        }
    }
};
} // namespace oc
