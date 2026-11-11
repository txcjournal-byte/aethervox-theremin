// Offline test of the pure-C++ DSP core (no JUCE needed).
//   g++ -std=c++17 -O2 -I../Source/DSP DspTest.cpp ../Source/DSP/ThereminEngine.cpp -o dsptest
#include "ThereminEngine.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <fstream>
#include <cstdint>

using namespace av;

static double measureHz (const std::vector<float>& x, double fs)
{
    // rising zero crossings with linear interpolation
    std::vector<double> zc;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            zc.push_back ((double) (i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    if (zc.size() < 3) return 0.0;
    return fs * (double) (zc.size() - 1) / (zc.back() - zc.front());
}

static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    std::ofstream f (path, std::ios::binary);
    auto w32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto w16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t n = (uint32_t) L.size();
    f.write ("RIFF", 4); w32 (36 + n * 4); f.write ("WAVEfmt ", 8);
    w32 (16); w16 (1); w16 (2); w32 ((uint32_t) fs); w32 ((uint32_t) fs * 4); w16 (4); w16 (16);
    f.write ("data", 4); w32 (n * 4);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) std::lround (clampv (L[i], -1.0f, 1.0f) * 32767.0f);
        int16_t b = (int16_t) std::lround (clampv (R[i], -1.0f, 1.0f) * 32767.0f);
        w16 ((uint16_t) a); w16 ((uint16_t) b);
    }
}

