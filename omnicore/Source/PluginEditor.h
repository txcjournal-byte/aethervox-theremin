#pragma once
#include "PluginProcessor.h"

// =============================================================================
//  OmniCore 3000 editor: a piece of retro-futurist hardware, drawn in code.
//  Everything lives in a fixed 1280 x 1000 "panel" that is scaled to the window.
// =============================================================================
namespace omniui
{
using namespace juce;

namespace col
{
    const Colour chassisTop   { 0xff2b2d31 };
    const Colour chassisBot   { 0xff16171a };
    const Colour panel        { 0xff1d1f23 };
    const Colour engrave      { 0xff0c0d0f };
    const Colour silk         { 0xffb9bcc2 };
    const Colour silkDim      { 0xff6d7178 };
    const Colour red          { 0xffff2a2a };
    const Colour redDeep      { 0xffb3121a };
    const Colour redDark      { 0xff4a0a0e };
    const Colour glass        { 0xff070304 };
    const Colour phosphor     { 0xffff3b30 };
}

class OmniLook : public LookAndFeel_V4
{
public:
    OmniLook();
    void drawRotarySlider (Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, Slider&) override;
    void drawButtonBackground (Graphics&, Button&, const Colour&, bool over, bool down) override;
    void drawButtonText (Graphics&, TextButton&, bool over, bool down) override;
    static Font mono (float h) { return Font (FontOptions (Font::getDefaultMonospacedFontName(), h, Font::bold)); }
    static Font sans (float h, bool bold = true) { return Font (FontOptions (h, bold ? Font::bold : Font::plain)); }
};

/** Something that wants to show "NAME  value" on the main display while being touched. */
struct Readout
{
    std::function<void (const String&, const String&)> show;
};

class Knob : public Component, private Slider::Listener
{
public:
    Knob (AudioProcessorValueTreeState& s, const char* paramId, const String& label, Readout& r, bool big = false);
    void resized() override;
    void paint (Graphics&) override;
private:
    void sliderValueChanged (Slider*) override;
    void sliderDragStarted (Slider*) override;
    Slider slider;
    String label;
    AudioProcessorValueTreeState::SliderAttachment att;
    RangedAudioParameter* param;
    Readout& readout;
    bool big;
};

/** Illuminated latching push button bound to a bool parameter. */
class LampButton : public TextButton
{
public:
    LampButton (AudioProcessorValueTreeState& s, const char* paramId, const String& text);
private:
    AudioProcessorValueTreeState::ButtonAttachment att;
};

/** Row of illuminated buttons for a choice parameter (mode, chord type...). */
class Selector : public Component, private Timer
{
public:
    Selector (AudioProcessorValueTreeState& s, const char* paramId, const StringArray& names, Readout& r, int columns = 0);
    void resized() override;
    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
private:
    void timerCallback() override { const int v = current(); if (v != shown) { shown = v; repaint(); } }
    int current() const { return roundToInt (param->convertFrom0to1 (param->getValue())); }
    Rectangle<float> cell (int i) const;
    RangedAudioParameter* param;
    StringArray names; Readout& readout; int columns, shown = -1;
};

/** The main glass display: chord name, mode, tempo, scope, arp lights, readouts. */
class Display : public Component, private Timer
{
public:
    explicit Display (OmniProcessor& p);
    void paint (Graphics&) override;
    void showReadout (const String& name, const String& value);
private:
    void timerCallback() override;
    OmniProcessor& proc;
    String roName, roValue; double roUntil = 0.0;
    float scope[128] {};
    float boot = 0.0f;
};

/** Mechanical key caps: chord buttons (3 x 12) and the playing keyboard, glissando-able. */
class KeyField : public Component, private Timer
{
public:
    enum Kind { ChordButtons, Keyboard };
    KeyField (OmniProcessor& p, Kind k);
    void paint (Graphics&) override;
    void resized() override { layout(); }
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
private:
    struct Cap { Rectangle<float> r; int channel, note; String label, sub; bool black = false; };
    void timerCallback() override;
    void layout();
    int hit (Point<float>) const;
    void press (int idx, float vel);
    void releaseCurrent();
    bool lit (const Cap&) const;
    OmniProcessor& proc; Kind kind;
    std::vector<Cap> caps; int down = -1;
    std::vector<bool> litCache;
};

/** Omnichord-style touch plate: 13 "sonic strings", strum by swiping. */
class StrumPlate : public Component, private Timer
{
public:
    explicit StrumPlate (OmniProcessor& p);
    void paint (Graphics&) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override { lastString = -1; }
private:
    void timerCallback() override;
    int stringAt (float y) const;
    void pluckTo (int s, float speed);
    OmniProcessor& proc;
    int lastString = -1; double lastTime = 0.0; float lastY = 0.0f;
    std::array<float, 13> glow {};
};

/** The stone pyramid with the red cat's eye: watches the mouse, pulses with the sound,
    and when you click it, it mutates the patch. */
class PyramidEye : public Component, private Timer
{
public:
    explicit PyramidEye (OmniProcessor& p);
    void paint (Graphics&) override;
    void resized() override { stone = {}; }
    void mouseDown (const MouseEvent&) override;
    bool hitTest (int x, int y) override { return eyeBounds().expanded (14.0f).contains ((float) x, (float) y); }
    std::function<void()> onMutate;
private:
    void timerCallback() override;
    Rectangle<float> eyeBounds() const;
    void buildStone();
    OmniProcessor& proc;
    Image stone;
    Point<float> gaze, gazeTarget;
    float pulse = 0.0f, flash = 0.0f, blink = 0.0f; double nextBlink = 0.0;
};

/** Everything on the 1280 x 1000 front panel. */
class FrontPanel : public Component
{
public:
    static constexpr int W = 1280, H = 1000;
    explicit FrontPanel (OmniProcessor& p);
    void paint (Graphics&) override;
    void resized() override;
private:
    void section (Graphics&, Rectangle<int>, const String& title) const;
    OmniProcessor& proc;
    Readout readout;
    Display display;
    PyramidEye eye;
    OwnedArray<Knob> knobs;
    std::unique_ptr<Selector> mode, chordType, arpRate, arpPat;
    std::unique_ptr<LampButton> hold, bass;
    KeyField chordKeys, keyboard;
    StrumPlate plate;
    std::vector<std::pair<Knob*, Rectangle<int>>> knobSpots;
    std::vector<std::pair<Rectangle<int>, String>> sections;
};
} // namespace omniui

class OmniEditor : public juce::AudioProcessorEditor
{
public:
    explicit OmniEditor (OmniProcessor&);
    ~OmniEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    omniui::OmniLook look;
    omniui::FrontPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OmniEditor)
};
