#include "ThereminEngine.h"

namespace av
{
void ThereminEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    osc.prepare (fs);
    saturator.prepare (fs);
    formant.prepare (fs);
    delay.prepare (fs);
    reverb.prepare (fs);
    voiceDc.prepare (fs, 5.0f);

    nJitter.setRate (7.0f,  fs);   // fast hand tremble
    nWander.setRate (0.7f,  fs);   // vibrato-rate wander
    nDepth .setRate (0.4f,  fs);   // vibrato-depth breathing
    nDrift .setRate (0.08f, fs);   // thermal RF drift
    nAmp   .setRate (5.0f,  fs);   // volume-hand micro-movement

    morph.reset      (fs, 0.03, params.morph);
    brightness.reset (fs, 0.03, params.brightness);
    outGain.reset    (fs, 0.03, dbToGain (params.outputDb));
    vibOnsetCoef = onePoleCoef (450.0f, fs);   // vibrato blooms in after attack

    setParameters (params);
    reset();
}

void ThereminEngine::reset()
{
    osc.reset(); saturator.reset(); formant.reset(); delay.reset(); reverb.reset();
    toneFilter.reset(); voiceDc.reset();
    env = 0.0f; vibEnv = 0.0f; vibPhase = 0.0;
    curMidi = targetMidi;
}

void ThereminEngine::setParameters (const EngineParams& p)
{
    params = p;

    glideCoef   = onePoleCoef (p.glideMs,   fs);
    attackCoef  = onePoleCoef (p.attackMs,  fs);
    releaseCoef = onePoleCoef (p.releaseMs, fs);

    quantizer.setScale (p.scale, p.root);
    osc.setCoupling (p.coupling);
    saturator.setDrive (p.drive);
    formant.setPreset (p.formant);
    formant.setMix (p.formantMix);
    delay.setParameters (p.delayMs, p.delayFb, p.delayMix);
    reverb.setParameters (p.reverbSize, p.reverbMix);

    morph.setTarget (p.morph);
    brightness.setTarget (p.brightness);
    outGain.setTarget (dbToGain (p.outputDb));

    targetMidi = quantizer.process (targetMidiRaw, params.quantAmount);
}

void ThereminEngine::setTarget (float midiNote, float amplitude)
{
    targetMidiRaw = midiNote;
    targetMidi    = quantizer.process (midiNote, params.quantAmount);
    targetAmp     = clampv (amplitude, 0.0f, 1.0f);
}

// --- band-limited waveforms, phase aligned so their fundamentals match sin(2*pi*p)
float ThereminEngine::triangle (double p, double dt)
{
    double y = p < 0.25 ? 4.0 * p : (p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
    double t1 = p - 0.25; if (t1 < 0.0) t1 += 1.0;     // peak corner   (slope -8)
    double t2 = p - 0.75; if (t2 < 0.0) t2 += 1.0;     // trough corner (slope +8)
    y += -8.0 * dt * polyBlamp (t1, dt) + 8.0 * dt * polyBlamp (t2, dt);
    return (float) y;
}

float ThereminEngine::sawtooth (double p, double dt)
{
    return (float) ((1.0 - 2.0 * p) + polyBlep (p, dt));
}

void ThereminEngine::renderVoice (float* dest, int n)
{
    const float hum        = params.humanize;
    const float vibDepth   = params.vibDepth;
    const float vibRate    = params.vibRate;
    const float tremDepth  = params.tremolo;
    const float driftAmt   = params.drift;

    for (int i = 0; i < n; ++i)
    {
        // --- pitch glide (in the logarithmic / semitone domain)
        if (env < 1.0e-4f || glideCoef >= 1.0f) curMidi = targetMidi;
        else                                     curMidi += (targetMidi - curMidi) * glideCoef;

        // --- antenna response envelope
        env += (targetAmp - env) * (targetAmp > env ? attackCoef : releaseCoef);

        // --- human hand: vibrato + micro-jitter + drift
        const float wander = nWander.next(), depthN = nDepth.next(), jit = nJitter.next();
        const float ampN = nAmp.next(), driftN = nDrift.next();

        const float gate = env > 0.02f ? 1.0f : 0.0f;
        vibEnv += (gate - vibEnv) * vibOnsetCoef;

        vibPhase += vibRate * (1.0f + 0.10f * hum * wander) / fs;
        vibPhase -= std::floor (vibPhase);
        const float lfo   = (float) std::sin (kTwoPi * vibPhase);
        const float depth = vibDepth * vibEnv * (1.0f + 0.35f * hum * depthN);
        const float cents = depth * lfo + hum * 9.0f * jit;

        currentHz = clampv (midiToHz (curMidi + cents * 0.01f), 20.0f, 4200.0f);   // up to C8

        osc.setDriftHz (driftAmt * (2.5 * driftN + 0.4 * jit));
        const auto s = osc.process (currentHz);

        // --- continuous waveform morph  (0 sine .. 0.5 triangle .. 1 saw)
        const float m   = morph.next();
        const float tri = triangle (s.phase, s.increment);
        float v;
        if (m < 0.5f) v = s.value + (tri - s.value) * (m * 2.0f);
        else          v = tri + (sawtooth (s.phase, s.increment) - tri) * ((m - 0.5f) * 2.0f);

        // --- tremolo (same hand -> phase related to vibrato) + amplitude unsteadiness
        const float tremLfo = 0.5f + 0.5f * (float) std::sin (kTwoPi * vibPhase + 1.1);
        const float trem = 1.0f - tremDepth * 0.45f * tremLfo * (0.3f + 0.7f * vibEnv)
                                - hum * 0.05f * (0.5f + 0.5f * ampN);

        dest[i] = voiceDc.process (v * env * trem * 0.5f);
    }
}

void ThereminEngine::processPost (float* mono, float* L, float* R, int n)
{
    saturator.process (mono, n);

    formant.setPitch (currentHz);
    formant.setShift (timbreMod >= 0.0f ? (timbreMod - 0.5f) * 2.0f : 0.0f);
    formant.process (mono, n);

    const float bOffset = timbreMod >= 0.0f ? (timbreMod - 0.5f) * 0.5f : 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float b = brightness.next();
        if ((i & 15) == 0)
        {
            const float bb = clampv (b + bOffset, 0.0f, 1.0f);
            toneFilter.set (250.0f * std::pow (72.0f, bb), 0.62f, fs);   // 250 Hz .. 18 kHz
        }
        mono[i] = toneFilter.lowpass (mono[i]);
        L[i] = R[i] = mono[i];
    }

    delay.process (L, R, n);
    reverb.process (L, R, n);

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.next();
        L[i] *= g; R[i] *= g;
    }
}
} // namespace av
