#pragma once
// =============================================================================
//  OmniCore 3000 - chord / strum / arp engine (pure C++17)
//
//  Inputs (all sample-timed by the caller):
//    key(note,vel,on)        keyboard keys + MIDI notes
//    chordButton(row,pc,on)  Omnichord-style chord buttons: row 0 Maj, 1 min, 2 7th
//    strum(string,vel)       touch-plate "sonic strings" 0..strings-1
//
//  Modes:
//    NOTES  keys play single notes; chord buttons play a lush chord pad
//    CHORD  every key plays a full chord (type = Chord Type) in its own octave
//    STRUM  chord buttons / keys pick the chord and strum it; the plate plucks it
//    ARP    the current chord runs as a tempo-synced arpeggio
// =============================================================================
#include "OmniSynth.h"
#include <functional>
#include <map>
#include <string>

namespace oc
{
enum class PlayMode { Notes = 0, Chord, Strum, Arp };
enum class ArpPattern { Up = 0, Down, UpDown, Random };

struct ChordSettings
{
    PlayMode mode = PlayMode::Chord;
    int   chordType = 0;     // 0 Maj, 1 min, 2 7, 3 maj7, 4 min7, 5 sus4, 6 add9, 7 dim
    bool  bass = false;      // add a root an octave below the pad
    bool  hold = false;      // chords latch until the next chord
    float strumMs = 28.0f;   // time between strings when auto-strumming
    int   arpRate = 3;       // 0 1/4, 1 1/8, 2 1/8T, 3 1/16, 4 1/16T, 5 1/32
    ArpPattern arpPattern = ArpPattern::UpDown;
    int   arpOctaves = 2;    // 1..3
    float arpGate = 0.55f;   // 0.05..1
    float pluck = 0.35f;     // strum note length, seconds
};

class ChordEngine
{
public:
    static constexpr int numStrings = 13;
    static constexpr int numTypes = 8;

    static const char* typeName (int t)
    {
        static const char* n[numTypes] = { "", "m", "7", "maj7", "m7", "sus4", "add9", "dim" };
        return n[clampv (t, 0, numTypes - 1)];
    }
    static const char* pcName (int pc)
    {
        static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        return n[((pc % 12) + 12) % 12];
    }
    static std::vector<int> intervals (int type)
    {
        switch (type)
        {
            case 1:  return { 0, 3, 7 };
            case 2:  return { 0, 4, 7, 10 };
            case 3:  return { 0, 4, 7, 11 };
            case 4:  return { 0, 3, 7, 10 };
            case 5:  return { 0, 5, 7 };
            case 6:  return { 0, 4, 7, 14 };
            case 7:  return { 0, 3, 6, 9 };
            default: return { 0, 4, 7 };
        }
    }

    void prepare (double sampleRate) { fs = sampleRate; reset(); }
    void reset()
    {
        pending.clear(); padNotes.clear(); keyChords.clear(); heldKeys.clear();
        chordRoot = -1; chordType = 0; arpIdx = 0; arpDir = 1; samplesToStep = 0.0; clock = 0;
        lastStrumString = -1;
    }
    void setSettings (OmniSynth& synth, const ChordSettings& s)
    {
        const bool modeChanged = s.mode != set.mode;
        const bool holdOff = set.hold && ! s.hold;
        const bool bassChanged = set.bass != s.bass;
        set = s;
        if (modeChanged) { releasePad (synth); arpRelease (synth); releaseArpBass (synth); keyChords.clear(); }
        // HOLD switched off with nothing pressed: let the latched chord go
        if (holdOff && buttonsDown == 0 && heldKeys.empty()) releaseChord (synth);
        // BASS switched while a chord rings: add / remove the root right away
        if (bassChanged && chordRoot >= 0 && ! modeChanged)
        {
            if (! padNotes.empty()) playPad (synth, chordRoot, chordType, 60, 0.8f);
            if (set.mode == PlayMode::Arp) updateArpBass (synth, 0.8f);
        }
    }
    const ChordSettings& settings() const { return set; }

    // --- display info -------------------------------------------------------
    int currentRoot() const { return chordRoot; }
    int currentType() const { return chordType; }
    std::string chordName() const
    {
        if (chordRoot < 0) return "--";
        return std::string (pcName (chordRoot)) + typeName (chordType);
    }
    int arpStep() const { return arpIdx; }