int main()
{
    const double fs = 48000.0;
    int failures = 0;

    // ---------- 1) pitch accuracy of the heterodyne oscillator (with coupling!)
    for (float coupling : { 0.0f, 0.5f, 1.0f })
        for (double hz : { 25.0, 55.0, 220.0, 440.0, 1760.0, 2900.0 })
        {
            HeterodyneOscillator o; o.prepare (fs); o.setCoupling (coupling);
            std::vector<float> buf ((size_t) fs);
            for (auto& s : buf) s = o.process (hz).value;
            double mean = 0; for (auto s : buf) mean += s; mean /= (double) buf.size();
            for (auto& s : buf) s -= (float) mean;
            const double m = measureHz (buf, fs);
            const double cents = 1200.0 * std::log2 (m / hz);
            const bool ok = std::abs (cents) < 2.0;
            if (! ok) ++failures;
            std::printf ("osc  coupling=%.1f  %7.1f Hz -> %8.2f Hz  (%+.2f c) %s\n", coupling, hz, m, cents, ok ? "OK" : "FAIL");
        }

    // ---------- 2) quantizer
    {
        PitchQuantizer q; q.setScale (PitchQuantizer::Major, 0);
        const float a = q.process (61.4f, 1.0f);   // C#+40 -> D (62) ... nearest in C major: 60 or 62 -> 62 (0.6 vs 1.4)? 61.4: |60|=1.4 |62|=0.6
        const float b = q.process (66.2f, 1.0f);   // F#+20 -> F (65)? |65|=1.2 |67|=0.8 -> 67
        const float c = q.process (61.4f, 0.5f);
        std::printf ("quant  61.4->%.2f  66.2->%.2f  half:%.2f\n", a, b, c);
        if (a != 62.0f || b != 67.0f || std::abs (c - 61.7f) > 1e-4f) ++failures;
    }

    // ---------- 3) full engine render: phrase with all modules active, check NaN/level
    ThereminEngine e;
    EngineParams p;
    p.morph = 0.35f; p.drive = 0.5f; p.formant = FormantFilter::Soprano; p.formantMix = 0.6f;
    p.quantAmount = 0.0f; p.glideMs = 90.0f; p.delayMix = 0.3f; p.reverbMix = 0.35f;
    e.prepare (fs);
    e.setParameters (p);

    const int block = 256;
    const int total = (int) (fs * 9.0);
    std::vector<float> L ((size_t) total), R ((size_t) total), mono (block);
    const float notes[] = { 60, 67, 72, 71, 67, 64, 69, 76 };
    double peak = 0, sumSq = 0; bool nan = false;
    for (int pos = 0; pos < total; pos += block)
    {
        const double t = pos / fs;
        const int idx = (int) (t / 0.9);
        const bool on = idx < 8 && std::fmod (t, 0.9) < 0.8;
        if (pos > (int) (fs * 4.5)) { p.morph = 1.0f; p.formant = FormantFilter::Ethereal; p.quantAmount = 1.0f; e.setParameters (p); }
        e.setTarget (on ? notes[idx] + 0.3f * (float) std::sin (t * 3.0) : notes[std::min (idx, 7)], on ? 0.9f : 0.0f);
        const int n = std::min (block, total - pos);
        e.renderVoice (mono.data(), n);
        e.processPost (mono.data(), L.data() + pos, R.data() + pos, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = L[(size_t) (pos + i)];
            if (! std::isfinite (v) || ! std::isfinite (R[(size_t) (pos + i)])) nan = true;
            peak = std::max (peak, (double) std::abs (v));
            sumSq += (double) v * v;
        }
    }
    const double rms = std::sqrt (sumSq / total);
    std::printf ("engine  peak=%.3f  rms=%.3f  nan=%d  latency=%d\n", peak, rms, (int) nan, e.getLatencySamples());
    if (nan || peak > 1.5 || rms < 0.01) ++failures;
    writeWav ("theremin_test.wav", L, R, (int) fs);

    // ---------- 4) engine pitch accuracy with FX off & vibrato off
    {
        ThereminEngine e2; EngineParams q;
        q.vibDepth = 0; q.humanize = 0; q.drift = 0; q.tremolo = 0; q.drive = 0;
        q.formant = 0; q.delayMix = 0; q.reverbMix = 0; q.brightness = 1.0f; q.glideMs = 0; q.coupling = 0.6f;
        e2.prepare (fs); e2.setParameters (q);
        e2.setTarget (69.0f, 1.0f);
        std::vector<float> out ((size_t) fs), l2 ((size_t) fs), r2 ((size_t) fs);
        for (int pos = 0; pos < (int) fs; pos += block)
        {
            const int n = std::min (block, (int) fs - pos);
            e2.renderVoice (out.data() + pos, n);
            e2.processPost (out.data() + pos, l2.data() + pos, r2.data() + pos, n);
        }
        std::vector<float> tail (l2.begin() + 4800, l2.end());
        const double m = measureHz (tail, fs);
        std::printf ("engine A4 -> %.3f Hz\n", m);
        if (std::abs (m - 440.0) > 0.5) ++failures;
    }

    // ---------- 5) polyphony: C-E-G chord, all three fundamentals present, layering + sub stay finite
    {
        ThereminEngine e3; EngineParams q;
        q.vibDepth = 0; q.humanize = 0; q.drift = 0; q.tremolo = 0; q.drive = 0; q.formant = 0;
        q.delayMix = 0; q.reverbMix = 0; q.brightness = 1.0f; q.glideMs = 0; q.layer = 0.6f; q.sub = 0.4f;
        e3.prepare (fs); e3.setParameters (q);
        e3.setVoiceTarget (0, 60.0f, 0.8f); e3.setVoiceTarget (1, 64.0f, 0.8f); e3.setVoiceTarget (2, 67.0f, 0.8f);
        const int N = (int) fs;
        std::vector<float> m ((size_t) N), l ((size_t) N), r ((size_t) N);
        for (int pos = 0; pos < N; pos += block)
        {
            const int n = std::min (block, N - pos);
            e3.renderVoice (m.data() + pos, n);
            e3.processPost (m.data() + pos, l.data() + pos, r.data() + pos, n);
        }
        bool ok = true;
        for (float f0 : { 261.63f, 329.63f, 392.0f })
        {
            double re = 0, im = 0;
            for (int i = N / 2; i < N; ++i) { re += l[(size_t) i] * std::cos (kTwoPi * f0 * i / fs); im += l[(size_t) i] * std::sin (kTwoPi * f0 * i / fs); }
            const double mag = 2.0 * std::sqrt (re * re + im * im) / (N / 2);
            std::printf ("poly  %.1f Hz magnitude %.3f\n", f0, mag);
            if (mag < 0.02) ok = false;
        }
        for (float v : l) if (! std::isfinite (v)) ok = false;
        if (! ok) ++failures;
    }

    std::printf (failures ? "FAILURES: %d\n" : "ALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
