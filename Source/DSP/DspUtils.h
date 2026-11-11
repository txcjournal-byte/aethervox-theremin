#pragma once
// =============================================================================
//  AetherVox Theremin - DSP utilities
//  Pure C++17 (no JUCE dependency) so the DSP core can be unit-tested offline.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace av
{
constexpr double kTwoPi = 6.283185307179586476925;
constexpr double kPi    = 3.141592653589793238463;

template <typename T> inline T clampv (T v, T lo, T hi) { return std::min (hi, std::max (lo, v)); }
template <typename T> inline T lerp   (T a, T b, T t)   { return a + (b - a) * t; }

inline float midiToHz (float m)  { return 440.0f * std::exp2 ((m - 69.0f) / 12.0f); }
inline float hzToMidi (float hz) { return 69.0f + 12.0f * std::log2 (std::max (hz, 1.0e-3f) / 440.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

/** One-pole smoothing coefficient for a given time constant. */
inline float onePoleCoef (float timeMs, double fs)
{
    if (timeMs <= 0.0f) return 1.0f;
    return 1.0f - std::exp (-1.0f / (timeMs * 0.001f * (float) fs));
}

/** One-pole lowpass coefficient for a cutoff frequency. */
inline float onePoleCutoff (float hz, double fs)
{
    return 1.0f - std::exp ((float) (-kTwoPi * hz / fs));
}

// -----------------------------------------------------------------------------
//  Band-limited discontinuity correction (polyBLEP / polyBLAMP, 2-point)
//  t  = phase relative to the discontinuity, in [0,1)
//  dt = phase increment per sample
// -----------------------------------------------------------------------------
inline double polyBlep (double t, double dt)
{
    if (t < dt)       { const double x = t / dt;          return x + x - x * x - 1.0; }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt;  return x * x + x + x + 1.0; }
    return 0.0;
}

inline double polyBlamp (double t, double dt)
{
    if (t < dt)       { const double x = t / dt - 1.0;         return -x * x * x / 3.0; }
    if (t > 1.0 - dt) { const double x = (t - 1.0) / dt + 1.0; return  x * x * x / 3.0; }
    return 0.0;
}

// -----------------------------------------------------------------------------
//  Small fast RNG (xorshift32)
// -----------------------------------------------------------------------------
struct Rng
{
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    uint32_t nextU32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    /** uniform in [-1, 1] */
    float bipolar() { return (float) ((double) nextU32() / 2147483647.5 - 1.0); }
    uint32_t s;
};

// -----------------------------------------------------------------------------
//  Smooth random curve (value-noise with smoothstep interpolation), range ±1.
//  Used for "human hand" micro-movements, drift and wander.
// -----------------------------------------------------------------------------
struct SmoothNoise
{
    explicit SmoothNoise (uint32_t seed = 1) : rng (seed) { a = rng.bipolar(); b = rng.bipolar(); }
    void setRate (float hz, double fs) { inc = (double) hz / fs; }
    float next()
    {
        phase += inc;
        if (phase >= 1.0) { phase -= std::floor (phase); a = b; b = rng.bipolar(); }
        const float t = (float) phase;
        const float sm = t * t * (3.0f - 2.0f * t);
        return a + (b - a) * sm;
    }
    Rng rng;
    double phase = 0.0, inc = 0.0;
    float a = 0.0f, b = 0.0f;
};

// -----------------------------------------------------------------------------
//  Linear parameter smoother (zipper-free parameter changes)
// -----------------------------------------------------------------------------
struct LinearSmoother
{
    void reset (double fs, double seconds, float value)
    {
        length = std::max (1, (int) (fs * seconds));
        current = target = value; remaining = 0;
    }
    void setTarget (float t)
    {
        if (std::abs (t - target) < 1.0e-9f) return;
        target = t; remaining = length; step = (target - current) / (float) length;
    }
    float next()
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int length = 1, remaining = 0;
};

// -----------------------------------------------------------------------------
//  DC blocker
// -----------------------------------------------------------------------------
struct DCBlocker
{
    void prepare (double fs, float hz = 8.0f) { R = (float) std::exp (-kTwoPi * hz / fs); reset(); }
    void reset() { x1 = y1 = 0.0f; }
    float process (float x) { const float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
    float x1 = 0.0f, y1 = 0.0f, R = 0.995f;
};

// -----------------------------------------------------------------------------
//  Topology-preserving-transform state variable filter (A. Simper / Cytomic)
//  Cheap to re-tune per sample -> ideal for modulated formants / brightness.
// -----------------------------------------------------------------------------
struct TptSvf
{
    void set (float fc, float q, double fs)
    {
        fc = clampv (fc, 10.0f, (float) (fs * 0.45));
        g  = (float) std::tan (kPi * fc / fs);
        k  = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0f; }

    inline void tick (float v0, float& lp, float& bp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1;
    }
    float lowpass (float x)  { float l, b; tick (x, l, b); return l; }
    /** Band-pass normalised to 0 dB at the centre frequency. */
    float bandNorm (float x) { float l, b; tick (x, l, b); return b * k; }

    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

} // namespace av
