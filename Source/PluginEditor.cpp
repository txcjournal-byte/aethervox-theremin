#include "PluginEditor.h"

using namespace juce;

namespace
{
    const Colour folkRed  { 0xffc4261c };
    const Colour folkGold { 0xfff2d27a };
    const Colour folkGreen{ 0xff2e7d3a };
    const Colour cream    { 0xfff6e7c4 };

    // the six knobs on the cabinet
    struct KnobDef { const char* id; const char* tip; };
    const KnobDef kPanelKnobs[6] = {
        { ParamIDs::pitchTune, "Ladění" }, { ParamIDs::glide,  "Glide" },  { ParamIDs::vibDepth, "Vibrato" },
        { ParamIDs::drive,     "Teplo (elektronky)" }, { ParamIDs::revMix, "Dozvuk" }, { ParamIDs::output, "Hlasitost" } };

    // a strip of cross-stitch embroidery: red diamonds, gold dots, green stems
    void drawEmbroidery (Graphics& g, Rectangle<float> r)
    {
        g.setColour (Colour (0xff120a06));
        g.fillRect (r);
        const float h = r.getHeight(), step = h * 2.0f;
        for (float x = r.getX(); x < r.getRight(); x += step)
        {
            const float cx = x + step * 0.5f, cy = r.getCentreY();
            Path d;
            d.addQuadrilateral (cx, cy - h * 0.38f, cx + h * 0.28f, cy, cx, cy + h * 0.38f, cx - h * 0.28f, cy);
            g.setColour (folkRed);
            g.fillPath (d);
            g.setColour (folkGold);
            g.fillEllipse (cx - h * 0.08f, cy - h * 0.08f, h * 0.16f, h * 0.16f);
            g.fillEllipse (x + h * 0.15f, cy - h * 0.30f, h * 0.12f, h * 0.12f);
            g.setColour (folkGreen);
            g.fillRect (x, cy - h * 0.05f, h * 0.4f, h * 0.1f);
        }
    }
}

// =============================================================================
//  ControlStrip
// =============================================================================
ControlStrip::ControlStrip (AudioProcessorValueTreeState& state) : apvts (state)
{
    using namespace ParamIDs;
    order = { choice (playMode, String::fromUTF8 ("Hlasy"), { "Poly", "Mono" }),
              knob (layer, "Vrstva"),
              knob (sub, "Sub"),
              knob (morph, "Vlna"),
              choice (scale, "Stupnice", { "Chrom", "Dur", "Moll", "Penta Dur", "Penta Moll" }),
              knob (quantAmount, String::fromUTF8 ("PŘICHYTIT")),
              knob (delayMix, String::fromUTF8 ("OZVĚNA")),
              knob (delayTime, String::fromUTF8 ("ČAS OZVĚNY")) };
}

Component* ControlStrip::knob (const char* id, const String& caption)
{
    auto k = std::make_unique<LabelledKnob> (apvts, id, caption, folkRed);
    k->label.setColour (Label::textColourId, cream);
    auto* raw = k.get(); addAndMakeVisible (raw); controls.push_back (std::move (k)); return raw;
}

Component* ControlStrip::choice (const char* id, const String& caption, const StringArray& shortNames)
{
    auto k = std::make_unique<LabelledChoice> (apvts, id, caption, folkRed, shortNames);
    k->label.setColour (Label::textColourId, cream);
    auto* raw = k.get(); addAndMakeVisible (raw); controls.push_back (std::move (k)); return raw;
}

void ControlStrip::resized()
{
    auto r = getLocalBounds().withTrimmedTop (22).reduced (0, 8);
    r.removeFromLeft (300);                                  // brand block
    r.removeFromRight (24);
    const int cell = r.getWidth() / (int) order.size();
    for (auto* c : order) c->setBounds (r.removeFromLeft (cell));
}

void ControlStrip::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff3a1f0e), 0.0f, 0.0f, Colour (0xff1c0f07), 0.0f, b.getBottom(), false));
    g.fillRect (b);
    drawEmbroidery (g, b.withHeight (20.0f));

    // brand block
    const auto brand = Rectangle<float> (34.0f, 30.0f, 260.0f, b.getHeight() - 40.0f);
    g.setColour (folkGold.withAlpha (0.85f));
    g.setFont (uiFont (17.0f, true).withExtraKerningFactor (0.45f));
    g.drawText ("TRAPWIN", brand.withHeight (36.0f), Justification::centredLeft);
    g.setColour (cream);
    g.setFont (uiFont (34.0f, true));
    g.drawText (String::fromUTF8 ("Babiččin"), brand.withY (brand.getY() + 36.0f).withHeight (40.0f), Justification::centredLeft);
    g.setColour (folkRed.brighter (0.25f));
    g.drawText ("Theremin", brand.withY (brand.getY() + 74.0f).withHeight (40.0f), Justification::centredLeft);
    g.setColour (cream.withAlpha (0.55f));
    g.setFont (uiFont (14.0f).withExtraKerningFactor (0.15f));
    g.drawText (String::fromUTF8 ("ROK 1929  ·  DRŽ MYŠ A HRAJ"), brand.withY (brand.getY() + 116.0f).withHeight (20.0f), Justification::centredLeft);

    g.setColour (folkGold.withAlpha (0.25f));
    g.fillRect (310.0f, 40.0f, 1.0f, b.getHeight() - 56.0f);
}

// =============================================================================
//  Editor
// =============================================================================
AetherVoxEditor::AetherVoxEditor (AetherVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p), strip (p.apvts)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.setBounds (0, 0, designWidth, designHeight);

    view.setBounds (0, 0, BabickaView::designW, BabickaView::designH);
    body.addAndMakeVisible (view);

    for (int i = 0; i < 6; ++i)
    {
        auto& k = panelKnobs[(size_t) i];
        k.setBounds (Rectangle<int> (72, 72).withCentre ({ roundToInt (Photo::knobX[i] * Photo::S), roundToInt (Photo::knobY * Photo::S) }));
        k.setPopupDisplayEnabled (true, true, &body);
        k.setTooltip (String::fromUTF8 (kPanelKnobs[i].tip));
        body.addAndMakeVisible (k);
        knobAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kPanelKnobs[i].id, k));
        if (auto* prm = p.apvts.getParameter (kPanelKnobs[i].id))
            k.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }

    strip.setBounds (0, BabickaView::designH, designWidth, stripHeight);
    body.addAndMakeVisible (strip);

    // default size: about half the screen width (fits inside the DAW), resizable from the corner
    int startW = 860;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay())
        startW = jlimit (640, 1000, roundToInt (d->userArea.getWidth() * 0.5));
    setResizable (true, true);
    setResizeLimits (designWidth * 35 / 100, designHeight * 35 / 100, designWidth * 3 / 2, designHeight * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designWidth / designHeight);
    setSize (startW, startW * designHeight / designWidth);

    lastTick = (int64) Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

AetherVoxEditor::~AetherVoxEditor()
{
    stopTimer();
    proc.padActive.store (false);
    setLookAndFeel (nullptr);
}

void AetherVoxEditor::timerCallback()
{
    const auto now = (int64) Time::getMillisecondCounterHiRes();
    const float dt = jlimit (0.0f, 0.1f, (float) (now - lastTick) * 0.001f);
    lastTick = now;
    view.tick (dt);
}

void AetherVoxEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void AetherVoxEditor::resized()
{
    body.setTransform (AffineTransform::scale ((float) getWidth() / (float) designWidth));
}
