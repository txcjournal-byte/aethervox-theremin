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

    int countNotes() const { return (int) buildNotes().size(); }

    /** Writes the take as plain MIDI notes that FL's piano roll keeps:
        the live melody becomes a legato line of notes that follows every slide
        semitone by semitone. Alpine plays back-to-back notes legato on the same
        voice, so with Glide the line slides exactly like the live playing. */
    bool writeTo (const juce::File& file, double bpm) const
    {
        if (buildPhrases().empty()) return false;

        constexpr int ppq = 960;
        const double tempoBpm = juce::jlimit (20.0, 400.0, bpm);
        const double ticksPerSec = ppq * tempoBpm / 60.0;
        auto tick = [&] (double t) { return juce::jmax (0.0, std::round ((t - start) * ticksPerSec)); };

        juce::MidiMessageSequence seq;
        auto add = [&] (juce::MidiMessage m, double ticks) { m.setTimeStamp (ticks); seq.addEvent (m); };
        add (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / tempoBpm)), 0);
        add (juce::MidiMessage::textMetaEvent (3, "Alpine Theremin live take"), 0);

        auto vel = [] (float v) { return (juce::uint8) juce::jlimit (30, 127, juce::roundToInt (35.0f + v * 92.0f)); };

        for (const auto& nt : buildNotes())
        {
            add (juce::MidiMessage::noteOn  (1, nt.note, vel (nt.vol)), tick (nt.on));
            add (juce::MidiMessage::noteOff (1, nt.note), juce::jmax (tick (nt.on) + 1.0, tick (nt.off)));
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

    static constexpr double gapForNewTake = 4.0;    // seconds of silence start a fresh take
    static constexpr float  onLevel  = 0.06f;       // hand volume that counts as sounding
    static constexpr float  offLevel = 0.03f;

    std::vector<Point> points;
    double start = 0.0;

    struct Note { int note; float vol; double on, off; };

    /** The live line as back-to-back notes: a new note each time the hand clearly
        reaches the next semitone (hysteresis), so every slide shows up as notes. */
    std::vector<Note> buildNotes() const
    {
        std::vector<Note> out;
        for (const auto& ph : buildPhrases())
        {
            const auto& p0 = points[(size_t) ph.first];
            Note cur { juce::jlimit (0, 127, (int) std::lround (p0.pitch)), p0.vol, p0.t, p0.t };
            for (int i = ph.first + 1; i <= ph.last; ++i)
            {
                const auto& pt = points[(size_t) i];
                cur.off = pt.t;
                if (std::fabs (pt.pitch - (float) cur.note) < 0.62f) continue;
                const int next = juce::jlimit (0, 127, (int) std::lround (pt.pitch));
                if (next == cur.note || pt.t - cur.on < 0.004) continue;
                out.push_back (cur);
                cur = { next, pt.vol, pt.t, pt.t };
            }
            cur.off = juce::jmax (cur.off, cur.on + 0.03);
            out.push_back (cur);
        }
        return out;
    }

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
