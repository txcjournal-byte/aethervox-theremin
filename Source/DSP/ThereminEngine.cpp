#include "ThereminEngine.h"

namespace av
{
void ThereminEngine::prepare (double sampleRate)
{
    fs = sampleRate;
    for (size_t v = 0; v < voices.size(); ++v)
    {
        auto& vc = voices[v];
        for (auto& o : vc.osc) o.prepare (fs);
        vc.dc.prepare (fs, 5.0f);
        const uint32_t s = 11u + (uint32_t) v * 97u;
        vc.nJitter = SmoothNoise (s);      vc.nJitter.setRate (7.0f,  fs);   // fast hand tremble
        vc.nWander = SmoothNoise (s + 1);  vc.nWander.setRate (0.7f,  fs);   // vibrato-rate wander
        vc.nDepth  = SmoothNoise (s + 2);  vc.nDepth .setRate (0.4f,  fs);   // vibrato-depth breathing
        vc.nDrift  = SmoothNoise (s + 3);  vc.nDrift .setRate (0.08f, fs);   // thermal RF drift
        vc.nAmp    = SmoothNoise (s + 4);  vc.nAmp   .setRate (5.0f,  fs);   // volume-hand micro-movement
        vc.vibPhase = 0.137 * (double) v;                                    // voices don't vibrate in lock-step
    }
    saturator.prepare (fs);
    rageDc.prepare (fs, 12.0f);
    formant.prepare (fs);
    delay.prepare (fs);
    reverb.prepare (fs);

    morph.reset      (fs, 0.03, params.morph);
    brightness.reset (fs, 0.03, params.brightness);
    outGain.reset    (fs, 0.03, dbToGain (params.outputDb));
    layerAmt.reset   (fs, 0.05, params.layer);
    subAmt.reset     (fs, 0.05, params.sub);
    vibOnsetCoef = onePoleCoef (450.0f, fs);   // vibrato blooms in after attack

    setParameters (params);
    reset();
}

void ThereminEngine::reset()
{
    for (auto& vc : voices)
    {
        for (auto& o : vc.osc) o.reset();
        vc.dc.reset();
        vc.env = 0.0f; vc.vibEnv = 0.0f; vc.targetAmp = 0.0f;
        vc.curMidi = vc.targetMidi;
    }
    saturator.reset(); formant.reset(); delay.reset(); reverb.reset(); toneFilter.reset();
    polyGain = 1.0f;
}

void ThereminEngine::setParameters (const EngineParams& p)
{
    params = p;

    glideCoef   = onePoleCoef (p.glideMs,   fs);
    attackCoef  = onePoleCoef (p.attackMs,  fs);
    releaseCoef = onePoleCoef (p.releaseMs, fs);

    quantizer.setScale (p.scale, p.root);
    for (auto& vc : voices)
    {
        vc.osc[0].setCoupling (p.coupling);
        vc.osc[1].setCoupling (p.coupling * 0.8f);
        vc.osc[2].setCoupling (p.coupling * 0.8f);
        vc.osc[3].setCoupling (p.coupling * 0.5f);
        vc.targetMidi = quantizer.process (vc.targetMidiRaw, p.quantAmount);
    }
    saturator.setDrive (clampv (p.drive * 1.6f, 0.0f, 1.0f));   // tube warmth comes in first
    rageAmt = clampv (p.drive, 0.0f, 1.0f);
    formant.setPreset (p.formant);
    formant.setMix (p.formantMix);
    delay.setParameters (p.delayMs, p.delayFb, p.delayMix);
    reverb.setParameters (p.reverbSize, p.reverbMix);

    morph.setTarget (p.morph);
    brightness.setTarget (p.brightness);
    outGain.setTarget (dbToGain (p.outputDb));
    layerAmt.setTarget (p.layer);
    subAmt.setTarget (p.sub);
}

void ThereminEngine::setVoiceTarget (int v, float midiNote, float amplitude)
{
    auto& vc = voices[(size_t) clampv (v, 0, maxVoices - 1)];
    vc.targetMidiRaw = midiNote;
    vc.targetMidi    = quantizer.process (midiNote, params.quantAmount);
    vc.targetAmp     = clampv (amplitude, 0.0f, 1.0f);
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
    const float hum       = params.humanize;
    const float vibDepth  = params.vibDepth;
    const float vibRate   = params.vibRate;
    const float tremDepth = params.tremolo;
    const float driftAmt  = params.drift;

    // count sounding voices once per block -> gentle level compensation for chords
    int sounding = 0;
    for (const auto& vc : voices) if (! vc.idle()) ++sounding;
    const float targetPolyGain = 1.0f / std::sqrt ((float) std::max (1, sounding));

    auto shape = [] (const HeterodyneOscillator::Sample& s, float m)
    {
        const float tri = triangle (s.phase, s.increment);
        if (m < 0.5f) return s.value + (tri - s.value) * (m * 2.0f);
        return tri + (sawtooth (s.phase, s.increment) - tri) * ((m - 0.5f) * 2.0f);
    };

    for (int i = 0; i < n; ++i)
    {
        const float m  = morph.next();
        const float la = layerAmt.next();
        const float sa = subAmt.next();
        polyGain += (targetPolyGain - polyGain) * 0.0005f;

        float sum = 0.0f, loudest = 0.0f, loudMidi = lastSoundMidi;
        for (auto& vc : voices)
        {
            if (vc.idle()) continue;

            // --- pitch glide (log domain). A voice starting from silence glides in from the
            //     last note that sounded (portamento), unless nothing sounded recently.
            if (glideCoef >= 1.0f)
                vc.curMidi = vc.targetMidi;
            else if (vc.env < 1.0e-4f)
                vc.curMidi = (silentSamples < (int) (fs * 0.6)) ? lastSoundMidi : vc.targetMidi;
            else
                vc.curMidi += (vc.targetMidi - vc.curMidi) * glideCoef;

            // --- antenna response envelope
            vc.env += (vc.targetAmp - vc.env) * (vc.targetAmp > vc.env ? attackCoef : releaseCoef);

            // --- human hand: vibrato + micro-jitter + drift
            const float wander = vc.nWander.next(), depthN = vc.nDepth.next(), jit = vc.nJitter.next();
            const float ampN = vc.nAmp.next(), driftN = vc.nDrift.next();

            const float gate = vc.env > 0.02f ? 1.0f : 0.0f;
            vc.vibEnv += (gate - vc.vibEnv) * vibOnsetCoef;

            vc.vibPhase += vibRate * (1.0f + 0.10f * hum * wander) / fs;
            vc.vibPhase -= std::floor (vc.vibPhase);
            const float lfo   = (float) std::sin (kTwoPi * vc.vibPhase);
            const float depth = vibDepth * vc.vibEnv * (1.0f + 0.35f * hum * depthN);
            const float cents = depth * lfo + hum * 9.0f * jit;

            const float midi = vc.curMidi + cents * 0.01f;
            vc.hz = clampv (midiToHz (midi), 20.0f, 4200.0f);   // up to C8

            const double drift = driftAmt * (2.5 * driftN + 0.4 * jit);
            vc.osc[0].setDriftHz (drift);
            float v = shape (vc.osc[0].process (vc.hz), m);

            // --- layering: two detuned unison copies (chorus of theremins)
            if (la > 0.001f)
            {
                const float det = std::exp2 ((4.0f + 14.0f * la) / 1200.0f);
                vc.osc[1].setDriftHz (-drift * 0.7);
                vc.osc[2].setDriftHz (drift * 0.6);
                const float l1 = shape (vc.osc[1].process (vc.hz * det), m);
                const float l2 = shape (vc.osc[2].process (vc.hz / det), m);
                v = (v + 0.75f * la * (l1 + l2)) / (1.0f + 0.9f * la);
            }
            // --- sub octave (pure heterodyne sine)
            if (sa > 0.001f && vc.hz * 0.5f >= 20.0f)
                v += 0.7f * sa * vc.osc[3].process (vc.hz * 0.5f).value;

            // --- tremolo (same hand -> phase related to vibrato) + amplitude unsteadiness
            const float tremLfo = 0.5f + 0.5f * (float) std::sin (kTwoPi * vc.vibPhase + 1.1);
            const float trem = 1.0f - tremDepth * 0.45f * tremLfo * (0.3f + 0.7f * vc.vibEnv)
                                    - hum * 0.05f * (0.5f + 0.5f * ampN);

            sum += vc.dc.process (v * vc.env * trem * 0.5f);
            if (vc.env > 0.02f && vc.env >= loudest) { loudest = vc.env; loudMidi = vc.curMidi; }
        }
        if (loudest > 0.0f) { lastSoundMidi = loudMidi; silentSamples = 0; }
        else if (silentSamples < (1 << 30)) ++silentSamples;
        dest[i] = sum * polyGain;
    }
}

void ThereminEngine::processPost (float* mono, float* L, float* R, int n)
{
    formant.setPitch (getCurrentHz());
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
    }

