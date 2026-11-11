#pragma once
// =============================================================================
//  FormantFilter - dual formant / body resonator
//
//  Two parallel resonant band-passes (TPT SVF) model the "body" of the
//  instrument: internal tuned circuits, the cabinet and the speaker cone.
//  Presets:
//    0  Off
//    1  Ethereal / Sci-Fi   - two narrow high resonances, slowly drifting
//    2  Soprano Voice       - "ah" vowel formants that lift with pitch
//                             (vowel modification, like a real soprano)
//    3  Vintage Tube        - broad cabinet thump + speaker-cone resonance
//  Preset changes are glided, so switching while playing never clicks.
// =============================================================================
#include "DspUtils.h"
#include <array>

namespace av
{
class FormantFilter
{
public:
    enum Preset { Off = 0, Ethereal, Soprano, VintageTube, NumPresets };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        mix.reset (fs, 0.08, 0.0f);
        reset();
    }

    void reset()
    {
        bp1.reset(); bp2.reset();
        const auto& p = presets[(size_t) presetIndex];
        f1 = p.f1; f2 = p.f2; q1 = p.q1; q2 = p.q2; g1 = p.g1; g2 = p.g2; makeup = p.makeup;
        lfoPhase = 0.0;
    }

    void setPreset (int index)   { presetIndex = clampv (index, 0, (int) NumPresets - 1); }
    void setMix (float m)        { userMix = clampv (m, 0.0f, 1.0f); }
    void setPitch (float hz)     { pitchHz = hz; }
    /** -1..1, e.g. MPE "slide" (CC74). Shifts both formants by up to ±~0.45 oct. */
    void setShift (float s)      { shift = clampv (s, -1.0f, 1.0f); }

    void process (float* x, int n)
    {
        const auto& p = presets[(size_t) presetIndex];
        mix.setTarget (presetIndex == Off ? 0.0f : userMix);

        float tf1 = p.f1, tf2 = p.f2;
        if (p.track > 0.0f)
        {
            tf1 = std::max (tf1, pitchHz * 1.12f);   // F1 never below the fundamental
            tf2 = std::max (tf2, tf1 * 1.45f);
        }
        const float sh = std::exp2 (shift * 0.45f);

        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0) updateCoefficients (p, tf1 * sh, tf2 * sh);

            const float in  = x[i];
            const float y1  = bp1.bandNorm (in);
            const float y2  = bp2.bandNorm (in);
            const float wet = (g1 * y1 + g2 * y2) * makeup;
            const float m   = mix.next();
            x[i] = in + m * (wet - in);
        }
    }

private:
    struct PresetData { float f1, q1, g1, f2, q2, g2, lfoRate, lfoDepth, track, makeup; };

    void updateCoefficients (const PresetData& p, float tf1, float tf2)
    {
        constexpr float s = 0.12f;      // glide speed per 16-sample block
        double lfo = 0.0;
        if (p.lfoRate > 0.0f)
        {
            lfoPhase += 16.0 * p.lfoRate / fs;
            lfoPhase -= std::floor (lfoPhase);
            lfo = std::sin (kTwoPi * lfoPhase);
        }
        const float m1 = 1.0f + p.lfoDepth * (float) lfo;
        const float m2 = 1.0f - 0.7f * p.lfoDepth * (float) lfo;

        f1 += (tf1 * m1 - f1) * s;   f2 += (tf2 * m2 - f2) * s;
        q1 += (p.q1 - q1) * s;       q2 += (p.q2 - q2) * s;
        g1 += (p.g1 - g1) * s;       g2 += (p.g2 - g2) * s;
        makeup += (p.makeup - makeup) * s;

        bp1.set (f1, q1, fs);
        bp2.set (f2, q2, fs);
    }

    static constexpr std::array<PresetData, NumPresets> presets {{
        //  f1     q1    g1    f2     q2    g2    lfoHz lfoDep track makeup
        { 1000.f, 1.0f, 0.0f, 2000.f, 1.0f, 0.0f, 0.0f,  0.0f,  0.0f, 1.0f },  // Off
        { 1350.f, 9.0f, 1.0f, 3600.f, 11.f, 0.8f, 0.11f, 0.22f, 0.0f, 2.2f },  // Ethereal
        {  820.f, 7.0f, 1.0f, 1180.f, 9.0f, 0.7f, 0.0f,  0.0f,  1.0f, 2.0f },  // Soprano
        {  240.f, 1.1f, 0.8f, 1650.f, 1.6f, 0.6f, 0.0f,  0.0f,  0.0f, 1.4f }   // Vintage tube
    }};

    double fs = 48000.0, lfoPhase = 0.0;
    int presetIndex = VintageTube;
    float userMix = 0.5f, pitchHz = 440.0f, shift = 0.0f;
    float f1 = 240.f, f2 = 1650.f, q1 = 1.f, q2 = 1.f, g1 = 0.f, g2 = 0.f, makeup = 1.f;
    LinearSmoother mix;
    TptSvf bp1, bp2;
};
} // namespace av
