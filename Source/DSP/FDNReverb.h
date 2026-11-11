#pragma once
// =============================================================================
//  FDNReverb - algorithmic "Space / Hall" reverb
//   * 25 ms pre-delay
//   * 4 series Schroeder all-pass diffusers
//   * 8-line Feedback Delay Network, Householder feedback matrix
//   * per-line damping (high frequencies decay faster, like air)
//   * slow delay modulation on 4 lines -> no metallic ringing
//   * Size controls room dimensions (line lengths) AND RT60 (1 s .. 12 s)
// =============================================================================
#include "DspUtils.h"
#include <array>
#include <vector>

namespace av
{
class FDNReverb
{
public:
    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        const int maxLine = (int) std::ceil (baseMs[N - 1] * maxScale * 0.001 * fs) + 128;
        lineSize = nextPow2 (maxLine);
        lineMask = lineSize - 1;
        for (auto& l : lines) l.assign ((size_t) lineSize, 0.0f);

        for (size_t a = 0; a < apMs.size(); ++a)
        {
            apLen[a] = std::max (1, (int) (apMs[a] * 0.001 * fs));
            apBuf[a].assign ((size_t) apLen[a], 0.0f);
            apIdx[a] = 0;
        }

        preLen  = (int) (0.025 * fs);
        preSize = nextPow2 (preLen + 4);
        preBuf.assign ((size_t) preSize, 0.0f);

        lfoInc = 0.31 / fs;
        modDepth = (float) (0.0006 * fs);
        mix.reset (fs, 0.05, 0.0f);
        sizeSm = targetSize;
        computeTargets();
        for (int k = 0; k < N; ++k) curLen[(size_t) k] = tgtLen[(size_t) k];
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& b : apBuf) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (preBuf.begin(), preBuf.end(), 0.0f);
        lp.fill (0.0f);
        w = 0; preW = 0;
    }

    void setParameters (float size, float mixAmount)
    {
        targetSize = clampv (size, 0.0f, 1.0f);
        mix.setTarget (clampv (mixAmount, 0.0f, 1.0f));
    }

    void process (float* L, float* R, int n)
    {
        sizeSm += (targetSize - sizeSm) * 0.2f;
        computeTargets();

        std::array<float, N> out {};
        for (int i = 0; i < n; ++i)
        {
            // pre-delay
            preBuf[(size_t) preW] = 0.5f * (L[i] + R[i]);
            float x = preBuf[(size_t) ((preW - preLen) & (preSize - 1))];
            preW = (preW + 1) & (preSize - 1);

            // input diffusion
            for (size_t a = 0; a < apBuf.size(); ++a)
            {
                float& slot = apBuf[a][(size_t) apIdx[a]];
                const float d  = slot;
                const float wv = x + apG * d;
                slot = wv;
                x = d - apG * wv;
                if (++apIdx[a] >= apLen[a]) apIdx[a] = 0;
            }

            // modulation (quadrature LFO)
            lfoPh += lfoInc; lfoPh -= std::floor (lfoPh);
            const float s = (float) std::sin (kTwoPi * lfoPh) * modDepth;
            const float c = (float) std::cos (kTwoPi * lfoPh) * modDepth;
            const float mods[N] = { s, c, -s, -c, 0, 0, 0, 0 };

            // read lines
            float sum = 0.0f;
            std::array<float, N> sv {};
            for (int k = 0; k < N; ++k)
            {
                auto kk = (size_t) k;
                curLen[kk] += (tgtLen[kk] - curLen[kk]) * 0.0007f;
                out[kk] = readLinear (lines[kk], curLen[kk] + mods[k]);
                lp[kk] += damp * (out[kk] - lp[kk]);
                sv[kk] = lp[kk] * gain[kk];
                sum += sv[kk];
            }

            // Householder feedback matrix:  A = I - 2/N * 1*1^T
            const float h = sum * (2.0f / (float) N);
            for (int k = 0; k < N; ++k)
            {
                auto kk = (size_t) k;
                lines[kk][(size_t) w] = sv[kk] - h + x * inSign[kk] * 0.35f;
            }
            w = (w + 1) & lineMask;

            const float wetL = (out[0] - out[2] + out[4] - out[6]) * 0.3f;
            const float wetR = (out[1] - out[3] + out[5] - out[7]) * 0.3f;
            const float m = mix.next();
            L[i] = L[i] * (1.0f - 0.5f * m) + m * wetL;
            R[i] = R[i] * (1.0f - 0.5f * m) + m * wetR;
        }
    }

private:
    void computeTargets()
    {
        const float scale = 0.45f + 1.15f * sizeSm;                   // room size
        const float rt60  = 1.0f + 11.0f * sizeSm * sizeSm;           // seconds
        for (int k = 0; k < N; ++k)
        {
            auto kk = (size_t) k;
            tgtLen[kk] = (float) (baseMs[kk] * scale * 0.001 * fs);
            gain[kk]   = std::pow (10.0f, -3.0f * tgtLen[kk] / (rt60 * (float) fs));
        }
        damp = onePoleCutoff (8000.0f - 4500.0f * sizeSm, fs);
    }

    inline float readLinear (const std::vector<float>& b, float d) const
    {
        float rp = (float) w - d;
        while (rp < 0.0f) rp += (float) lineSize;
        const int   i = (int) rp;
        const float f = rp - (float) i;
        return b[(size_t) (i & lineMask)] * (1.0f - f) + b[(size_t) ((i + 1) & lineMask)] * f;
    }

    static constexpr std::array<double, N> baseMs { 29.7, 37.1, 41.1, 43.7, 53.3, 59.9, 67.1, 73.3 };
    static constexpr std::array<float,  N> inSign { 1.f, -1.f, 1.f, 1.f, -1.f, 1.f, -1.f, -1.f };
    static constexpr std::array<double, 4> apMs   { 4.71, 3.59, 12.73, 9.31 };
    static constexpr double maxScale = 1.62;
    static constexpr float  apG = 0.62f;

    double fs = 48000.0, lfoPh = 0.0, lfoInc = 0.0;
    std::array<std::vector<float>, N> lines;
    std::array<float, N> curLen {}, tgtLen {}, gain {}, lp {};
    std::array<std::vector<float>, 4> apBuf;
    std::array<int, 4> apLen {}, apIdx {};
    std::vector<float> preBuf;
    int lineSize = 0, lineMask = 0, w = 0, preLen = 0, preSize = 0, preW = 0;
    float damp = 0.5f, modDepth = 0.0f, sizeSm = 0.7f, targetSize = 0.7f;
    LinearSmoother mix;
};
} // namespace av
