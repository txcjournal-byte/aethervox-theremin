#pragma once
// =============================================================================
//  PitchQuantizer - "Scale Snap / Auto-Tune" for a continuous-pitch instrument.
//  amount = 0   -> free theremin pitch
//  amount = 1   -> hard snap to the nearest scale degree
//  in between   -> partial pull toward the scale (soft correction)
//  The glide stage after the quantizer turns hard steps into musical slides.
// =============================================================================
#include <array>
#include <cstdint>
#include <cmath>

namespace av
{
class PitchQuantizer
{
public:
    enum Scale { Chromatic = 0, Major, Minor, PentatonicMajor, PentatonicMinor, NumScales };

    void setScale (int scaleIndex, int rootPitchClass)
    {
        scale = (scaleIndex >= 0 && scaleIndex < NumScales) ? scaleIndex : 0;
        root  = ((rootPitchClass % 12) + 12) % 12;
    }

    float process (float midi, float amount) const
    {
        if (amount <= 0.0f) return midi;
        const float snapped = nearest (midi);
        return midi + (snapped - midi) * std::min (amount, 1.0f);
    }

    bool isInScale (int midiNote) const
    {
        const int pc = ((midiNote - root) % 12 + 12) % 12;
        return masks[(size_t) scale][(size_t) pc] != 0;
    }

private:
    float nearest (float midi) const
    {
        const int base = (int) std::lround (midi);
        float best = (float) base, bestDist = 1.0e9f;
        for (int o = -6; o <= 6; ++o)
        {
            const int n = base + o;
            if (! isInScale (n)) continue;
            const float d = std::abs ((float) n - midi);
            if (d < bestDist) { bestDist = d; best = (float) n; }
        }
        return best;
    }

    int scale = 0, root = 0;

    static constexpr std::array<std::array<uint8_t, 12>, NumScales> masks {{
        {{ 1,1,1,1,1,1,1,1,1,1,1,1 }},   // Chromatic
        {{ 1,0,1,0,1,1,0,1,0,1,0,1 }},   // Major
        {{ 1,0,1,1,0,1,0,1,1,0,1,0 }},   // Natural minor
        {{ 1,0,1,0,1,0,0,1,0,1,0,0 }},   // Pentatonic major
        {{ 1,0,0,1,0,1,0,1,0,0,1,0 }}    // Pentatonic minor
    }};
};
} // namespace av