    // --- inputs ---------------------------------------------------------------
    void key (OmniSynth& s, int note, float vel, bool on)
    {
        if (set.mode == PlayMode::Notes)
        {
            if (on) s.noteOn (note, vel); else s.noteOff (note);
            return;
        }
        if (on)
        {
            heldKeys.push_back (note);
            selectChord (s, ((note % 12) + 12) % 12, set.chordType, note, vel, true);
        }
        else
        {
            heldKeys.erase (std::remove (heldKeys.begin(), heldKeys.end(), note), heldKeys.end());
            if (set.mode == PlayMode::Chord)
            {
                auto it = keyChords.find (note);
                if (it != keyChords.end()) { for (int n : it->second) s.noteOff (n); keyChords.erase (it); }
                if (heldKeys.empty() && ! set.hold) chordRoot = -1;
            }
            else if (heldKeys.empty() && ! set.hold && buttonsDown == 0) releaseChord (s);
            else if (! heldKeys.empty())
            {
                const int k = heldKeys.back();
                selectChord (s, ((k % 12) + 12) % 12, set.chordType, k, vel, false);
            }
        }
    }

    void chordButton (OmniSynth& s, int row, int pc, bool on, float vel = 0.85f)
    {
        const int type = row == 1 ? 1 : (row == 2 ? 2 : 0);
        // with HOLD on, pressing the latched chord again switches it off
        if (on && set.hold && buttonsDown == 0 && heldKeys.empty() && chordRoot == pc && chordType == type
            && set.mode != PlayMode::Strum)
        {
            releaseChord (s);
            return;
        }
        if (on)
        {
            ++buttonsDown;
            selectChord (s, pc, type, -1, vel, true);
        }
        else
        {
            buttonsDown = std::max (0, buttonsDown - 1);
            // last chord key let go (in whatever order they were pressed): stop, unless HOLD
            if (buttonsDown == 0 && heldKeys.empty() && ! set.hold)
                releaseChord (s);
        }
    }

    void strum (OmniSynth& s, int string, float vel)
    {
        const int note = stringNoteOrLast (string);
        s.noteOn (note, vel);
        schedule (clock + (int64_t) (set.pluck * fs), note, false, vel);
        lastStrumString = string;
    }

    /** A whole swipe across the plate (space bar): every string, strumMs apart. */
    void strumSweep (OmniSynth& s, bool up, float vel)
    {
        for (int k = 0; k < numStrings; ++k)
        {
            const int str = up ? k : numStrings - 1 - k;
            const int note = stringNoteOrLast (str);
            const int64_t t = clock + (int64_t) (k * std::max (8.0f, set.strumMs) * 0.001 * fs);
            schedule (t, note, true, vel * (0.8f + 0.2f * (float) k / numStrings));
            schedule (t + (int64_t) (set.pluck * fs * 1.4), note, false, 0.0f);
        }
    }

    /** Strings follow the held chord, or the last chord played (C major before the first one). */
    int stringNoteOrLast (int k)
    {
        if (chordRoot >= 0) return stringNote (k);
        const int r = chordRoot, t = chordType;
        chordRoot = lastRoot; chordType = lastType;
        const int n = stringNote (k);
        chordRoot = r; chordType = t;
        return n;
    }

    /** Note the k-th sonic string plays for the current chord (rising chord tones from G3). */
    int stringNote (int k) const
    {
        if (chordRoot < 0) return 60;
        std::vector<int> pcs;
        for (int iv : intervals (chordType)) pcs.push_back ((chordRoot + iv) % 12);
        std::vector<int> notes;
        for (int n = 50; (int) notes.size() < numStrings && n < 110; ++n)
            if (std::find (pcs.begin(), pcs.end(), n % 12) != pcs.end()) notes.push_back (n);
        return notes[(size_t) clampv (k, 0, (int) notes.size() - 1)];
    }

