#pragma once
// =============================================================================
//  MidiTake: remembers what was played with the mouse and turns it into MIDI
//  notes (nearest semitone), so the take can be dragged into a piano roll.
// =============================================================================
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <cmath>

class MidiTake
{
public:
    /** Add one point of the performance. timeSec is wall-clock seconds. */
    void add (double timeSec, float midiPitch, float volume)
    {
        if (! points.empty() && timeSec - points.back().t > gapForNewTake) points.clear();
        if (points.empty()) start = timeSec;
        points.push_back ({ timeSec, midiPitch, volume });
    }

    /** A pause in playing (mouse released): marks silence. */
    void release (double timeSec)
    {
        if (! points.empty()) points.push_back ({ timeSec, points.back().pitch, 0.0f });
    }

    bool isEmpty() const { return countNotes() == 0; }
    void clear() { points.clear(); }

    int countNotes() const { return (int) buildNotes().size(); }

    /** Writes the take as a one-track MIDI file. Returns false if there is nothing to write. */
    bool writeTo (const juce::File& file, double bpm) const
    {
        const auto notes = buildNotes();
        if (notes.empty()) return false;

        constexpr int ppq = 960;
        const double ticksPerSec = ppq * juce::jlimit (20.0, 400.0, bpm) / 60.0;

        juce::MidiMessageSequence seq;
        auto tempo = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jlimit (20.0, 400.0, bpm)));
        tempo.setTimeStamp (0);
        seq.addEvent (tempo);
        auto name = juce::MidiMessage::textMetaEvent (3, "Alpine Theremin");
        name.setTimeStamp (0);
        seq.addEvent (name);

        for (const auto& n : notes)
        {
            const double on  = (n.on  - start) * ticksPerSec;
            const double off = juce::jmax (on + ppq / 32.0, (n.off - start) * ticksPerSec);
            seq.addEvent (juce::MidiMessage::noteOn  (1, n.note, (juce::uint8) n.velocity), on);
            seq.addEvent (juce::MidiMessage::noteOff (1, n.note), off);
        }
        seq.updateMatchedPairs();
        seq.sort();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (seq);

        file.deleteFile();
        juce::FileOutputStream out (file);
        if (! out.openedOk()) return false;
        mf.writeTo (out);
        return true;
    }

private:
    struct Point { double t; float pitch, vol; };
    struct Note  { int note; int velocity; double on, off; };

    static constexpr double gapForNewTake = 4.0;    // seconds of silence start a fresh take
    static constexpr float  onLevel  = 0.06f;       // hand volume that counts as sounding
    static constexpr float  offLevel = 0.03f;
    static constexpr double minHold  = 0.07;        // a pitch must hold this long to become a new note

    std::vector<Point> points;
    double start = 0.0;

    std::vector<Note> buildNotes() const
    {
        std::vector<Note> out;
        bool sounding = false;
        int cur = -1; float peak = 0.0f; double noteStart = 0.0;
        int cand = -1; double candSince = 0.0;

        auto close = [&] (double t)
        {
            if (cur >= 0 && t - noteStart >= 0.03)
                out.push_back ({ cur, juce::jlimit (20, 127, juce::roundToInt (40.0f + peak * 87.0f)), noteStart, t });
            cur = -1; peak = 0.0f;
        };

        for (const auto& p : points)
        {
            const int semi = juce::jlimit (0, 127, (int) std::lround (p.pitch));
            if (! sounding)
            {
                if (p.vol >= onLevel) { sounding = true; cur = semi; noteStart = p.t; peak = p.vol; cand = -1; }
                continue;
            }
            if (p.vol < offLevel) { close (p.t); sounding = false; continue; }

            peak = juce::jmax (peak, p.vol);
            if (semi == cur) { cand = -1; continue; }
            if (semi != cand) { cand = semi; candSince = p.t; continue; }
            if (p.t - candSince >= minHold)
            {
                close (candSince);
                cur = semi; noteStart = candSince; peak = p.vol; cand = -1;
            }
        }
        if (sounding && ! points.empty()) close (points.back().t);
        return out;
    }
};

/** The "DRAG MIDI" pill: drag it out of the plugin and drop it into a piano roll. */
class DragMidiButton : public juce::Component
{
public:
    std::function<juce::File()> makeFile;   // returns an existing .mid file, or a non-existent one if empty
    std::function<int()> noteCount;

    void paint (juce::Graphics& g) override
    {
        const int n = noteCount ? noteCount() : 0;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (n > 0 ? 0xeee2b65c : 0x99403a30));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colour (n > 0 ? 0xff1f3a2c : 0xffe6dccb));
        g.setFont (juce::FontOptions (r.getHeight() * 0.42f, juce::Font::bold));
        g.drawText (n > 0 ? "DRAG MIDI  (" + juce::String (n) + " notes)" : "PLAY, THEN DRAG MIDI",
                    getLocalBounds(), juce::Justification::centred);
    }

    void mouseEnter (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! makeFile) return;
        const auto f = makeFile();
        if (! f.existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                     [this] { dragging = false; });
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    bool dragging = false;
};
