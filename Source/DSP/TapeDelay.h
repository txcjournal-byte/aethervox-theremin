#pragma once
// =============================================================================
//  TapeDelay - stereo tape echo
//   * delay-time changes move the "tape head" with transport inertia
//     (pitch-bending repeats, just like a real tape echo)
//   * wow (0.55 Hz) + flutter (6.3 Hz) modulation
//   * feedback path: head-bump high-pass, tape high-cut, soft saturation
//   * Hermite (4-point) interpolation, gentle cross-feedback for width
// =============================================================================
#include "DspUtils.h"
#include <vector>

namespace av
{
class TapeDelay
{
public:
    void prepare (double sampleRate)
    {
        fs   = sampleRate;
        size = nextPow2 ((int) (fs * 2.2) + 16);
        mask = size - 1;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);

        slew    = onePoleCoef (250.0f, fs);           // tape transport inertia
        lpCoef  = onePoleCutoff (4200.0f, fs);
        hpCoef  = onePoleCutoff (110.0f, fs);
        wowInc  = 0.55 / fs;
        flutInc = 6.3  / fs;
        stereoOffset = (float) (0.009 * fs);

        mix.reset (fs, 0.05, 0.0f);
        feedback.reset (fs, 0.05, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        write = 0;
        curDelay = targetDelay;
        lpL = lpR = hpL = hpR = 0.0f;
    }

    void setParameters (float timeMs, float fb, float mixAmount)
    {
        targetDelay = clampv (timeMs, 1.0f, 1600.0f) * 0.001f * (float) fs;
        feedback.setTarget (clampv (fb, 0.0f, 0.95f));
        mix.setTarget (clampv (mixAmount, 0.0f, 1.0f));
    }

    void process (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            curDelay += (targetDelay - curDelay) * slew;

            wowPh  += wowInc;  wowPh  -= std::floor (wowPh);
            flutPh += flutInc; flutPh -= std::floor (flutPh);
            const float mod = (float) ((std::sin (kTwoPi * wowPh) * 0.0011
                                      + std::sin (kTwoPi * flutPh) * 0.00012) * fs);

            const float dL = std::max (4.0f, curDelay + mod);
            const float dR = std::max (4.0f, curDelay + stereoOffset + 0.85f * mod);

            const float rL = read (bufL, dL);
            const float rR = read (bufR, dR);

            const float fL = tapePath (rL, hpL, lpL);
            const float fR = tapePath (rR, hpR, lpR);

            const float g = feedback.next();
            const float m = mix.next();

            bufL[(size_t) write] = L[i] + g * (0.82f * fL + 0.18f * fR);
            bufR[(size_t) write] = R[i] + g * (0.82f * fR + 0.18f * fL);
            write = (write + 1) & mask;

            L[i] = L[i] * (1.0f - 0.3f * m) + m * rL;
            R[i] = R[i] * (1.0f - 0.3f * m) + m * rR;
        }
    }

private:
    inline float tapePath (float x, float& hp, float& lp) const
    {
        hp += hpCoef * (x - hp);
        float y = x - hp;                 // remove low rumble build-up
        lp += lpCoef * (y - lp);          // tape high-frequency loss
        return std::tanh (lp * 1.3f) / 1.3f;
    }

    inline float read (const std::vector<float>& b, float delaySamples) const
    {
        float rp = (float) write - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        const int   i = (int) rp;
        const float f = rp - (float) i;
        const float xm1 = b[(size_t) ((i - 1) & mask)];
        const float x0  = b[(size_t) ( i      & mask)];
        const float x1  = b[(size_t) ((i + 1) & mask)];
        const float x2  = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double fs = 48000.0, wowPh = 0.0, flutPh = 0.25, wowInc = 0.0, flutInc = 0.0;
    std::vector<float> bufL, bufR;
    int size = 0, mask = 0, write = 0;
    float curDelay = 0.0f, targetDelay = 18000.0f, slew = 0.0f, stereoOffset = 0.0f;
    float lpCoef = 0.0f, hpCoef = 0.0f, lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    LinearSmoother mix, feedback;
};
} // namespace av
