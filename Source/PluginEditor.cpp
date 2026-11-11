#include "PluginEditor.h"

using namespace juce;

namespace
{
    const Colour red   { 0xffd42f3a };
    const Colour chalk { 0xfff2f2f0 };

    // the six knobs on the instrument's front panel
    struct KnobDef { const char* id; const char* tip; };
    const KnobDef kPanelKnobs[6] = {
        { ParamIDs::pitchTune, "Tune" },       { ParamIDs::glide, "Glide" },        { ParamIDs::vibDepth, "Vibrato" },
        { ParamIDs::drive,     "Tube Drive" }, { ParamIDs::revMix, "Reverb" },      { ParamIDs::output, "Volume" } };
}

// =============================================================================
//  EnginePanel
// =============================================================================
EnginePanel::EnginePanel (AudioProcessorValueTreeState& state) : apvts (state)
{
    using namespace ParamIDs;
    const Colour c = Palette::pitch;

    rows.push_back ({ { "PITCH", { knob (pitchTune, "Tuning"), knob (pitchSens, "Sensitivity"), knob (glide, "Glide"),
                                   knob (quantAmount, "Quantize"),
                                   choice (scale, "Scale", { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min" }),
                                   choice (root, "Root") }, {} },
                      { "TIMBRE", { knob (morph, "Morph"), knob (drive, "Tube Drive"), knob (brightness, "Brightness"),
                                    choice (formant, "Formant", { "Off", "Ethereal", "Soprano", "Tube" }), knob (formantMix, "Formant Mix") }, {} } });
    rows.push_back ({ { "VOLUME & EXPRESSION", { knob (volSens, "Vol Sens"), knob (attack, "Attack"), knob (release, "Release") }, {} },
                      { "HETERODYNE", { knob (coupling, "RF Coupling"), knob (drift, "RF Drift") }, {} },
                      { "HUMANIZE", { knob (vibDepth, "Vib Depth"), knob (vibRate, "Vib Rate"), knob (tremolo, "Tremolo"), knob (humanize, "Humanize") }, {} } });
    rows.push_back ({ { "TAPE ECHO / HALL", { knob (delayTime, "Delay Time"), knob (delayFb, "Feedback"), knob (delayMix, "Delay Mix"),
                                              knob (revSize, "Rev Size"), knob (revMix, "Rev Mix"), knob (output, "Output") }, {} },
                      { "MIDI", { toggle (mpe, "MPE"), knob (pbRange, "PB Range") }, {} } });
    ignoreUnused (c);

    close.setColour (TextButton::buttonColourId, Colours::transparentBlack);
    close.setColour (TextButton::buttonOnColourId, Colours::transparentBlack);
    close.setColour (TextButton::textColourOffId, chalk.withAlpha (0.7f));
    close.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

Component* EnginePanel::knob (const char* id, const String& caption)
{
    auto k = std::make_unique<LabelledKnob> (apvts, id, caption, Palette::pitch);
    auto* raw = k.get(); addAndMakeVisible (raw); controls.push_back (std::move (k)); return raw;
}

Component* EnginePanel::choice (const char* id, const String& caption, const StringArray& shortNames)
{
    auto k = std::make_unique<LabelledChoice> (apvts, id, caption, Palette::pitch, shortNames);
    auto* raw = k.get(); addAndMakeVisible (raw); controls.push_back (std::move (k)); return raw;
}

Component* EnginePanel::toggle (const char* id, const String& caption)
{
    auto k = std::make_unique<LabelledToggle> (apvts, id, caption, red);
    auto* raw = k.get(); addAndMakeVisible (raw); controls.push_back (std::move (k)); return raw;
}

void EnginePanel::layoutRow (std::vector<Section>& row, Rectangle<int> area, int maxCell)
{
    constexpr int gap = 46;
    size_t total = 0;
    for (const auto& s : row) total += s.items.size();
    const int cell = jmin (maxCell, (area.getWidth() - gap * ((int) row.size() - 1)) / (int) total);
    const int used = cell * (int) total + gap * ((int) row.size() - 1);
    int x = area.getX() + (area.getWidth() - used) / 2;
    for (auto& s : row)
    {
        const int w = cell * (int) s.items.size();
        s.bounds = { x, area.getY(), w, area.getHeight() };
        auto inner = s.bounds.withTrimmedTop (26);
        for (auto* c : s.items) c->setBounds (inner.removeFromLeft (cell));
        x += w + gap;
    }
}

void EnginePanel::resized()
{
    auto r = getLocalBounds().reduced (56, 0);
    auto top = r.removeFromTop (78);
    close.setBounds (top.removeFromRight (110).withSizeKeepingCentre (110, 34));
    const int rowH = (r.getHeight() - 30) / 3;
    for (size_t i = 0; i < rows.size(); ++i)
        layoutRow (rows[i], r.withY (r.getY() + (int) i * rowH).withHeight (rowH - 8), 108);
}

void EnginePanel::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xf2101012), 0.0f, 0.0f, Colour (0xfa060607), 0.0f, b.getBottom(), false));
    g.fillRect (b);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.35f), b.getCentreX(), 0.0f,
                                       Colours::white.withAlpha (0.0f), b.getRight(), 0.0f, true));
    g.fillRect (b.withHeight (1.0f));

    g.setColour (chalk.withAlpha (0.92f));
    g.setFont (uiFont (30.0f).withExtraKerningFactor (0.4f));
    g.drawText ("ENGINE", Rectangle<float> (56.0f, 22.0f, 400.0f, 40.0f), Justification::centredLeft);
    g.setColour (red);
    g.fillRect (60.0f, 66.0f, 40.0f, 2.0f);
    g.setColour (chalk.withAlpha (0.4f));
    g.setFont (uiFont (12.0f).withExtraKerningFactor (0.3f));
    g.drawText ("AETHERVOX  /  HETERODYNE SYNTHESIS", Rectangle<float> (290.0f, 30.0f, 600.0f, 24.0f), Justification::centredLeft);

    for (const auto& row : rows)
        for (const auto& s : row)
        {
            const auto r = s.bounds.toFloat();
            g.setColour (chalk.withAlpha (0.55f));
            g.setFont (uiFont (11.0f, true).withExtraKerningFactor (0.3f));
            g.drawText (s.title, r.withHeight (16.0f), Justification::centredLeft);
            g.setColour (chalk.withAlpha (0.08f));
            g.fillRect (r.getX(), r.getY() + 19.0f, r.getWidth(), 1.0f);
        }
}

