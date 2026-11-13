#include "OmniSynth.h"

namespace oc
{
// =============================================================================
//  Voice
// =============================================================================
void Voice::start (int midiNote, float velocity, const SynthParams& p, bool legato, uint32_t ord)
{
    const bool wasActive = amp.active();
    note = midiNote; held = true; age = ord; vel = velocity;
    tgtMidi = (float) midiNote;
    if (! (legato && wasActive) || p.glide <= 0.0005f) curMidi = tgtMidi;
    if (! wasActive) { phA = 0.0; phB = 0.0; phA2 = 0.31; f1.reset(); f2.reset(); }
    amp.gateOn (! wasActive);
    fenv.gateOn (! wasActive);
}

void Voice::render (float* out, int n, const SynthParams& p, const LofiTables& tables)
{
    amp.set (p.attack, p.decay, p.sustain, p.release, fs);
    fenv.set (p.fAttack, p.fDecay, p.fSustain, p.fRelease, fs);

    const float glideCoef = p.glide <= 0.0005f ? 1.0f : 1.0f - std::exp (-1.0f / (p.glide * fs));
    const float shape = clampv (p.oscAShape, 0.0f, 1.0f);
    const auto& tbl = tables.t[(size_t) clampv (p.oscBWave, 0, LofiTables::numWaves - 1)];
    const float levels = std::exp2 (clampv (p.oscBBits, 2.0f, 8.0f)) * 0.5f;
    const float mixB = clampv (p.oscMix, 0.0f, 1.0f), mixA = 1.0f - mixB;
    const float res = clampv (p.resonance, 0.0f, 1.0f);
    const float q = 0.55f + res * res * 9.0f;
    const float drive = 1.0f + p.drive * 5.0f;
    const float velAmp = 0.35f + 0.65f * vel;

    for (int i = 0; i < n; ++i)
    {
        curMidi += (tgtMidi - curMidi) * glideCoef;

        // slow analog drift per voice
        if (--driftCount <= 0) { driftCount = 2048; driftTgt = nz.next() * p.detune * 9.0f; }
        drift += (driftTgt - drift) * 0.0006f;

        const float hzA = midiToHz (curMidi + drift * 0.01f + p.mutPitch);
        const float dtA = hzA / fs;
        // --- OSC A: morph sine > tri > saw > square, with a detuned twin for width
        auto oscA = [&] (double& ph, float dt) -> float
        {
            const float t = (float) ph;
            float y;
            if (shape < 0.333f)
            {
                const float sine = std::sin (kTwoPi * t);
                const float tri  = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
                const float m = shape / 0.333f;
                y = sine + (tri - sine) * m;
            }
            else if (shape < 0.666f)
            {
                const float tri = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
                float saw = 2.0f * t - 1.0f; saw -= polyBlep (t, dt);
                const float m = (shape - 0.333f) / 0.333f;
                y = tri + (saw - tri) * m;
            }
            else
            {
                float saw = 2.0f * t - 1.0f; saw -= polyBlep (t, dt);
                float sq = t < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep (t, dt);
                float t2 = t + 0.5f; if (t2 >= 1.0f) t2 -= 1.0f;
                sq -= polyBlep (t2, dt);
                const float m = (shape - 0.666f) / 0.334f;
                y = saw + (sq - saw) * m;
            }
            ph += dt; if (ph >= 1.0) ph -= 1.0;
            return y;
        };
        const float spread = 1.0f + p.detune * 0.006f;
        float a = oscA (phA, dtA);
        if (p.detune > 0.02f) a = (a + 0.8f * oscA (phA2, dtA * spread)) / 1.8f;

        // --- OSC B: 8-bit wavetable, no interpolation (that's the point), bit crunch
        const float hzB = midiToHz (curMidi + p.oscBSemi - drift * 0.007f + p.mutPitch);
        phB += hzB / fs; if (phB >= 1.0) phB -= 1.0;
        float b = tbl[(size_t) ((int) (phB * LofiTables::len) & (LofiTables::len - 1))];
        b = std::round (b * levels) / levels;

        float x = a * mixA + b * mixB * 0.8f;
        if (p.noise > 0.001f) x += nz.next() * p.noise * 0.5f;

        // --- filter (update coefficients every 8 samples)
        const float fe = fenv.next();
        if ((filterTick++ & 7) == 0)
        {
            const float octs = p.envAmt * 6.0f * fe + p.keyTrack * (curMidi - 60.0f) / 12.0f + p.mutCutOct;
            const float hz = clampv (p.cutoff * std::exp2 (octs), 20.0f, fs * 0.45f);
            f1.set (hz, q, fs); f2.set (hz, 0.6f + res * 1.2f, fs);
        }
        x = softClip (x * drive) / std::sqrt (drive);
        x = f1.lowpass (x);
        x = f2.lowpass (softClip (x * 1.2f));

        out[i] += dc.process (x * amp.next() * velAmp * 0.35f);
    }
    if (! amp.active()) note = held ? note : -1;
}

// =============================================================================
//  Tape chorus / vibrato: two modulated delay taps + slow wow + random flutter
// =============================================================================
void TapeChorus::prepare (float sampleRate)
{
    fs = sampleRate;
    const int sz = (int) (fs * 0.06f) + 4;
    bufL.assign ((size_t) sz, 0.0f); bufR.assign ((size_t) sz, 0.0f); w = 0;
    flutLp.set (12.0f, fs);
}

float TapeChorus::read (const std::vector<float>& b, float d) const
{
    const int sz = (int) b.size();
    float rp = (float) w - d; while (rp < 0.0f) rp += (float) sz;
    const int i0 = (int) rp; const float fr = rp - (float) i0;
    const int i1 = (i0 + 1) % sz;
    return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
}

void TapeChorus::process (float* L, float* R, int n, const SynthParams& p)
{
    if (p.tapeMix <= 0.001f && p.tapeWow <= 0.001f) return;
    const int sz = (int) bufL.size();
    const float rate = 0.15f + p.tapeRate * 4.5f;
    const float depthMs = 0.4f + p.tapeDepth * 6.0f;
    const float vib = p.tapeMix >= 0.98f ? 1.0f : 0.0f;   // full mix = pure vibrato
    for (int i = 0; i < n; ++i)
    {
        bufL[(size_t) w] = L[i]; bufR[(size_t) w] = R[i];
        lfo += rate / fs; if (lfo >= 1.0) lfo -= 1.0;
        wow += 0.55 / fs; if (wow >= 1.0) wow -= 1.0;
        flut = flutLp.process (nz.next());
        const float w0 = (float) std::sin (kTwoPi * wow) * p.tapeWow * 1.6f + flut * p.tapeWow * 2.5f;
        const float mL = (float) std::sin (kTwoPi * lfo);
        const float mR = (float) std::sin (kTwoPi * lfo + 1.7);
        const float base = 7.0f;
        const float dL = (base + depthMs * (0.5f + 0.5f * mL) + w0) * fs * 0.001f;
        const float dR = (base + depthMs * (0.5f + 0.5f * mR) + w0) * fs * 0.001f;
        const float wl = read (bufL, clampv (dL, 1.0f, (float) sz - 2.0f));
        const float wr = read (bufR, clampv (dR, 1.0f, (float) sz - 2.0f));
        const float mix = vib > 0.5f ? 1.0f : p.tapeMix * 0.6f;
        const float dry = vib > 0.5f ? 0.0f : 1.0f - p.tapeMix * 0.25f;
        L[i] = L[i] * dry + wl * mix;
        R[i] = R[i] * dry + wr * mix;
        if (++w >= sz) w = 0;
    }
}

// =============================================================================
//  Bitcrusher / sample-rate reducer
// =============================================================================
void Crusher::process (float* L, float* R, int n, const SynthParams& p)
{
    if (p.crushMix <= 0.001f) return;
    const float levels = std::exp2 (clampv (p.crushBits, 2.0f, 16.0f) - 1.0f);
    const float step = 1.0f / clampv (p.crushDown, 1.0f, 64.0f);
    for (int i = 0; i < n; ++i)
    {
        phase += step;
        if (phase >= 1.0f) { phase -= 1.0f; holdL = L[i]; holdR = R[i]; }
        const float cl = std::round (holdL * levels) / levels;
        const float cr = std::round (holdR * levels) / levels;
        L[i] += (cl - L[i]) * p.crushMix;
        R[i] += (cr - R[i]) * p.crushMix;
    }
}

// =============================================================================
//  Analog (BBD-flavoured) delay: darkening, saturating, ping-pong feedback
// =============================================================================
void AnalogDelay::prepare (float sampleRate)
{
    fs = sampleRate;
    const int sz = (int) (fs * 2.1f);
    bufL.assign ((size_t) sz, 0.0f); bufR.assign ((size_t) sz, 0.0f); w = 0;
    time.reset (fs, 0.25, 0.375f * fs);
    hpL.set (120.0f, 0.7f, fs); hpR.set (120.0f, 0.7f, fs);
}

void AnalogDelay::process (float* L, float* R, int n, const SynthParams& p)
{
    if (p.delayMix <= 0.001f) return;
    const int sz = (int) bufL.size();
    time.set (clampv (p.delayTime, 0.02f, 2.0f) * fs);
    const float toneHz = 900.0f * std::pow (12.0f, p.delayTone);
    lpL.set (toneHz, fs); lpR.set (toneHz, fs);
    const float fb = clampv (p.delayFb, 0.0f, 0.95f);
    for (int i = 0; i < n; ++i)
    {
        mod += 0.35 / fs; if (mod >= 1.0) mod -= 1.0;
        const float d = clampv (time.next() + (float) std::sin (kTwoPi * mod) * fs * 0.0012f, 4.0f, (float) sz - 4.0f);
        float rp = (float) w - d; while (rp < 0.0f) rp += (float) sz;
        const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % sz;
        const float yl = bufL[(size_t) i0] + (bufL[(size_t) i1] - bufL[(size_t) i0]) * fr;
        const float yr = bufR[(size_t) i0] + (bufR[(size_t) i1] - bufR[(size_t) i0]) * fr;
        float a, b2, c;
        hpL.process (lpL.process (yl), a, b2, c); const float fl = c;
        hpR.process (lpR.process (yr), a, b2, c); const float fr2 = c;
        // ping-pong: left feeds right, right feeds left
        bufL[(size_t) w] = softClip (L[i] + fr2 * fb * 1.05f);
        bufR[(size_t) w] = softClip (R[i] * 0.3f + fl * fb * 1.05f);
        L[i] += yl * p.delayMix;
        R[i] += yr * p.delayMix;
        if (++w >= sz) w = 0;
    }
}

// =============================================================================
//  Space reverb: pre-delay > 2 diffusers > 8-line FDN (Householder), damped
// =============================================================================
void SpaceReverb::prepare (float sampleRate)
{
    fs = sampleRate;
    static const int base[N] = { 1433, 1601, 1867, 2053, 2251, 2399, 2617, 2797 };
    const float sc = fs / 44100.0f * 1.6f;
    for (int i = 0; i < N; ++i)
    {
        len[(size_t) i] = (int) (base[i] * sc);
        lines[(size_t) i].assign ((size_t) len[(size_t) i], 0.0f); pos[(size_t) i] = 0; damp[(size_t) i] = 0.0f;
    }
    pre.assign ((size_t) (fs * 0.05f), 0.0f); prePos = 0;
    shimmerShift.prepare (fs); shimmerLp.set (5000.0f, fs); shimmerFb = 0.0f;
    const int apl[2] = { (int) (fs * 0.0047f), (int) (fs * 0.0113f) };
    for (int k = 0; k < 2; ++k) { ap[(size_t) k].assign ((size_t) apl[k], 0.0f); apPos[(size_t) k] = 0; }
}

void SpaceReverb::process (float* L, float* R, int n, const SynthParams& p)
{
    if (p.spaceMix <= 0.001f && p.shimmer <= 0.001f) return;
    const float size = clampv (p.spaceSize, 0.0f, 1.0f);
    const float g = 0.70f + 0.29f * size;                       // feedback
    const float dmp = 0.15f + 0.8f * clampv (p.spaceDamp, 0.0f, 1.0f);
    const int preLen = (int) pre.size();
    for (int i = 0; i < n; ++i)
    {
        // pre-delay
        float in = (L[i] + R[i]) * 0.5f + shimmerFb * p.shimmer * 0.85f;
        const float pd = pre[(size_t) prePos]; pre[(size_t) prePos] = in; if (++prePos >= preLen) prePos = 0;
        in = pd;
        // two allpass diffusers
        for (int k = 0; k < 2; ++k)
        {
            auto& b = ap[(size_t) k]; int& ps = apPos[(size_t) k];
            const float bv = b[(size_t) ps];
            const float y = -0.6f * in + bv;
            b[(size_t) ps] = in + 0.6f * y;
            if (++ps >= (int) b.size()) ps = 0;
            in = y;
        }
        // FDN
        std::array<float, N> o {};
        float sum = 0.0f;
        for (int k = 0; k < N; ++k)
        {
            float v = lines[(size_t) k][(size_t) pos[(size_t) k]];
            damp[(size_t) k] = v + dmp * (damp[(size_t) k] - v);
            o[(size_t) k] = damp[(size_t) k];
            sum += o[(size_t) k];
        }
        const float hh = sum * (2.0f / N);
        for (int k = 0; k < N; ++k)
        {
            const float fbv = (o[(size_t) k] - hh) * g;
            lines[(size_t) k][(size_t) pos[(size_t) k]] = in * (k & 1 ? -0.35f : 0.35f) + fbv;
            if (++pos[(size_t) k] >= len[(size_t) k]) pos[(size_t) k] = 0;
        }
        const float wl = o[0] + o[2] + o[4] + o[6];
        const float wr = o[1] + o[3] + o[5] + o[7];
        if (p.shimmer > 0.001f) shimmerFb = softClip (shimmerLp.process (shimmerShift.process ((wl + wr) * 0.5f)));
        const float wet = 0.35f * std::max (p.spaceMix, p.shimmer * 0.7f);
        L[i] += wl * wet;
        R[i] += wr * wet;
    }
}

// =============================================================================
//  OmniSynth
// =============================================================================
void OmniSynth::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate;
    for (auto& v : voices) v.prepare ((float) fs);
    mono.assign ((size_t) std::max (1, maxBlock), 0.0f);
    tape.prepare ((float) fs); delay.prepare ((float) fs); space.prepare ((float) fs);
    warp.prepare ((float) fs); orbit.prepare ((float) fs);
    out.reset (fs, 0.03, dbToGain (params.outputDb));
}

