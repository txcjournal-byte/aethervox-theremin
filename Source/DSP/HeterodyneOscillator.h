#pragma once
// =============================================================================
//  HeterodyneOscillator
//
//  Models the core of a real theremin: two radio-frequency oscillators
//  (one FIXED, one VARIABLE whose frequency is detuned by the hand's
//  capacitance). Their signals are multiplied in a mixer and low-pass
//  filtered, leaving only the audible difference ("beat") frequency.
//
//  Implementation notes
//  --------------------
//  * Both RF oscillators are tracked explicitly in double precision at a
//    virtual RF of 170 kHz. Running them at audio sample-rate would alias
//    massively, so the "ideal mixer + LPF" stage is evaluated analytically:
//    the product of two oscillators with harmonics a_k, b_k leaves, after the
//    low-pass, a sum of  a_k * b_k * cos(k * (phiVar - phiFixed)).
//    -> we only ever *output* the difference phase, never the RF itself.
//
//  * Oscillator pulling (injection locking) - the parasitic coupling between
//    the two RF oscillators - is modelled with the Adler equation:
//        dTheta/dt = 2*pi * (dF - L * sin(Theta))
//    It produces the theremin's characteristic waveform asymmetry at low
//    pitches and makes the beat "lock" to zero when |dF| < L.
//    The audible pitch would drop to sqrt(dF^2 - L^2), so the detune is
//    pre-compensated:  dF = sqrt(f^2 + L^2)  -> tuning stays exact.
//
//  * Thermal RF drift is injected as a few Hz of slow random detune, which
//    (exactly like the real instrument) is most audible on low notes.
// =============================================================================
#include "DspUtils.h"

namespace av
{
class HeterodyneOscillator
{
public:
    struct Sample
    {
        float  value;      // mixer output (heterodyne "sine")
        double phase;      // difference phase, [0,1)
        double increment;  // phase increment this sample (cycles), for BLEP
    };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        nyquistLimit = 0.45 * fs;
        reset();
    }

    void reset()
    {
        phiFixed = phiVariable = lastDiff = 0.0;
    }

    /** 0..1 : amount of RF coupling (pull/locking range + mixer impurity). */
    void setCoupling (float amount)
    {
        coupling = clampv (amount, 0.0f, 1.0f);
        lockHz   = 14.0 * coupling;              // locking range in Hz
        h2       = 0.12 * coupling;              // mixer 2nd-order products
        h3       = 0.05 * coupling;              // mixer 3rd-order products
    }

    /** Slow thermal detune of the variable RF oscillator in Hz. */
    void setDriftHz (double hz) { driftHz = hz; }

    Sample process (double audioHz)
    {
        const double L = lockHz;
        double detune  = std::sqrt (audioHz * audioHz + L * L) + driftHz;
        detune = std::max (0.0, detune);

        // Adler equation integrated with RK2 (midpoint) on the pulled oscillator
        const double theta    = kTwoPi * lastDiff;
        const double k1       = kTwoPi * (detune - L * std::sin (theta)) / fs;
        const double thetaMid = theta + 0.5 * k1;
        const double varHz    = rfHz + detune - L * std::sin (thetaMid);

        // advance both virtual RF oscillators
        phiFixed    += rfHz  / fs;  phiFixed    -= std::floor (phiFixed);
        phiVariable += varHz / fs;  phiVariable -= std::floor (phiVariable);

        // mixer: difference phase
        double diff = phiVariable - phiFixed;
        diff -= std::floor (diff);

        double inc = diff - lastDiff;
        inc -= std::round (inc);                 // unwrap to (-0.5, 0.5]
        lastDiff = diff;

        // ideal mixer + LPF -> sum of difference harmonics (band-limited)
        const double th = kTwoPi * diff;
        double v = std::sin (th), norm = 1.0;
        if (2.0 * audioHz < nyquistLimit) { v += h2 * std::sin (2.0 * th + 0.3); norm += h2; }
        if (3.0 * audioHz < nyquistLimit) { v += h3 * std::sin (3.0 * th);       norm += h3; }

        return { (float) (v / norm), diff, std::max (inc, 1.0e-9) };
    }

    double getFixedPhase()    const { return phiFixed; }
    double getVariablePhase() const { return phiVariable; }
    static constexpr double rfHz = 170000.0;     // virtual RF carrier

private:
    double fs = 48000.0, nyquistLimit = 21600.0;
    double phiFixed = 0.0, phiVariable = 0.0, lastDiff = 0.0;
    double lockHz = 0.0, h2 = 0.0, h3 = 0.0, driftHz = 0.0;
    float  coupling = 0.0f;
};
} // namespace av