// =============================================================================
//  Editor
// =============================================================================
AetherVoxEditor::AetherVoxEditor (AetherVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), instrument (p), engine (p.apvts)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.setBounds (0, 0, designWidth, designHeight);

    instrument.setBounds (body.getLocalBounds());
    instrument.onDisplayClicked = [this] { setEngineOpen (true); };
    body.addAndMakeVisible (instrument);

    for (int i = 0; i < 6; ++i)
    {
        auto& k = panelKnobs[(size_t) i];
        k.setBounds (Rectangle<int> (72, 72).withCentre ({ roundToInt (Art::knobX[i]), roundToInt (Art::knobY) }));
        k.setPopupDisplayEnabled (true, true, &body);
        k.setTooltip (kPanelKnobs[i].tip);
        body.addAndMakeVisible (k);
        knobAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kPanelKnobs[i].id, k));
        if (auto* prm = p.apvts.getParameter (kPanelKnobs[i].id))
            k.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }

    engine.onClose = [this] { setEngineOpen (false); };
    engine.setBounds (0, designHeight, designWidth, 600);
    body.addChildComponent (engine);

    setResizable (true, true);
    setResizeLimits (designWidth * 45 / 100, designHeight * 45 / 100, designWidth * 3 / 2, designHeight * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (designWidth * 72 / 100, designHeight * 72 / 100);

    lastTick = Time::getMillisecondCounterHiRes() > 0 ? (int64) Time::getMillisecondCounterHiRes() : 0;
    startTimerHz (60);
}

AetherVoxEditor::~AetherVoxEditor()
{
    stopTimer();
    proc.padActive.store (false);
    setLookAndFeel (nullptr);
}

void AetherVoxEditor::setEngineOpen (bool open)
{
    engineOpen = open;
    if (open) engine.setVisible (true);
}

void AetherVoxEditor::timerCallback()
{
    const auto now = (int64) Time::getMillisecondCounterHiRes();
    const float dt = jlimit (0.0f, 0.1f, (float) (now - lastTick) * 0.001f);
    lastTick = now;
    instrument.tick (dt);
    const float revealX = instrument.getRevealX();          // knobs appear behind the red line
    for (int i = 0; i < 6; ++i)
        panelKnobs[(size_t) i].setVisible (revealX > Art::knobX[i] + 30.0f);

    const float target = engineOpen ? 1.0f : 0.0f;
    if (std::abs (enginePos - target) > 0.001f)
    {
        enginePos += (target - enginePos) * 0.18f;
        const float e = 1.0f - std::pow (1.0f - enginePos, 3.0f);
        engine.setTopLeftPosition (0, designHeight - roundToInt (e * (float) engine.getHeight()));
        if (! engineOpen && enginePos < 0.01f) { enginePos = 0.0f; engine.setVisible (false); }
    }
}

void AetherVoxEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void AetherVoxEditor::resized()
{
    body.setTransform (AffineTransform::scale ((float) getWidth() / (float) designWidth));
}
