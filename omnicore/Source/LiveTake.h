#pragma once
// =============================================================================
//  LiveTake: everything the pyramid plays (chord keys, strums, arp steps, keys)
//  collected as notes, ready to be dragged into a piano roll as a .mid file.
//  While the host plays, notes keep their song position (bar-aligned when dropped);
//  otherwise they keep the live timing at the host tempo.
// =============================================================================
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <cmath>

class LiveTake
{
public:
    struct Ev { double sec, ppq; int note; float vel; bool on; };

    /** Silence longer than this starts a fresh take. */
    static constexpr double gapForNewTake = 3.0;

    void add (const Ev& e)
    {
        const bool synced = e.ppq >= 0.0;
        if (e.on)
        {
            if (! evs.empty())
            {
                const bool modeChanged = synced != (evs.back().ppq >= 0.0);
                const bool longGap = e.sec - lastOnSec > gapForNewTake;          // a pause in playing
                if (finished || modeChanged || longGap) clear();
            }
            finished = false;
            lastOnSec = e.sec; lastOnPpq = e.ppq;
            ++notes;
        }
        else if (evs.empty()) return;                                           // stray note-off before the take
        if (e.note >= 0 && e.note < 128) held[(size_t) e.note] = e.on;
        lastSec = e.sec;
        evs.push_back (e);
    }

    /** After a drag-out: the next note played starts a brand-new take. */
    void markFinished() { finished = true; }
    bool isFinished() const { return finished; }

    void clear() { evs.clear(); held.fill (false); notes = 0; }
    bool isEmpty() const { return notes == 0; }
    int countNotes() const { return notes; }
    bool isSounding (double nowSec = -1.0) const
    {
        if (nowSec >= 0.0) return nowSec - lastOnSec < 0.6;
        for (bool h : held) if (h) return true;
        return false;
    }
    double lastEventSec() const { return lastSec; }

    bool writeTo (const juce::File& file, double bpm) const
    {
        if (notes == 0) return false;
        constexpr int tpq = 960;
        const double tempo = juce::jlimit (20.0, 400.0, bpm);
        const bool synced = evs.front().ppq >= 0.0;

        // start of the take: bar line before the first note (synced) or the first note itself
        // the first note keeps its place in the bar (when the host was playing);
        // everything after it keeps the live timing, so pattern loops never fold notes on top of each other
        const Ev* first = nullptr;
        for (auto& e : evs) if (e.on) { first = &e; break; }
        if (first == nullptr) return false;
        const double lead = synced ? first->ppq - std::floor (first->ppq / 4.0) * 4.0 : 0.0;
        const double t0 = first->sec;
        auto tick = [&] (const Ev& e)
        {
            const double beats = lead + (e.sec - t0) * tempo / 60.0;
            return juce::jmax (0.0, std::round (beats * tpq));
        };

        juce::MidiMessageSequence seq;
        auto put = [&] (juce::MidiMessage m, double t) { m.setTimeStamp (t); seq.addEvent (m); };
        put (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempo)), 0);
        put (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        put (juce::MidiMessage::textMetaEvent (3, "OmniCore 3000 live take"), 0);

        // pair every note-on with its note-off; anything still held ends a beat later
        std::vector<std::vector<double>> open (128);
        std::vector<int> openVel (128, 100);
        double lastTick = 0.0;
        for (auto& e : evs)
        {
            const double t = tick (e);
            lastTick = juce::jmax (lastTick, t);
            if (e.on)
            {
                if (! open[(size_t) e.note].empty())   // retrigger: close the previous one first
                {
                    put (juce::MidiMessage::noteOff (1, e.note), juce::jmax (open[(size_t) e.note].back() + 1.0, t));
                    open[(size_t) e.note].clear();
                }
                const auto v = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (e.vel * 127.0f));
                put (juce::MidiMessage::noteOn (1, e.note, v), t);
                open[(size_t) e.note].push_back (t);
            }
            else if (! open[(size_t) e.note].empty())
            {
                put (juce::MidiMessage::noteOff (1, e.note), juce::jmax (open[(size_t) e.note].back() + 1.0, t));
                open[(size_t) e.note].clear();
            }
        }
        for (int n = 0; n < 128; ++n)
            if (! open[(size_t) n].empty())
                put (juce::MidiMessage::noteOff (1, n), juce::jmax (open[(size_t) n].back() + 1.0, lastTick + tpq));

        seq.updateMatchedPairs();
        seq.sort();
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (tpq);
        mf.addTrack (seq);
        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk()) return false;
        mf.writeTo (out);
        return true;
    }

private:
    std::vector<Ev> evs;
    std::array<bool, 128> held {};
    int notes = 0;
    double lastSec = -1.0e9, lastOnSec = -1.0e9, lastOnPpq = -1.0;
    bool finished = false;
};