    // --- time -----------------------------------------------------------------
    /** Renders n samples, firing scheduled strums / arp steps / note-offs sample-accurately.
        bpm: host tempo; ppq: host position at block start (or <0 when the host is stopped). */
    void render (OmniSynth& s, float* L, float* R, int n, double bpm, double ppq)
    {
        int done = 0;
        const double spb = 60.0 / std::max (20.0, bpm) * fs;            // samples per beat
        const double stepBeats = rateBeats (set.arpRate);
        const double stepSamples = spb * stepBeats;

        // host-synced arp: align the next step to the bar grid
        if (set.mode == PlayMode::Arp && ppq >= 0.0 && chordRoot >= 0)
        {
            const double steps = ppq / stepBeats;
            double frac = std::ceil (steps - 1.0e-9) - steps;
            samplesToStep = frac * stepSamples;
            // the step on this grid line already fired at the end of the previous block
            if (samplesToStep < 1.0 && lastArpClock == clock) samplesToStep += stepSamples;
        }

        while (done < n)
        {
            int seg = n - done;
            // next pending event
            for (const auto& e : pending) seg = (int) std::min<int64_t> (seg, std::max<int64_t> (0, e.at - clock));
            // next arp step
            const bool arpOn = set.mode == PlayMode::Arp && chordRoot >= 0;
            if (arpOn) seg = std::min (seg, std::max (0, (int) std::ceil (samplesToStep)));

            if (seg > 0)
            {
                s.render (L + done, R + done, seg);
                done += seg; clock += seg;
                if (arpOn) samplesToStep -= seg;
            }
            // fire due events
            for (size_t i = 0; i < pending.size();)
            {
                if (pending[i].at <= clock)
                {
                    const auto e = pending[i];
                    pending.erase (pending.begin() + (long) i);
                    if (e.on) s.noteOn (e.note, e.vel); else s.noteOff (e.note);
                }
                else ++i;
            }
            if (arpOn && samplesToStep <= 0.0)
            {
                arpFire (s, stepSamples);
                lastArpClock = clock;
                samplesToStep += stepSamples;
            }
        }
    }

    void allOff (OmniSynth& s)
    {
        pending.clear(); keyChords.clear(); padNotes.clear(); heldKeys.clear(); buttonsDown = 0;
        chordRoot = -1; arpBassNote = -1; s.allNotesOff();
    }

private:
    struct Ev { int64_t at; int note; bool on; float vel; };

    double fs = 48000.0;
    ChordSettings set;
    int64_t clock = 0;
    std::vector<Ev> pending;
    std::vector<int> padNotes, heldKeys, arpNotes;
    std::map<int, std::vector<int>> keyChords;
    int chordRoot = -1, chordType = 0, buttonsDown = 0, lastStrumString = -1;
    int arpIdx = 0, arpDir = 1; double samplesToStep = 0.0; uint32_t rnd = 12345;
    int lastRoot = 0, lastType = 0, arpBassNote = -1; int64_t lastArpClock = -1;

    void updateArpBass (OmniSynth& s, float vel)
    {
        const int want = (set.bass && chordRoot >= 0) ? 36 + chordRoot : -1;
        if (want == arpBassNote) return;
        if (arpBassNote >= 0) s.noteOff (arpBassNote);
        arpBassNote = want;
        if (want >= 0) s.noteOn (want, juce_bass_vel (vel) + 0.4f);
    }
    void releaseArpBass (OmniSynth& s) { if (arpBassNote >= 0) s.noteOff (arpBassNote); arpBassNote = -1; }
    static float juce_bass_vel (float v) { return std::max (0.6f, v); }

    static double rateBeats (int r)
    {
        static const double b[6] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return b[clampv (r, 0, 5)];
    }

    void schedule (int64_t at, int note, bool on, float vel) { pending.push_back ({ at, note, on, vel }); }

    /** Voiced chord: tones folded into a warm window around the anchor, + optional bass. */
    std::vector<int> voicing (int pc, int type, int anchor) const
    {
        const int centre = anchor >= 0 ? anchor : 60;
        std::vector<int> v;
        for (int iv : intervals (type))
        {
            int n = (centre / 12) * 12 + pc + iv;
            while (n < centre - 5) n += 12;
            while (n > centre + 9) n -= 12;
            v.push_back (n);
        }
        std::sort (v.begin(), v.end());
        v.erase (std::unique (v.begin(), v.end()), v.end());
        // BASS: a strong root two octaves down + one octave down, so it is heard even on small speakers
        if (set.bass) { v.insert (v.begin(), 36 + pc); v.insert (v.begin() + 1, 48 + pc); v.erase (std::unique (v.begin(), v.end()), v.end()); }
        return v;
    }

