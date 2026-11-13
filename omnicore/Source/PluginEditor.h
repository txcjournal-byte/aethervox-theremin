#pragma once
#include "PluginProcessor.h"
#include "LiveTake.h"
#include <map>

// =============================================================================
//  OmniCore 3000 editor
//
//  The interface IS the pyramid: a photographic render (Assets/pyramid.jpg,
//  1254 x 1254). Code only brings it to life:
//    * every red key on the photo is playable (pressed keys sink, lit keys glow)
//    * the red cat's eye follows the mouse, burns with the sound, click = MUTATE
//    * the dark strip above the keys is a red LED read-out; drag it into a
//      piano roll to drop everything you just played as MIDI notes
//
//  Key map (on the photo):
//    row 1  1..0          major chords   Ab Eb Bb F C G D A E B
//    row 2  Q..P          minor chords   (same roots)
//    row 3  A..L + Enter  7th chords     (same roots)
//    row 4  Z..Enter      NOTES CHORD STRUM ARP | HOLD BASS | ARP RATE | COSMOS | MUTATE
//    row 5  [<]  long bar  [>]   preset down / strum (high strings) / preset up
//    row 6  long bar      strum plate (13 strings, swipe)
// =============================================================================
class OmniEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OmniEditor (OmniProcessor&);
    ~OmniEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void focusLost (FocusChangeType) override;
    void releaseKeyboardChords (bool all);
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum class KeyKind { Chord, Mode, Hold, Bass, ArpRate, Cosmos, Mutate, PresetDown, PresetUp, StrumHigh, Strum };
    struct Key { juce::Rectangle<float> r; KeyKind kind; int a = 0, b = 0; juce::String name; };

    void timerCallback() override;
    void buildKeys();
    int keyAt (juce::Point<float> photoPos) const;
    // the window shows a close crop of the photo, so the keys are big enough to read and hit
    static constexpr float viewX = 0.0f, viewY = 0.0f, viewW = 1254.0f;
    juce::Point<float> toPhoto (juce::Point<float> p) const { return p / scale() + juce::Point<float> (viewX, viewY); }
    juce::Rectangle<int> toScreen (juce::Rectangle<float> r) const
    { return ((r - juce::Point<float> (viewX, viewY)) * scale()).getSmallestIntegerContainer().expanded (2); }
    float scale() const { return (float) getWidth() / viewW; }
    juce::Image photoScaled;          // the visible crop at window size (drawn as a plain blit)

    // computer keyboard: the letters printed on the keys play them
    int keyForCode (int code) const;
    juce::String keyLetter (int idx) const;
    std::map<int, int> kbHeld;        // key code -> chord key index
    int hoverKey = -1;
    juce::int64 lastPaintHash = 0;
    bool eyeWasLive = true;
    int modeBeforeArp = 1; bool holdBeforeArp = false;
    void toggleArp (bool forceOn);

    void pressKey (int idx, juce::Point<float> photoPos);
    void releaseKey();
    void strumAt (const Key& k, float photoX, float speed);
    bool isLit (const Key& k) const;
    void setChoice (const char* id, int index);
    void showLed (const juce::String& line1, const juce::String& line2 = {});

    OmniProcessor& proc;
    static constexpr int photoSize = 1254;
    juce::Image photo, photoLit;
    std::vector<Key> keys;

    int downKey = -1, lastString = -1; double lastStrumTime = 0.0; float lastStrumX = 0.0f;
    std::vector<bool> litCache;

    // eye
    juce::Point<float> gaze, gazeTarget;
    float pulse = 0.0f, flash = 0.0f;

    // live take -> DRAG MIDI (drag the LED read-out into a piano roll)
    LiveTake take;
    bool ledDrag = false, dragging = false;
    juce::File writeTake();

    // led
    juce::String led1, led2; double ledUntil = 0.0;
    std::array<float, 13> stringGlow {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OmniEditor)
};