void OmniSynth::noteOn (int note, float velocity)
{
    if (logCount < (int) noteLog.size()) noteLog[(size_t) logCount++] = { clock, note, velocity, true };
    // same note still sounding -> retrigger that voice
    Voice* v = nullptr;
    for (auto& vc : voices) if (vc.note == note && vc.active()) { v = &vc; break; }
    // glide: mono-ish legato from the newest held voice when glide is on
    const bool legato = params.glide > 0.0005f;
    if (! v) for (auto& vc : voices) if (! vc.active()) { v = &vc; break; }
    if (! v)
    {   // steal: quietest released voice, else oldest
        float q = 1e9f;
        for (auto& vc : voices) if (! vc.held && vc.level() < q) { q = vc.level(); v = &vc; }
        if (! v) { uint32_t oldest = UINT32_MAX; for (auto& vc : voices) if (vc.age < oldest) { oldest = vc.age; v = &vc; } }
    }
    v->start (note, clampv (velocity, 0.0f, 1.0f), params, legato, ++order);
}

void OmniSynth::noteOff (int note)
{
    bool held = false;
    for (auto& vc : voices) if (vc.note == note && vc.held) held = true;
    if (held && logCount < (int) noteLog.size()) noteLog[(size_t) logCount++] = { clock, note, 0.0f, false };
    for (auto& vc : voices) if (vc.note == note && vc.held) vc.stop();
}

