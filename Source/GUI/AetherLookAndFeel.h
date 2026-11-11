#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Palette
{
    // --- product stage (TrapWin reference: black textured arch, red lacquer, chrome)
    const juce::Colour stageLight { 0xffe6e6e6 };
    const juce::Colour stageDark  { 0xff141416 };
    const juce::Colour arch       { 0xff2a2a2c };   // matte pebbled black
    const juce::Colour archDark   { 0xff0b0b0c };
    const juce::Colour lacquer    { 0xff8a0f1a };   // deep red lacquer
    const juce::Colour chrome     { 0xffd8dadd };
    const juce::Colour lcd        { 0xff111214 };   // black glass display
    const juce::Colour lcdInk     { 0xffececec };
    const juce::Colour holo       { 0xff1c1c1e };   // line art drawn on the lit stage

    // --- UI
    const juce::Colour bg         { 0xff0e0e10 };
    const juce::Colour bgLight    { 0xff1b1c1f };
    const juce::Colour panel      { 0xf0141416 };
    const juce::Colour panelEdge  { 0xff2c2d31 };
    const juce::Colour text       { 0xffececec };
    const juce::Colour textDim    { 0xff8d9198 };

    // --- section accents: chrome, red and greys
    const juce::Colour pitch      { 0xffededed };
    const juce::Colour volume     { 0xffd42f3a };
    const juce::Colour osc        { 0xffa9b1ba };
    const juce::Colour midi       { 0xff8e949b };
    const juce::Colour timbre     { 0xffff5a64 };
    const juce::Colour mod        { 0xffc7ccd1 };
    const juce::Colour fx         { 0xff9aa3ad };
}


inline juce::Font uiFont (float height, bool bold = false)
{
    auto opts = juce::FontOptions().withHeight (height);
    if (bold) opts = opts.withStyle ("Bold");
    return juce::Font (opts);
}

class AetherLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AetherLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override        { return uiFont (11.5f); }
    juce::Font getPopupMenuFont() override                       { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label& l) override            { return l.getFont(); }
};