    // --- RAGE (Drive knob): tube warmth -> fuzz growl -> grit/crackle -> broken bitcrush.
    //     Loudness is matched to the clean signal, so the knob changes character, not volume.
    float pre[512];
    for (int off = 0; off < n; off += 512)
    {
        const int m = std::min (512, n - off);
        std::copy (mono + off, mono + off + m, pre);
        saturator.process (mono + off, m);

        const float powCoef = 1.0f - std::exp (-1.0f / (0.03f * (float) fs));
        for (int j = 0; j < m; ++j)
        {
            const int i = off + j;
            rageSm += (rageAmt - rageSm) * 0.002f;
            const float d = rageSm;
            float x = mono[i];

            if (d > 0.06f)
            {
                const float f = clampv ((d - 0.06f) / 0.50f, 0.0f, 1.0f);
                // sub-octave rasp: flip polarity every other cycle -> growl an octave down
                if ((lastRageX <= 0.0f) && (x > 0.0f)) subFlip = -subFlip;
                lastRageX = x;
                // motor growl LFO ~ 30..55 Hz
                growlPhase += (30.0f + 25.0f * f) / (float) fs;
                growlPhase -= std::floor (growlPhase);
                const float growl = 0.5f + 0.5f * std::sin (kTwoPi * growlPhase);
                // hard fuzz
                float fz = std::tanh (x * (3.0f + 60.0f * f));
                fz = clampv (fz * 1.25f, -0.75f, 1.0f);                     // asymmetric -> buzz
                // rasp: blend in the sub-octave square
                fz = fz * (1.0f - 0.45f * f) + 0.45f * f * fz * subFlip;
                // grit: crackle riding on the waveform
                rageSeed = rageSeed * 1664525u + 1013904223u;
                const float noise = ((float) (rageSeed >> 9) / 8388608.0f) - 1.0f;
                fz += noise * (0.25f + 0.75f * std::fabs (fz)) * 0.9f * f;
                // motor amplitude growl
                fz *= 1.0f - 0.7f * f * growl;
                x = x * (1.0f - f) + fz * f;
            }
            if (d > 0.5f)
            {
                const float c = clampv ((d - 0.5f) / 0.5f, 0.0f, 1.0f);
                crushPhase += 1.0f / (1.0f + 20.0f * c * c);
                if (crushPhase >= 1.0f) { crushPhase -= 1.0f; crushHold = x; }
                const float levels = 64.0f * std::pow (0.06f, c);
                const float crushed = std::round (crushHold * levels) / levels;
                x = x * (1.0f - c) + c * crushed;
            }
            x = rageDc.process (x);

            // loudness match to the clean (pre-drive) signal
            rageInPow  += (pre[j] * pre[j] - rageInPow)  * powCoef;
            rageOutPow += (x * x        - rageOutPow) * powCoef;
            const float match = std::sqrt ((rageInPow + 1.0e-9f) / (rageOutPow + 1.0e-9f));
            x *= clampv (match, 0.05f, 4.0f) * (1.0f + 0.6f * d);   // up to ~+4 dB at full rage
            mono[i] = x;
            L[i] = R[i] = x;
        }
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
