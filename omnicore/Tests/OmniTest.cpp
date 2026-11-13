// Offline test + demo render: chord pad, keyboard chords, strum, arp; checks for NaN / silence / clipping.
#include "ChordEngine.h"
#include <cstdio>
#include <cstdint>
using namespace oc;

static void writeWav (const char* path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    FILE* f = std::fopen (path, "wb"); if (! f) return;
    uint32_t n = (uint32_t) L.size(), bytes = n * 4, v; uint16_t s;
    std::fwrite ("RIFF", 1, 4, f); v = 36 + bytes; std::fwrite (&v, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    v = 16; std::fwrite (&v, 4, 1, f); s = 1; std::fwrite (&s, 2, 1, f); s = 2; std::fwrite (&s, 2, 1, f);
    v = (uint32_t) fs; std::fwrite (&v, 4, 1, f); v = (uint32_t) fs * 4; std::fwrite (&v, 4, 1, f);
    s = 4; std::fwrite (&s, 2, 1, f); s = 16; std::fwrite (&s, 2, 1, f); std::fwrite ("data", 1, 4, f); std::fwrite (&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) (clampv (L[i], -1.0f, 1.0f) * 32767), b = (int16_t) (clampv (R[i], -1.0f, 1.0f) * 32767);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    const int fs = 48000, block = 256;
    OmniSynth synth; ChordEngine ce;
    synth.prepare (fs, block); ce.prepare (fs);
    std::vector<float> L, R; std::vector<float> bl (block), br (block);
    int bad = 0; float pk = 0.0f;

    auto run = [&] (double seconds, double bpm, double& ppq)
    {
        const int blocks = (int) (seconds * fs / block);
        for (int b = 0; b < blocks; ++b)
        {
            ce.render (synth, bl.data(), br.data(), block, bpm, ppq);
            ppq += block / (double) fs * bpm / 60.0;
            for (int i = 0; i < block; ++i)
            {
                if (! std::isfinite (bl[(size_t) i]) || ! std::isfinite (br[(size_t) i])) ++bad;
                pk = std::max (pk, std::fabs (bl[(size_t) i]));
                L.push_back (bl[(size_t) i]); R.push_back (br[(size_t) i]);
            }
        }
    };
    double ppq = 0.0; const double bpm = 92.0;
    ChordSettings cs; SynthParams sp;

    // 1) NOTES mode + chord buttons: lush pad progression  C - Am - F - G7
    cs.mode = PlayMode::Notes; ce.setSettings (synth, cs); synth.setParams (sp);
    const int prog[4][2] = { { 0, 0 }, { 1, 9 }, { 0, 5 }, { 2, 7 } };
    for (auto& c : prog) { ce.chordButton (synth, c[0], c[1], true); run (1.3, bpm, ppq); ce.chordButton (synth, c[0], c[1], false); run (0.2, bpm, ppq); }
    run (0.8, bpm, ppq);

    // 2) STRUM: chord buttons auto-strum + plate sweeps (lofi B osc louder)
    sp.oscMix = 0.55f; sp.oscBWave = 1; sp.tapeMix = 0.55f; sp.spaceMix = 0.4f; synth.setParams (sp);
    cs.mode = PlayMode::Strum; ce.setSettings (synth, cs);
    for (auto& c : prog)
    {
        ce.chordButton (synth, c[0], c[1], true); run (0.45, bpm, ppq);
        for (int k = 0; k < ChordEngine::numStrings; ++k) { ce.strum (synth, k, 0.7f); run (0.035, bpm, ppq); }
        run (0.4, bpm, ppq); ce.chordButton (synth, c[0], c[1], false); run (0.05, bpm, ppq);
    }
    run (0.8, bpm, ppq);

    // 3) ARP synced 1/16 up-down, 2 octaves, with crunchy crusher
    sp.oscMix = 0.4f; sp.oscBWave = 3; sp.crushMix = 0.35f; sp.crushBits = 8; sp.crushDown = 3; sp.delayMix = 0.3f;
    sp.attack = 0.002f; sp.decay = 0.18f; sp.sustain = 0.3f; sp.release = 0.15f; synth.setParams (sp);
    cs.mode = PlayMode::Arp; cs.hold = false; ce.setSettings (synth, cs);
    for (auto& c : prog) { ce.chordButton (synth, c[0], c[1], true); run (60.0 / bpm * 2, bpm, ppq); ce.chordButton (synth, c[0], c[1], false); }
    run (1.0, bpm, ppq);

    // 4) CHORD mode from the keyboard (minor 7ths), tape vibrato
    sp = SynthParams(); sp.tapeMix = 1.0f; sp.tapeDepth = 0.35f; sp.oscAShape = 0.5f; synth.setParams (sp);
    cs.mode = PlayMode::Chord; cs.chordType = 4; ce.setSettings (synth, cs);
    const int keys[4] = { 57, 62, 55, 60 };
    for (int k : keys) { ce.key (synth, k, 0.8f, true); run (1.0, bpm, ppq); ce.key (synth, k, 0.0f, false); run (0.15, bpm, ppq); }
    run (2.0, bpm, ppq);

    // 5) COSMIC: shimmer + warp + mutant + orbit on a strummed chord
    sp = SynthParams(); sp.shimmer = 0.8f; sp.spaceMix = 0.5f; sp.warpMix = 0.3f; sp.warpHz = 340.0f;
    sp.mutAmt = 0.6f; sp.mutRate = 0.4f; sp.orbitMix = 0.7f; sp.oscMix = 0.5f; sp.oscBWave = 6; synth.setParams (sp);
    cs.mode = PlayMode::Strum; ce.setSettings (synth, cs);
    ce.chordButton (synth, 1, 4, true); run (0.4, bpm, ppq);
    for (int k = 0; k < ChordEngine::numStrings; ++k) { ce.strum (synth, k, 0.8f); run (0.05, bpm, ppq); }
    run (1.5, bpm, ppq); ce.chordButton (synth, 1, 4, false); run (3.0, bpm, ppq);

    std::printf ("samples=%zu  peak=%.3f  nan=%d  voices_left=%d\n", L.size(), pk, bad, synth.activeVoices());
    if (argc > 1) writeWav (argv[1], L, R, fs);
    const bool ok = bad == 0 && pk > 0.05f && pk <= 1.0f;
    std::printf (ok ? "ALL TESTS PASSED\n" : "TEST FAILED\n");
    return ok ? 0 : 1;
}
