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

    bool isEmpty() const { return buildPhrases().empty(); }
    void clear() { points.clear(); }

    int countNotes() const { return (int) buildPhrases().size(); }

    /** Writes the take as a one-track MIDI file that replays the live playing 1:1:
        one note per phrase (re-struck only for very wide slides) + 14-bit pitch bend
        for every slide and wobble + CC11 expression for the hand volume.
        Pitch bend range is announced with RPN 0 (and the plugin defaults to it). */
    bool writeTo (const juce::File& file, double bpm) const
    {
        const auto phrases = buildPhrases();
        if (phrases.empty()) return false;

        constexpr int ppq = 960;
        const double tempoBpm = juce::jlimit (20.0, 400.0, bpm);
        const double ticksPerSec = ppq * tempoBpm / 60.0;
        auto tick = [&] (double t) { return (t - start) * ticksPerSec; };

        juce::MidiMessageSequence seq;
        auto add = [&] (juce::MidiMessage m, double ticks) { m.setTimeStamp (juce::jmax (0.0, ticks)); seq.addEvent (m); };

        add (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempoBpm)), 0);
        add (juce::MidiMessage::textMetaEvent (3, "Alpine Theremin live take"), 0);
        // RPN 0: pitch bend sensitivity = bendRange semitones
        add (juce::MidiMessage::controllerEvent (1, 101, 0), 0);
        add (juce::MidiMessage::controllerEvent (1, 100, 0), 0);
        add (juce::MidiMessage::controllerEvent (1, 6, bendRange), 0);
        add (juce::MidiMessage::controllerEvent (1, 38, 0), 0);
        add (juce::MidiMessage::controllerEvent (1, 101, 127), 0);
        add (juce::MidiMessage::controllerEvent (1, 100, 127), 0);

        auto bendValue = [] (float semis)
        {
            const float v = juce::jlimit (-1.0f, 1.0f, semis / (float) bendRange);
            return juce::jlimit (0, 16383, 8192 + juce::roundToInt (v * (v >= 0 ? 8191.0f : 8192.0f)));
        };

        for (const auto& ph : phrases)
        {
            int base = juce::jlimit (0, 127, (int) std::lround (points[(size_t) ph.first].pitch));
            const double t0 = points[(size_t) ph.first].t;
            // bend + expression first, so the note starts exactly where the hand was
            add (juce::MidiMessage::pitchWheel (1, bendValue (points[(size_t) ph.first].pitch - (float) base)), tick (t0) - 1);
            add (juce::MidiMessage::controllerEvent (1, 11, juce::jlimit (1, 127, juce::roundToInt (points[(size_t) ph.first].vol * 127.0f))), tick (t0) - 1);
            add (juce::MidiMessage::noteOn (1, base, (juce::uint8) 127), tick (t0));

            double lastT = t0; int lastBend = -1, lastCc = -1;
            for (int i = ph.first; i <= ph.last; ++i)
            {
                const auto& pt = points[(size_t) i];
                if (std::fabs (pt.pitch - (float) base) > (float) bendRange - 1.0f)
                {
                    // very wide slide: re-strike legato on a new base note, bend continues seamlessly
                    add (juce::MidiMessage::noteOff (1, base), tick (pt.t));
                    base = juce::jlimit (0, 127, (int) std::lround (pt.pitch));
                    add (juce::MidiMessage::pitchWheel (1, bendValue (pt.pitch - (float) base)), tick (pt.t));
                    add (juce::MidiMessage::noteOn (1, base, (juce::uint8) 127), tick (pt.t) + 0.5);
                    lastBend = -1;
                }
                if (pt.t - lastT < 0.006 && i != ph.last) continue;   // ~166 points / s is plenty
                lastT = pt.t;
                const int b = bendValue (pt.pitch - (float) base);
                if (b != lastBend) { add (juce::MidiMessage::pitchWheel (1, b), tick (pt.t)); lastBend = b; }
                const int c = juce::jlimit (1, 127, juce::roundToInt (pt.vol * 127.0f));
                if (c != lastCc) { add (juce::MidiMessage::controllerEvent (1, 11, c), tick (pt.t)); lastCc = c; }
            }
            const double tEnd = juce::jmax (points[(size_t) ph.last].t, t0 + 0.03);
            add (juce::MidiMessage::noteOff (1, base), tick (tEnd));
        }
        // leave the channel clean for whatever comes next
        const double tail = tick (points.back().t) + ppq / 8.0;
        add (juce::MidiMessage::pitchWheel (1, 8192), tail);
        add (juce::MidiMessage::controllerEvent (1, 11, 127), tail);

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

    static constexpr int bendRange = 12;   // semitones, matches the plugin's default Pitch Bend Range

private:
    struct Point { double t; float pitch, vol; };

    static constexpr double gapForNewTake = 4.0;    // seconds of silence start a fresh take
    static constexpr float  onLevel  = 0.06f;       // hand volume that counts as sounding
    static constexpr float  offLevel = 0.03f;

    std::vector<Point> points;
    double start = 0.0;

    struct Phrase { int first, last; };

    /** Sounding stretches of the take (hand volume above the threshold). */
    std::vector<Phrase> buildPhrases() const
    {
        std::vector<Phrase> out;
        int first = -1;
        for (int i = 0; i < (int) points.size(); ++i)
        {
            const float v = points[(size_t) i].vol;
            if (first < 0) { if (v >= onLevel) first = i; continue; }
            if (v < offLevel)
            {
                if (points[(size_t) (i - 1)].t - points[(size_t) first].t >= 0.03) out.push_back ({ first, i - 1 });
                first = -1;
            }
        }
        if (first >= 0 && (int) points.size() - 1 > first) out.push_back ({ first, (int) points.size() - 1 });
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
        g.drawText (n > 0 ? "DRAG MIDI  (" + juce::String (n) + (n == 1 ? " phrase)" : " phrases)") : "PLAY, THEN DRAG MIDI",
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