    void selectChord (OmniSynth& s, int pc, int type, int keyNote, float vel, bool fresh)
    {
        const bool changed = pc != chordRoot || type != chordType;
        chordRoot = pc; chordType = type;
        lastRoot = pc; lastType = type;
        switch (set.mode)
        {
            case PlayMode::Notes:
            {
                if (keyNote < 0) playPad (s, pc, type, 60, vel);
                break;
            }
            case PlayMode::Chord:
            {
                if (keyNote >= 0 && fresh)
                {
                    auto v = voicing (pc, type, keyNote);
                    for (int n : v) s.noteOn (n, vel * 0.85f);
                    keyChords[keyNote] = v;
                }
                else if (keyNote < 0) playPad (s, pc, type, 60, vel);
                break;
            }
            case PlayMode::Strum:
            {
                if (fresh || changed)
                    for (int k = 0; k < 8; ++k)
                    {
                        const int note = stringNote (k + 1);
                        const int64_t t = clock + (int64_t) (k * set.strumMs * 0.001 * fs);
                        schedule (t, note, true, vel * (0.75f + 0.25f * (k / 8.0f)));
                        schedule (t + (int64_t) (set.pluck * fs * 1.6), note, false, 0.0f);
                    }
                if (set.bass) for (int b : { 36 + pc, 48 + pc }) { s.noteOn (b, b < 48 ? 1.0f : vel * 0.8f); schedule (clock + (int64_t) (set.pluck * fs * 3.0), b, false, 0.0f); }
                break;
            }
            case PlayMode::Arp:
            {
                buildArp();
                if (fresh && changed) { arpIdx = 0; arpDir = 1; }
                updateArpBass (s, vel * 0.8f);
                break;
            }
        }
    }

    void playPad (OmniSynth& s, int pc, int type, int anchor, float vel)
    {
        auto v = voicing (pc, type, anchor);
        // voice-leading: keep common tones ringing, move only what changes
        for (int n : padNotes) if (std::find (v.begin(), v.end(), n) == v.end()) s.noteOff (n);
        for (int n : v) if (std::find (padNotes.begin(), padNotes.end(), n) == padNotes.end()) s.noteOn (n, vel * 0.8f);
        padNotes = v;
    }
    void releasePad (OmniSynth& s) { for (int n : padNotes) s.noteOff (n); padNotes.clear(); }

    void releaseChord (OmniSynth& s)
    {
        releasePad (s); arpRelease (s); releaseArpBass (s);
        chordRoot = -1;
    }

    void buildArp()
    {
        arpNotes.clear();
        if (chordRoot < 0) return;
        const auto iv = intervals (chordType);
        for (int o = 0; o < clampv (set.arpOctaves, 1, 3); ++o)
            for (int i : iv) arpNotes.push_back (48 + chordRoot + i + 12 * o);
        std::sort (arpNotes.begin(), arpNotes.end());
    }

    void arpRelease (OmniSynth& s)
    {
        for (auto it = pending.begin(); it != pending.end();)
            if (! it->on) { s.noteOff (it->note); it = pending.erase (it); } else ++it;
    }

    void arpFire (OmniSynth& s, double stepSamples)
    {
        buildArp();
        if (arpNotes.empty()) return;
        const int cnt = (int) arpNotes.size();
        int idx = 0;
        switch (set.arpPattern)
        {
            case ArpPattern::Up:   idx = arpIdx % cnt; ++arpIdx; break;
            case ArpPattern::Down: idx = cnt - 1 - (arpIdx % cnt); ++arpIdx; break;
            case ArpPattern::UpDown:
            {
                const int period = std::max (1, 2 * cnt - 2);
                const int p = arpIdx % period;
                idx = p < cnt ? p : period - p;
                ++arpIdx; break;
            }
            case ArpPattern::Random: rnd = rnd * 1664525u + 1013904223u; idx = (int) ((rnd >> 16) % (uint32_t) cnt); ++arpIdx; break;
        }
        const int note = arpNotes[(size_t) idx];
        const float accent = (arpIdx % 4 == 1) ? 0.95f : 0.72f;
        s.noteOn (note, accent);
        schedule (clock + std::max<int64_t> (1, (int64_t) (stepSamples * clampv (set.arpGate, 0.05f, 1.0f))), note, false, 0.0f);
    }
};
} // namespace oc
