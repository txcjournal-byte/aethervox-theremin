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

    // Only the controls that shape the sound musically; everything else keeps tuned defaults.
    rows.push_back ({ { "TONE", { knob (morph, "Waveform"), knob (drive, "Tube Drive"), knob (brightness, "Brightness"),
                                  choice (formant, "Body", { "Clean", "Ethereal", "Soprano", "Tube" }), knob (glide, "Glide") }, {} } });
    rows.push_back ({ { "MOVEMENT", { knob (vibDepth, "Vibrato"), knob (vibRate, "Vib Speed"), knob (humanize, "Humanize"),
                                      knob (attack, "Attack"), knob (release, "Release") }, {} } });
    rows.push_back ({ { "SPACE  &  TUNING", { knob (delayMix, "Echo"), knob (delayTime, "Echo Time"), knob (revMix, "Hall"),
                                              knob (quantAmount, "Snap"), choice (scale, "Scale", { "Chromatic", "Major", "Minor", "Penta Maj", "Penta Min" }) }, {} } });
    ignoreUnused (c);

    close.setColour (TextButton::buttonColourId, Colours::transparentBlack);
    close.setColour (TextButton::buttonOnColourId, Colours::transparentBlack);
    close.setColour (TextButton::textColourOffId, chalk.withAlpha (0.7f));
    close.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    close.onClick = [this] { if (onClose) onClose(); };
    close.setButtonText ("CLOSE  X");
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
        auto inner = s.bounds.withTrimmedTop (40);
        for (auto* c : s.items) c->setBounds (inner.removeFromLeft (cell));
        x += w + gap;
    }
}

void EnginePanel::resized()
{
    auto r = getLocalBounds().reduced (56, 0);
    auto top = r.removeFromTop (84);
    close.setBounds (top.removeFromRight (170).withSizeKeepingCentre (170, 50));
    const int rowH = (r.getHeight() - 30) / 3;
    for (size_t i = 0; i < rows.size(); ++i)
        layoutRow (rows[i], r.withY (r.getY() + (int) i * rowH).withHeight (rowH - 8), 200);
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
    g.setFont (uiFont (40.0f).withExtraKerningFactor (0.35f));
    g.drawText ("ENGINE", Rectangle<float> (56.0f, 16.0f, 400.0f, 50.0f), Justification::centredLeft);
    g.setColour (red);
    g.fillRect (60.0f, 66.0f, 40.0f, 2.0f);
    g.setColour (chalk.withAlpha (0.4f));
    g.setFont (uiFont (18.0f).withExtraKerningFactor (0.2f));
    g.drawText ("AETHERVOX  /  HETERODYNE SYNTHESIS", Rectangle<float> (330.0f, 28.0f, 700.0f, 28.0f), Justification::centredLeft);

    for (const auto& row : rows)
        for (const auto& s : row)
        {
            const auto r = s.bounds.toFloat();
            g.setColour (red);
            g.fillRect (r.getX(), r.getY() + 6.0f, 3.0f, 18.0f);
            g.setColour (chalk.withAlpha (0.8f));
            g.setFont (uiFont (20.0f, true).withExtraKerningFactor (0.25f));
            g.drawText (s.title, r.withHeight (30.0f).withTrimmedLeft (14.0f), Justification::centredLeft);
            g.setColour (chalk.withAlpha (0.10f));
            g.fillRect (r.getX(), r.getY() + 34.0f, r.getWidth(), 1.0f);
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
    engine.setBounds (0, designHeight, designWidth, 720);
    body.addChildComponent (engine);

    // default size: about half the screen width (fits comfortably inside the DAW), resizable from the corner
    int startW = 800;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        startW = jlimit (560, 960, roundToInt (d->userArea.getWidth() * 0.55));
    setResizable (true, true);
    setResizeLimits (designWidth * 30 / 100, designHeight * 30 / 100, designWidth * 3 / 2, designHeight * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (startW, startW * designHeight / designWidth);

    lastTick = Time::getMillisecondCounterHiRes() > 0 ? (int64) Time::getMillisecondCounterHiRes() : 0;
    startTimerHz (30);
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