void OmniSynth::allNotesOff()
{
    for (auto& vc : voices)
        if (vc.held && vc.note >= 0 && logCount < (int) noteLog.size()) noteLog[(size_t) logCount++] = { clock, vc.note, 0.0f, false };
    for (auto& vc : voices) vc.stop();
}

void OmniSynth::render (float* L, float* R, int n)
{
    if ((int) mono.size() < n) mono.assign ((size_t) n, 0.0f);
    std::fill (mono.begin(), mono.begin() + n, 0.0f);

    // MUTANT: random sample & hold, smoothed, on filter cutoff and pitch
    if (params.mutAmt > 0.001f)
    {
        mutPhase += n * (0.3 + params.mutRate * 12.0) / fs;
        if (mutPhase >= 1.0) { mutPhase -= std::floor (mutPhase); mutA = mutNoise.next(); mutB = mutNoise.next(); }
        const float k = std::min (1.0f, (float) n / (float) (fs * 0.02));
        mutSmA += (mutA - mutSmA) * k; mutSmB += (mutB - mutSmB) * k;
        params.mutCutOct = mutSmA * params.mutAmt * 3.0f;
        params.mutPitch  = mutSmB * params.mutAmt * params.mutAmt * 1.2f;
    }
    else { params.mutCutOct = 0.0f; params.mutPitch = 0.0f; }

    for (auto& v : voices) if (v.active()) v.render (mono.data(), n, params, tables);

    for (int i = 0; i < n; ++i) { L[i] = mono[(size_t) i]; R[i] = mono[(size_t) i]; }

    tape.process (L, R, n, params);
    crush.process (L, R, n, params);
    warp.process (L, R, n, params);
    orbit.process (L, R, n, params);
    delay.process (L, R, n, params);
    space.process (L, R, n, params);

    out.set (dbToGain (params.outputDb));
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = out.next();
        L[i] = softClip (L[i] * g * 1.4f) / 1.4f;
        R[i] = softClip (R[i] * g * 1.4f) / 1.4f;
        pk = std::max (pk, std::fabs (L[i]));
        if ((i & 3) == 0) { scope[(size_t) scopePos] = (L[i] + R[i]) * 0.5f; scopePos = (scopePos + 1) & 511; }
    }
    peak = std::max (pk, peak * 0.9f);
    clock += n;
}
} // namespace oc
