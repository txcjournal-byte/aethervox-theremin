#include "PluginEditor.h"

namespace omniui
{
// =============================================================================
//  Drawing helpers
// =============================================================================
static void screw (Graphics& g, Point<float> c, float r)
{
    g.setGradientFill (ColourGradient (Colour (0xff8a8e95), c.x - r, c.y - r, Colour (0xff2a2c30), c.x + r, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (Colour (0xff111214)); g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 1.0f);
    Path hex; hex.addPolygon (c, 6, r * 0.45f, 0.3f);
    g.setColour (Colour (0xff18191c)); g.fillPath (hex);
}

static void glowText (Graphics& g, const String& t, Rectangle<float> r, const Font& f, Justification j, Colour c, float glow = 1.0f)
{
    g.setFont (f);
    if (glow > 0.0f)
    {
        g.setColour (c.withAlpha (0.10f * glow));
        for (auto d : { Point<float> (-2, 0), Point<float> (2, 0), Point<float> (0, -2), Point<float> (0, 2) })
            g.drawText (t, r.translated (d.x, d.y), j, false);
        g.setColour (c.withAlpha (0.22f * glow));
        for (auto d : { Point<float> (-1, 0), Point<float> (1, 0), Point<float> (0, -1), Point<float> (0, 1) })
            g.drawText (t, r.translated (d.x, d.y), j, false);
    }
    g.setColour (c);
    g.drawText (t, r, j, false);
}

/** Red mechanical key cap (tilted top face, skirt, glossy legend). */
static void keyCap (Graphics& g, Rectangle<float> r, bool lit, bool pressed, bool dark, const String& label, const String& sub)
{
    const float press = pressed ? 2.5f : 0.0f;
    auto base = r.reduced (1.5f);
    // shadow
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (base.translated (0, 3.0f - press * 0.6f), 5.0f);
    // skirt
    const Colour skirtTop = dark ? Colour (0xff5c0b10) : Colour (0xffa3121b);
    const Colour skirtBot = dark ? Colour (0xff2a0407) : Colour (0xff5e070d);
    g.setGradientFill (ColourGradient (skirtTop, base.getX(), base.getY(), skirtBot, base.getX(), base.getBottom(), false));
    auto skirt = base.translated (0, press);
    g.fillRoundedRectangle (skirt, 5.0f);
    // top face (inset, slightly toward the top = sculpted keycap)
    auto top = skirt.reduced (skirt.getWidth() * 0.09f, skirt.getHeight() * 0.10f).translated (0, -skirt.getHeight() * 0.05f);
    Colour fTop = dark ? Colour (0xff8e1119) : Colour (0xffff3b3b);
    Colour fBot = dark ? Colour (0xff5b080e) : Colour (0xffd8141f);
    if (lit) { fTop = Colour (0xffff8a7a); fBot = Colour (0xffff2d2d); }
    g.setGradientFill (ColourGradient (fTop, top.getX(), top.getY(), fBot, top.getX(), top.getBottom(), false));
    g.fillRoundedRectangle (top, 4.0f);
    // gloss
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (lit ? 0.35f : 0.22f), top.getX(), top.getY(),
                                       Colours::transparentWhite, top.getX(), top.getCentreY(), false));
    g.fillRoundedRectangle (top.withHeight (top.getHeight() * 0.55f), 4.0f);
    // lit halo
    if (lit)
    {
        g.setColour (col::red.withAlpha (0.35f));
        g.drawRoundedRectangle (skirt.expanded (2.0f), 6.0f, 3.0f);
    }
    // legend
    if (label.isNotEmpty())
    {
        g.setColour (lit ? Colour (0xff3a0000) : Colour (0xff2a0004).withAlpha (0.85f));
        g.setFont (OmniLook::sans (jmin (16.0f, top.getHeight() * 0.42f)));
        g.drawText (label, top.withTrimmedBottom (sub.isNotEmpty() ? top.getHeight() * 0.3f : 0.0f), Justification::centred, false);
        if (sub.isNotEmpty())
        {
            g.setFont (OmniLook::sans (jmin (10.0f, top.getHeight() * 0.24f)));
            g.drawText (sub, top.withTrimmedTop (top.getHeight() * 0.62f), Justification::centred, false);
        }
    }
}

// =============================================================================
//  Look and feel
// =============================================================================
OmniLook::OmniLook()
{
    setColour (Slider::textBoxTextColourId, col::silk);
    setColour (TextButton::buttonColourId, col::panel);
    setColour (TextButton::textColourOffId, col::silk);
    setColour (TextButton::textColourOnId, Colours::white);
}

void OmniLook::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, Slider&)
{
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);

    // scale ticks
    for (int i = 0; i <= 10; ++i)
    {
        const float a = a0 + (a1 - a0) * (float) i / 10.0f;
        const auto p1 = c.getPointOnCircumference (r * 0.98f, a), p2 = c.getPointOnCircumference (r * (i % 5 == 0 ? 0.84f : 0.89f), a);
        g.setColour (col::silkDim.withAlpha (0.7f)); g.drawLine ({ p1, p2 }, 1.0f);
    }
    // value arc (neon)
    Path arc; arc.addCentredArc (c.x, c.y, r * 0.80f, r * 0.80f, 0.0f, a0, ang, true);
    g.setColour (col::red.withAlpha (0.25f)); g.strokePath (arc, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (col::red); g.strokePath (arc, PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));

    // machined knob body
    const float kr = r * 0.66f;
    g.setColour (Colours::black.withAlpha (0.6f)); g.fillEllipse (c.x - kr, c.y - kr + 3.0f, kr * 2, kr * 2);
    g.setGradientFill (ColourGradient (Colour (0xff4a4d53), c.x, c.y - kr, Colour (0xff141517), c.x, c.y + kr, false));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    // knurled rim
    g.setColour (Colour (0xff0d0e10));
    for (int i = 0; i < 36; ++i)
    {
        const float a = ang + MathConstants<float>::twoPi * (float) i / 36.0f;
        g.drawLine ({ c.getPointOnCircumference (kr * 0.86f, a), c.getPointOnCircumference (kr, a) }, 1.0f);
    }
    // brushed cap
    const float cr = kr * 0.78f;
    g.setGradientFill (ColourGradient (Colour (0xffc9ccd2), c.x - cr, c.y - cr, Colour (0xff55585e), c.x + cr, c.y + cr, false));
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (Colours::white.withAlpha (0.08f));
    for (float rr = cr * 0.2f; rr < cr; rr += 2.0f) g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, 0.5f);
    // pointer
    const auto p0 = c.getPointOnCircumference (cr * 0.25f, ang), p1 = c.getPointOnCircumference (cr * 0.92f, ang);
    g.setColour (col::red.withAlpha (0.35f)); g.drawLine ({ p0, p1 }, 5.0f);
    g.setColour (col::red); g.drawLine ({ p0, p1 }, 2.4f);
}

void OmniLook::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();
    g.setColour (Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (0, 2), 4.0f);
    g.setGradientFill (ColourGradient (Colour (0xff34363b), r.getX(), r.getY(), Colour (0xff1a1b1e), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r.translated (0, down ? 1.0f : 0.0f), 4.0f);
    // lamp
    auto lamp = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.32f);
    g.setColour (on ? col::red : col::redDark);
    g.fillEllipse (lamp);
    if (on) { g.setColour (col::red.withAlpha (0.3f)); g.fillEllipse (lamp.expanded (4.0f)); }
    if (over) { g.setColour (Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (b.getLocalBounds().toFloat(), 4.0f); }
}

void OmniLook::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    r.removeFromLeft (r.getHeight() * 0.8f);
    g.setColour (b.getToggleState() ? Colours::white : col::silk);
    g.setFont (sans (12.0f));
    g.drawText (b.getButtonText(), r, Justification::centred, false);
}

// =============================================================================
//  Knob
// =============================================================================
Knob::Knob (AudioProcessorValueTreeState& s, const char* id, const String& l, Readout& r, bool b)
    : label (l), att (s, id, slider), param (s.getParameter (id)), readout (r), big (b)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.22f, MathConstants<float>::pi * 2.78f, true);
    slider.setMouseDragSensitivity (180);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.addListener (this);
    addAndMakeVisible (slider);
}
void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (16);
    slider.setBounds (r);
}
void Knob::paint (Graphics& g)
{
    g.setColour (col::silk);
    g.setFont (OmniLook::sans (big ? 11.5f : 10.5f));
    g.drawText (label, getLocalBounds().removeFromBottom (15), Justification::centred, false);
}
void Knob::sliderDragStarted (Slider*) { if (readout.show) readout.show (label, param->getCurrentValueAsText()); }
void Knob::sliderValueChanged (Slider*) { if (readout.show) readout.show (label, param->getCurrentValueAsText()); }

LampButton::LampButton (AudioProcessorValueTreeState& s, const char* id, const String& text)
    : TextButton (text), att (s, id, *this)
{
    setClickingTogglesState (true);
}

// =============================================================================
//  Selector
// =============================================================================
Selector::Selector (AudioProcessorValueTreeState& s, const char* id, const StringArray& n, Readout& r, int cols)
    : param (s.getParameter (id)), names (n), readout (r), columns (cols > 0 ? cols : n.size())
{
    startTimerHz (20);
}
void Selector::resized() {}
Rectangle<float> Selector::cell (int i) const
{
    const int rows = (names.size() + columns - 1) / columns;
    const float cw = (float) getWidth() / (float) columns, ch = (float) getHeight() / (float) rows;
    return { (float) (i % columns) * cw, (float) (i / columns) * ch, cw, ch };
}
void Selector::paint (Graphics& g)
{
    const int cur = current();
    for (int i = 0; i < names.size(); ++i)
    {
        auto r = cell (i).reduced (2.0f);
        const bool on = i == cur;
        g.setColour (Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (r.translated (0, 1.5f), 3.0f);
        g.setGradientFill (on ? ColourGradient (Colour (0xffff4a3d), r.getX(), r.getY(), Colour (0xffb5121b), r.getX(), r.getBottom(), false)
                              : ColourGradient (Colour (0xff303237), r.getX(), r.getY(), Colour (0xff1a1b1e), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 3.0f);
        if (on) { g.setColour (col::red.withAlpha (0.3f)); g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 2.0f); }
        g.setColour (on ? Colour (0xff2a0003) : col::silk);
        g.setFont (OmniLook::sans (jmin (12.0f, r.getHeight() * 0.55f)));
        g.drawText (names[i], r, Justification::centred, false);
    }
}
void Selector::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < names.size(); ++i)
        if (cell (i).contains (e.position))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) i));
            param->endChangeGesture();
            if (readout.show) readout.show (param->getName (32).toUpperCase(), names[i]);
            repaint();
            return;
        }
}

// =============================================================================
//  Display
// =============================================================================
Display::Display (OmniProcessor& p) : proc (p) { startTimerHz (30); }

void Display::showReadout (const String& n, const String& v)
{
    roName = n.toUpperCase(); roValue = v.toUpperCase();
    roUntil = Time::getMillisecondCounterHiRes() + 1400.0;
}

void Display::timerCallback()
{
    for (int i = 0; i < 128; ++i) scope[i] = proc.dispScope[(size_t) i].load();
    boot = jmin (1.0f, boot + 0.04f);
    repaint();
}

void Display::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // bezel
    g.setGradientFill (ColourGradient (Colour (0xff0b0b0c), r.getX(), r.getY(), Colour (0xff3a3c41), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 10.0f);
    auto glass = r.reduced (9.0f);
    g.setColour (col::glass); g.fillRoundedRectangle (glass, 6.0f);
    g.setGradientFill (ColourGradient (Colour (0x30ff2a2a), glass.getCentreX(), glass.getCentreY(), Colours::transparentBlack, glass.getX(), glass.getY(), true));
    g.fillRoundedRectangle (glass, 6.0f);

    auto in = glass.reduced (14.0f, 10.0f);
    const auto ph = col::phosphor.withAlpha (boot);
    const auto dim = col::phosphor.withAlpha (0.38f * boot);

    // header row
    const int mode = roundToInt (proc.apvts.getRawParameterValue (PID::mode)->load());
    auto head = in.removeFromTop (18.0f);
    glowText (g, "MODE " + Params::modeNames[mode], head, OmniLook::mono (13.0f), Justification::centredLeft, ph, 0.6f);
    glowText (g, String (roundToInt (proc.dispBpm.load())) + " BPM   " + String (proc.dispVoices.load()).paddedLeft ('0', 2) + " VOX",
              head, OmniLook::mono (13.0f), Justification::centredRight, ph, 0.6f);

    auto scopeArea = in.removeFromBottom (54.0f);
    auto arpRow = in.removeFromBottom (14.0f);

    const bool ro = Time::getMillisecondCounterHiRes() < roUntil;
    if (ro)
    {
        glowText (g, roName, in.removeFromTop (in.getHeight() * 0.38f), OmniLook::mono (19.0f), Justification::centredLeft, dim, 0.5f);
        glowText (g, roValue, in, OmniLook::mono (44.0f), Justification::centredLeft, ph, 1.2f);
    }
    else
    {
        const int root = proc.dispRoot.load(), type = proc.dispType.load();
        String name = root < 0 ? String ("--") : String (oc::ChordEngine::pcName (root)) + oc::ChordEngine::typeName (type);
        glowText (g, name, in.withTrimmedRight (in.getWidth() * 0.35f), OmniLook::mono (66.0f), Justification::centredLeft, ph, 1.4f);
        auto side = in.removeFromRight (in.getWidth() * 0.35f);
        glowText (g, "OSC A/B", side.removeFromTop (side.getHeight() / 3), OmniLook::mono (11.0f), Justification::centredRight, dim, 0.4f);
        const int w = roundToInt (proc.apvts.getRawParameterValue (PID::oscBWave)->load());
        glowText (g, Params::waveNames[w], side.removeFromTop (side.getHeight() / 2), OmniLook::mono (14.0f), Justification::centredRight, ph, 0.6f);
        glowText (g, "KEY " + Params::chordTypeNames[roundToInt (proc.apvts.getRawParameterValue (PID::chordType)->load())], side,
                  OmniLook::mono (12.0f), Justification::centredRight, dim, 0.4f);
    }

    // arp step lights
    if (mode == 3)
    {
        const int step = proc.dispArpStep.load();
        const float cw = arpRow.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            auto c = Rectangle<float> (arpRow.getX() + i * cw + 2, arpRow.getY() + 3, cw - 4, 7);
            g.setColour (((step - 1) & 15) == i ? ph : dim.withAlpha (0.18f)); g.fillRect (c);
        }
    }

    // oscilloscope
    Path p;
    for (int i = 0; i < 128; ++i)
    {
        const float x = scopeArea.getX() + scopeArea.getWidth() * i / 127.0f;
        const float y = scopeArea.getCentreY() - jlimit (-1.0f, 1.0f, scope[i] * 2.2f) * scopeArea.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (ph.withAlpha (0.18f)); g.strokePath (p, PathStrokeType (4.0f));
    g.setColour (ph); g.strokePath (p, PathStrokeType (1.4f));
    g.setColour (dim.withAlpha (0.15f));
    g.drawHorizontalLine ((int) scopeArea.getCentreY(), scopeArea.getX(), scopeArea.getRight());

    // scanlines + glass reflection
    g.setColour (Colours::black.withAlpha (0.22f));
    for (float y = glass.getY(); y < glass.getBottom(); y += 3.0f) g.drawHorizontalLine ((int) y, glass.getX(), glass.getRight());
    Path refl; refl.addRoundedRectangle (glass.withHeight (glass.getHeight() * 0.42f), 6.0f);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.07f), glass.getX(), glass.getY(), Colours::transparentWhite, glass.getX(), glass.getY() + glass.getHeight() * 0.42f, false));
    g.fillPath (refl);
}

// =============================================================================
//  Key field (chord buttons / keyboard)
// =============================================================================
KeyField::KeyField (OmniProcessor& p, Kind k) : proc (p), kind (k) { startTimerHz (30); }

void KeyField::layout()
{
    caps.clear();
    const auto b = getLocalBounds().toFloat();
    if (kind == ChordButtons)
    {
        // Omnichord order: circle of fifths, Db .. F#
        static const int order[12] = { 1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6 };
        static const char* rowSub[3] = { "MAJ", "MIN", "7TH" };
        const float cw = b.getWidth() / 12.0f, ch = b.getHeight() / 3.0f;
        for (int row = 0; row < 3; ++row)
            for (int i = 0; i < 12; ++i)
            {
                const int pc = order[i];
                String lab = oc::ChordEngine::pcName (pc);
                if (row == 1) lab << "m"; else if (row == 2) lab << "7";
                caps.push_back ({ { b.getX() + i * cw, b.getY() + row * ch, cw, ch }, OmniProcessor::chordChannel, row * 12 + pc, lab, rowSub[row], false });
            }
    }
    else
    {
        // C3 .. C5 (two octaves + top C)
        const int first = 48, last = 72;
        int whites = 0; for (int n = first; n <= last; ++n) if (! MidiMessage::isMidiNoteBlack (n)) ++whites;
        const float ww = b.getWidth() / (float) whites;
        int wi = 0;
        std::vector<Cap> blacks;
        for (int n = first; n <= last; ++n)
        {
            const String name = MidiMessage::getMidiNoteName (n, true, n % 12 == 0, 4);
            if (! MidiMessage::isMidiNoteBlack (n))
            {
                caps.push_back ({ { b.getX() + wi * ww, b.getY(), ww, b.getHeight() }, 1, n, n % 12 == 0 ? name : String(), {}, false });
                ++wi;
            }
            else
                blacks.push_back ({ { b.getX() + wi * ww - ww * 0.32f, b.getY(), ww * 0.64f, b.getHeight() * 0.58f }, 1, n, {}, {}, true });
        }
        for (auto& c : blacks) caps.push_back (c);     // drawn last = on top
    }
    litCache.assign (caps.size(), false);
}

int KeyField::hit (Point<float> p) const
{
    for (int i = (int) caps.size() - 1; i >= 0; --i) if (caps[(size_t) i].r.contains (p)) return i;
    return -1;
}

void KeyField::press (int idx, float vel)
{
    if (idx == down) return;
    releaseCurrent();
    if (idx < 0) return;
    down = idx;
    const auto& c = caps[(size_t) idx];
    proc.keyboardState.noteOn (c.channel, c.note, vel);
    repaint();
}

void KeyField::releaseCurrent()
{
    if (down < 0) return;
    const auto& c = caps[(size_t) down];
    proc.keyboardState.noteOff (c.channel, c.note, 0.0f);
    down = -1;
    repaint();
}

void KeyField::mouseDown (const MouseEvent& e)
{
    const int i = hit (e.position);
    if (i < 0) return;
    const auto& r = caps[(size_t) i].r;
    const float vel = jlimit (0.35f, 1.0f, 0.45f + 0.6f * (e.position.y - r.getY()) / r.getHeight());
    press (i, vel);
}
void KeyField::mouseDrag (const MouseEvent& e) { const int i = hit (e.position); if (i >= 0) press (i, 0.8f); }
void KeyField::mouseUp (const MouseEvent&) { releaseCurrent(); }

bool KeyField::lit (const Cap& c) const
{
    if (kind == Keyboard) return proc.noteLit[(size_t) c.note].load();
    const int root = proc.dispRoot.load(), type = proc.dispType.load();
    const int row = c.note / 12, pc = c.note % 12;
    const int rowType = row == 1 ? 1 : (row == 2 ? 2 : 0);
    return root == pc && type == rowType;
}

void KeyField::timerCallback()
{
    bool changed = false;
    for (size_t i = 0; i < caps.size(); ++i)
    {
        const bool l = lit (caps[i]);
        if (l != litCache[i]) { litCache[i] = l; changed = true; }
    }
    if (changed) repaint();
}

void KeyField::paint (Graphics& g)
{
    for (size_t i = 0; i < caps.size(); ++i)
    {
        const auto& c = caps[i];
        auto r = c.r;
        if (kind == Keyboard && ! c.black) r = r.withTrimmedTop (0.0f);
        keyCap (g, r, litCache.size() > i && litCache[i], (int) i == down, c.black || (kind == ChordButtons && c.note / 12 == 2),
                c.label, kind == ChordButtons ? String() : c.sub);
    }
}

// =============================================================================
//  Strum plate
// =============================================================================
StrumPlate::StrumPlate (OmniProcessor& p) : proc (p) { startTimerHz (40); }

int StrumPlate::stringAt (float y) const
{
    const float top = 34.0f, h = (float) getHeight() - 48.0f;
    const float t = jlimit (0.0f, 0.999f, (y - top) / h);
    return oc::ChordEngine::numStrings - 1 - (int) (t * oc::ChordEngine::numStrings);   // low strings at the bottom
}

void StrumPlate::pluckTo (int s, float speed)
{
    if (lastString < 0) lastString = s;
    const int step = s >= lastString ? 1 : -1;
    for (int k = lastString; ; k += step)
    {
        if (k != lastString || speed == 0.0f)
        {
            const float vel = jlimit (0.3f, 1.0f, 0.55f + speed * 0.4f);
            proc.keyboardState.noteOn (OmniProcessor::stringChannel, k, vel);
            proc.keyboardState.noteOff (OmniProcessor::stringChannel, k, 0.0f);
            glow[(size_t) k] = 1.0f;
        }
        if (k == s) break;
    }
    lastString = s;
}

void StrumPlate::mouseDown (const MouseEvent& e)
{
    lastString = -1;
    lastTime = Time::getMillisecondCounterHiRes(); lastY = e.position.y;
    pluckTo (stringAt (e.position.y), 0.0f);
}

void StrumPlate::mouseDrag (const MouseEvent& e)
{
    const int s = stringAt (e.position.y);
    if (s == lastString) return;
    const double now = Time::getMillisecondCounterHiRes();
    const float speed = (float) jlimit (0.0, 2.0, std::abs (e.position.y - lastY) / jmax (1.0, now - lastTime) * 0.6);
    lastTime = now; lastY = e.position.y;
    pluckTo (s, speed);
}

void StrumPlate::timerCallback()
{
    bool any = false;
    for (auto& v : glow) if (v > 0.001f) { v *= 0.88f; any = true; }
    if (any) repaint();
}

void StrumPlate::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (ColourGradient (Colour (0xff0d0e10), r.getX(), r.getY(), Colour (0xff24262a), r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colour (0xff3c3f45)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::silk); g.setFont (OmniLook::sans (11.0f));
    g.drawText ("SONIC STRINGS", r.removeFromTop (28.0f), Justification::centred, false);
    const float top = 34.0f, h = (float) getHeight() - 48.0f;
    const bool hasChord = proc.dispRoot.load() >= 0;
    for (int k = 0; k < oc::ChordEngine::numStrings; ++k)
    {
        const float y = top + h * (1.0f - (k + 0.5f) / oc::ChordEngine::numStrings);
        const float gl = glow[(size_t) k];
        g.setColour (col::red.withAlpha (0.10f + 0.5f * gl)); g.fillRect (10.0f, y - 3.0f - 3 * gl, r.getWidth() - 20.0f, 6.0f + 6 * gl);
        g.setColour (hasChord ? col::red.withAlpha (0.55f + 0.45f * gl) : col::redDark);
        g.fillRect (10.0f, y - 0.8f, r.getWidth() - 20.0f, 1.6f);
    }
    g.setColour (col::silkDim); g.setFont (OmniLook::sans (9.5f, false));
    g.drawText (hasChord ? "SWIPE TO STRUM" : "HOLD A CHORD", getLocalBounds().removeFromBottom (14), Justification::centred, false);
}

// =============================================================================
//  Pyramid eye
// =============================================================================
PyramidEye::PyramidEye (OmniProcessor& p) : proc (p) { startTimerHz (40); setMouseCursor (MouseCursor::PointingHandCursor); }

Rectangle<float> PyramidEye::eyeBounds() const
{
    const float w = (float) getWidth(), h = (float) getHeight();
    return { w * 0.30f, h * 0.30f, w * 0.30f, h * 0.17f };
}

void PyramidEye::buildStone()
{
    stone = Image (Image::ARGB, jmax (1, getWidth()), jmax (1, getHeight()), true);
    Graphics g (stone);
    Random r (1929);
    g.fillAll (Colour (0xff77777a));
    for (int i = 0; i < getWidth() * getHeight() / 3; ++i)
    {
        const float x = r.nextFloat() * getWidth(), y = r.nextFloat() * getHeight();
        const float v = r.nextFloat();
        const uint8 c = (uint8) (60 + v * 120);
        g.setColour (Colour (c, c, (uint8) jmin (255, c + 3)).withAlpha (0.55f));
        const float sz = 0.6f + r.nextFloat() * (v > 0.95f ? 3.2f : 1.4f);
        g.fillEllipse (x, y, sz, sz);
    }
}

void PyramidEye::timerCallback()
{
    // gaze follows the mouse anywhere on screen
    const auto m = getLocalPoint (nullptr, Desktop::getMousePosition().toFloat());
    const auto c = eyeBounds().getCentre();
    auto d = m - c;
    const float len = std::sqrt (d.x * d.x + d.y * d.y);
    gazeTarget = len > 1.0f ? d * (jmin (1.0f, len / 260.0f) / len) : Point<float>();
    gaze += (gazeTarget - gaze) * 0.18f;

    const float pk = proc.dispPeak.load();
    pulse += (jlimit (0.0f, 1.0f, pk * 3.0f) - pulse) * 0.25f;
    flash *= 0.9f;

    const double now = Time::getMillisecondCounterHiRes();
    if (now > nextBlink) { blink = 1.0f; nextBlink = now + 2500.0 + Random::getSystemRandom().nextDouble() * 5000.0; }
    blink = jmax (0.0f, blink - 0.12f);
    repaint();
}

void PyramidEye::mouseDown (const MouseEvent&)
{
    flash = 1.0f; blink = 1.0f;
    if (onMutate) onMutate();
}

void PyramidEye::paint (Graphics& g)
{
    if (stone.isNull()) buildStone();
    const float w = (float) getWidth(), h = (float) getHeight();
    const Point<float> apex (w * 0.52f, h * 0.03f), bl (w * 0.03f, h * 0.86f), bm (w * 0.60f, h * 0.97f), br (w * 0.97f, h * 0.78f);

    // ground shadow
    g.setColour (Colours::black.withAlpha (0.55f));
    g.fillEllipse (w * 0.04f, h * 0.84f, w * 0.94f, h * 0.15f);
    // red underglow from the eye
    g.setGradientFill (ColourGradient (col::red.withAlpha (0.10f + 0.25f * pulse + 0.3f * flash), w * 0.45f, h * 0.9f,
                                       Colours::transparentBlack, w * 0.45f, h * 0.55f, true));
    g.fillEllipse (0, h * 0.62f, w, h * 0.38f);

    Path left;  left.startNewSubPath (apex);  left.lineTo (bl); left.lineTo (bm); left.closeSubPath();
    Path right; right.startNewSubPath (apex); right.lineTo (bm); right.lineTo (br); right.closeSubPath();

    // stone faces: same granite, lit from the upper left
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (left);
        g.drawImageAt (stone, 0, 0);
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.10f), apex.x, apex.y, Colours::black.withAlpha (0.25f), bl.x, bl.y, false));
        g.fillPath (left);
    }
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (right);
        g.drawImageAt (stone, 0, 0);
        g.setColour (Colours::black.withAlpha (0.38f)); g.fillPath (right);
        // carved relief: wedge script + a striding guardian figure
        g.setColour (Colours::black.withAlpha (0.45f));
        Random rr (77);
        for (int row = 0; row < 9; ++row)
            for (int k = 0; k < 7; ++k)
            {
                const float t = (float) row / 9.0f;
                const float x = bm.x + (br.x - bm.x) * (0.18f + 0.1f * k) * (1.0f - t * 0.7f) + 6.0f;
                const float y = h * (0.40f + 0.05f * row) - (float) k * 3.0f;
                if (rr.nextFloat() < 0.55f)
                {
                    Path wedge; wedge.addTriangle (x, y, x + 6, y + 1.5f, x + 1, y + 5);
                    g.fillPath (wedge);
                    g.setColour (Colours::white.withAlpha (0.07f)); g.drawLine (x, y, x + 6, y + 1.5f, 0.8f);
                    g.setColour (Colours::black.withAlpha (0.45f));
                }
            }
        Path fig;                                                   // guardian silhouette (simplified relief)
        const float fx = w * 0.80f, fy = h * 0.42f, fh = h * 0.34f;
        fig.addRoundedRectangle (fx - 8, fy, 16, fh * 0.22f, 6.0f);              // head + crown
        fig.addRoundedRectangle (fx - 13, fy + fh * 0.22f, 26, fh * 0.45f, 4.0f); // robe
        fig.addRectangle (fx - 11, fy + fh * 0.67f, 7, fh * 0.30f);                // legs
        fig.addRectangle (fx + 3, fy + fh * 0.67f, 7, fh * 0.30f);
        fig.addRoundedRectangle (fx - 34, fy + fh * 0.25f, 24, 6, 3.0f);          // reaching arm
        g.setColour (Colours::black.withAlpha (0.35f)); g.fillPath (fig, AffineTransform::translation (2, 2));
        g.setColour (Colours::white.withAlpha (0.10f)); g.fillPath (fig);
    }
    // edges
    g.setColour (Colours::white.withAlpha (0.18f)); g.drawLine ({ apex, bm }, 1.4f);
    g.setColour (Colours::black.withAlpha (0.5f)); g.strokePath (left, PathStrokeType (1.0f)); g.strokePath (right, PathStrokeType (1.0f));

    // --- the eye
    auto e = eyeBounds();
    const float open = 1.0f - jlimit (0.0f, 1.0f, blink > 0.5f ? (1.0f - blink) * 2.0f : blink * 2.0f) * 0.92f;
    Path socket;
    socket.startNewSubPath (e.getX() - 6, e.getCentreY());
    socket.quadraticTo (e.getCentreX(), e.getY() - 12, e.getRight() + 6, e.getCentreY());
    socket.quadraticTo (e.getCentreX(), e.getBottom() + 12, e.getX() - 6, e.getCentreY());
    g.setColour (Colours::black.withAlpha (0.75f)); g.fillPath (socket);
    g.setColour (Colours::white.withAlpha (0.12f)); g.strokePath (socket, PathStrokeType (1.2f), AffineTransform::translation (0, 1.5f));

    Path almond;
    const float lidTop = e.getCentreY() - (e.getCentreY() - e.getY() + 6) * open;
    const float lidBot = e.getCentreY() + (e.getBottom() - e.getCentreY() + 6) * open;
    almond.startNewSubPath (e.getX(), e.getCentreY());
    almond.quadraticTo (e.getCentreX(), lidTop - 6, e.getRight(), e.getCentreY());
    almond.quadraticTo (e.getCentreX(), lidBot + 6, e.getX(), e.getCentreY());
    almond.closeSubPath();

    // outer glow
    const float glowA = 0.25f + 0.45f * pulse + 0.4f * flash;
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (col::red.withAlpha (glowA * 0.12f));
        g.strokePath (almond, PathStrokeType ((float) i * 5.0f));
    }
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (almond);
        // red glass iris
        const auto ic = e.getCentre() + gaze * Point<float> (e.getWidth() * 0.18f, e.getHeight() * 0.25f);
        g.setGradientFill (ColourGradient (Colour (0xffff6a50).interpolatedWith (Colours::white, 0.2f * flash), ic.x - 8, ic.y - 10,
                                           Colour (0xff6e0008), ic.x + e.getWidth() * 0.55f, ic.y + e.getHeight() * 0.6f, true));
        g.fillRect (e.expanded (10));
        // fibres
        g.setColour (Colour (0xffff9a80).withAlpha (0.18f));
        for (int i = 0; i < 40; ++i)
        {
            const float a = MathConstants<float>::twoPi * i / 40.0f;
            g.drawLine ({ ic.getPointOnCircumference (6.0f, a), ic.getPointOnCircumference (e.getWidth() * 0.45f, a) }, 0.7f);
        }
        // slit pupil (dilates with the sound)
        const float pw = e.getWidth() * (0.045f + 0.06f * pulse), ph = e.getHeight() * 1.05f;
        g.setColour (Colour (0xff120002));
        g.fillEllipse (ic.x - pw * 0.5f, ic.y - ph * 0.5f, pw, ph);
        g.setColour (Colours::black);
        g.fillEllipse (ic.x - pw * 0.3f, ic.y - ph * 0.45f, pw * 0.6f, ph * 0.9f);
        // glass highlight
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.65f), e.getX() + e.getWidth() * 0.32f, e.getY(),
                                           Colours::transparentWhite, e.getX() + e.getWidth() * 0.32f, e.getCentreY(), false));
        g.fillEllipse (e.getX() + e.getWidth() * 0.18f, e.getY() - 2.0f, e.getWidth() * 0.34f, e.getHeight() * 0.42f);
    }
    g.setColour (Colours::black.withAlpha (0.8f)); g.strokePath (almond, PathStrokeType (1.6f));

    // legend
    g.setColour (col::silkDim.withAlpha (0.9f)); g.setFont (OmniLook::sans (9.5f));
    g.drawText (flash > 0.2f ? "MUTATED" : "CLICK THE EYE TO MUTATE", Rectangle<float> (0, h - 16, w, 14), Justification::centred, false);
}

// =============================================================================
//  Front panel
// =============================================================================
FrontPanel::FrontPanel (OmniProcessor& p)
    : proc (p), display (p), eye (p), chordKeys (p, KeyField::ChordButtons), keyboard (p, KeyField::Keyboard), plate (p)
{
    readout.show = [this] (const String& n, const String& v) { display.showReadout (n, v); };
    addAndMakeVisible (display);
    addAndMakeVisible (eye);
    eye.onMutate = [this] { proc.mutate(); display.showReadout ("THE EYE", "MUTATED"); };
    auto& s = p.apvts;

    auto add = [&] (const char* id, const char* label, Rectangle<int> at, bool big = false)
    {
        auto* k = knobs.add (new Knob (s, id, label, readout, big));
        addAndMakeVisible (k);
        knobSpots.push_back ({ k, at });
    };

    // --- COSMOS (826,76 418x248)
    sections.push_back ({ { 826, 76, 418, 248 }, "COSMOS" });
    {
        const int x = 834, y = 104, kw = 100, kh = 104;
        add (PID::shimmer,   "SHIMMER",   { x + 0 * kw, y, kw, kh }, true);
        add (PID::warpMix,   "WARP",      { x + 1 * kw, y, kw, kh }, true);
        add (PID::mutAmt,    "MUTANT",    { x + 2 * kw, y, kw, kh }, true);
        add (PID::orbitMix,  "ORBIT",     { x + 3 * kw, y, kw, kh }, true);
        add (PID::spcSize,   "SPACE SIZE",{ x + 0 * kw, y + kh + 6, kw, kh });
        add (PID::warpHz,    "WARP HZ",   { x + 1 * kw, y + kh + 6, kw, kh });
        add (PID::mutRate,   "MUT RATE",  { x + 2 * kw, y + kh + 6, kw, kh });
        add (PID::orbitRate, "ORBIT RATE",{ x + 3 * kw, y + kh + 6, kw, kh });
    }

    // --- OSC (36,346 420x232)
    sections.push_back ({ { 36, 346, 420, 232 }, "OSCILLATORS" });
    {
        const int x = 42, y = 374, kw = 102, kh = 98;
        add (PID::oscA,     "A SHAPE", { x + 0 * kw, y, kw, kh }, true);
        add (PID::oscBWave, "B 8-BIT", { x + 1 * kw, y, kw, kh }, true);
        add (PID::oscBBits, "B BITS",  { x + 2 * kw, y, kw, kh });
        add (PID::oscBSemi, "B PITCH", { x + 3 * kw, y, kw, kh });
        add (PID::oscMix,   "MIX A/B", { x + 0 * kw, y + kh + 4, kw, kh }, true);
        add (PID::detune,   "DRIFT",   { x + 1 * kw, y + kh + 4, kw, kh });
        add (PID::noise,    "NOISE",   { x + 2 * kw, y + kh + 4, kw, kh });
        add (PID::glide,    "GLIDE",   { x + 3 * kw, y + kh + 4, kw, kh });
    }

    // --- FILTER + ENV (466,346 300x232)
    sections.push_back ({ { 466, 346, 300, 232 }, "FILTER  /  AMP ENV" });
    {
        const int x = 472, y = 374, kw = 72, kh = 98;
        add (PID::cutoff, "CUTOFF",  { x + 0 * kw, y, kw, kh }, true);
        add (PID::reso,   "RESO",    { x + 1 * kw, y, kw, kh });
        add (PID::envAmt, "ENV",     { x + 2 * kw, y, kw, kh });
        add (PID::drive,  "WARMTH",  { x + 3 * kw, y, kw, kh });
        add (PID::attack, "ATTACK",  { x + 0 * kw, y + kh + 4, kw, kh });
        add (PID::decay,  "DECAY",   { x + 1 * kw, y + kh + 4, kw, kh });
        add (PID::sustain,"SUSTAIN", { x + 2 * kw, y + kh + 4, kw, kh });
        add (PID::release,"RELEASE", { x + 3 * kw, y + kh + 4, kw, kh });
    }

    // --- PLAY (776,346 468x232)
    sections.push_back ({ { 776, 346, 468, 232 }, "PLAY" });
    mode      = std::make_unique<Selector> (s, PID::mode, Params::modeNames, readout);
    chordType = std::make_unique<Selector> (s, PID::chordType, Params::chordTypeNames, readout);
    arpRate   = std::make_unique<Selector> (s, PID::arpRate, Params::arpRateNames, readout);
    arpPat    = std::make_unique<Selector> (s, PID::arpPat, Params::arpPatNames, readout);
    hold      = std::make_unique<LampButton> (s, PID::hold, "HOLD");
    bass      = std::make_unique<LampButton> (s, PID::bass, "BASS");
    for (Component* c : std::initializer_list<Component*> { mode.get(), chordType.get(), arpRate.get(), arpPat.get(), hold.get(), bass.get() })
        addAndMakeVisible (c);
    {
        const int x = 784, y = 494, kw = 75, kh = 80;
        add (PID::arpOct,  "ARP OCT",  { x + 0 * kw, y, kw, kh });
        add (PID::arpGate, "ARP GATE", { x + 1 * kw, y, kw, kh });
        add (PID::strumMs, "STRUM",    { x + 2 * kw, y, kw, kh });
        add (PID::pluck,   "PLUCK",    { x + 3 * kw, y, kw, kh });
        add (PID::fDecay,  "F.DECAY",  { x + 4 * kw, y, kw, kh });
        add (PID::keyTrack,"KEYTRK",   { x + 5 * kw, y, kw, kh });
    }

    // --- FX (36,594 1208x124): one row, grouped
    sections.push_back ({ { 36, 594, 1208, 124 }, "TAPE          /          CRUSH          /          DELAY          /          SPACE" });
    {
        const int x = 44, y = 618, kw = 79, kh = 94;
        int i = 0;
        auto f = [&] (const char* id, const char* lab, bool big = false) { add (id, lab, { x + (i++) * kw, y, kw, kh }, big); };
        f (PID::tapeMix, "TAPE MIX", true); f (PID::tapeRate, "RATE"); f (PID::tapeDepth, "DEPTH"); f (PID::tapeWow, "WOW");
        f (PID::crushMix, "CRUSH", true); f (PID::crushBits, "BITS"); f (PID::crushDown, "SR");
        f (PID::dlyMix, "DELAY", true); f (PID::dlySync, "TIME"); f (PID::dlyFb, "FEEDBACK"); f (PID::dlyTone, "TONE");
        f (PID::spcMix, "SPACE", true); f (PID::spcDamp, "DAMP");
        f (PID::output, "OUTPUT", true);
    }

    addAndMakeVisible (chordKeys);
    addAndMakeVisible (keyboard);
    addAndMakeVisible (plate);
}

void FrontPanel::resized()
{
    display.setBounds (36, 76, 420, 248);
    eye.setBounds (466, 66, 350, 266);
    for (auto& [k, r] : knobSpots) k->setBounds (r);
    mode->setBounds      (786, 372, 270, 30);
    hold->setBounds      (1062, 372, 84, 30);
    bass->setBounds      (1152, 372, 84, 30);
    chordType->setBounds (786, 406, 450, 30);
    arpRate->setBounds   (786, 440, 250, 30);
    arpPat->setBounds    (1042, 440, 194, 30);
    chordKeys.setBounds  (36, 744, 1046, 150);
    keyboard.setBounds   (36, 900, 1046, 88);
    plate.setBounds      (1096, 744, 148, 244);
}

void FrontPanel::section (Graphics& g, Rectangle<int> r, const String& title) const
{
    auto f = r.toFloat();
    g.setColour (col::engrave); g.fillRoundedRectangle (f.translated (0, 1.5f), 8.0f);
    g.setGradientFill (ColourGradient (Colour (0xff222428), f.getX(), f.getY(), Colour (0xff1a1b1f), f.getX(), f.getBottom(), false));
    g.fillRoundedRectangle (f, 8.0f);
    g.setColour (Colour (0xff35383e)); g.drawRoundedRectangle (f.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (col::silkDim); g.setFont (OmniLook::sans (10.5f));
    g.drawText (title, f.reduced (12.0f, 6.0f).removeFromTop (14.0f), Justification::centredLeft, false);
    g.setColour (col::red.withAlpha (0.6f)); g.fillRect (f.getX() + 12.0f, f.getY() + 22.0f, 18.0f, 1.5f);
}

void FrontPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // chassis
    g.setGradientFill (ColourGradient (col::chassisTop, 0, 0, col::chassisBot, 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 18.0f);
    // brushed metal streaks
    Random rnd (3000);
    for (int i = 0; i < 260; ++i)
    {
        const float y = rnd.nextFloat() * r.getHeight();
        g.setColour (Colours::white.withAlpha (0.012f + rnd.nextFloat() * 0.018f));
        g.drawHorizontalLine ((int) y, rnd.nextFloat() * 200.0f, r.getRight() - rnd.nextFloat() * 200.0f);
    }
    g.setColour (Colours::white.withAlpha (0.06f)); g.drawRoundedRectangle (r.reduced (1.0f), 18.0f, 1.5f);
    g.setColour (Colours::black.withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (4.0f), 15.0f, 1.0f);
    for (auto c : { Point<float> (18, 18), Point<float> (r.getRight() - 18, 18), Point<float> (18, r.getBottom() - 18), Point<float> (r.getRight() - 18, r.getBottom() - 18) })
        screw (g, c, 7.0f);

    // brand
    glowText (g, "OMNICORE 3000", { 40, 18, 420, 40 }, OmniLook::sans (32.0f), Justification::centredLeft, col::red, 1.0f);
    g.setColour (col::silk); g.setFont (OmniLook::sans (11.0f));
    g.drawText ("ADVANCED CHORD COMPUTING", Rectangle<float> (330, 22, 300, 16), Justification::centredLeft, false);
    g.setColour (col::silkDim); g.setFont (OmniLook::sans (10.0f, false));
    g.drawText ("RETRO-FUTURE SYNTH WORKSTATION  /  MODEL OC-3000", Rectangle<float> (330, 38, 400, 14), Justification::centredLeft, false);
    g.setColour (col::silk); g.setFont (OmniLook::sans (11.0f));
    g.drawText ("ARTIFICIAL HARMONY ENGINE", Rectangle<float> (900, 22, 344, 16), Justification::centredRight, false);
    g.setColour (col::silkDim); g.setFont (OmniLook::sans (10.0f, false));
    g.drawText ("TRAPWIN  -  SERIAL 3000-" + String (1977), Rectangle<float> (900, 38, 344, 14), Justification::centredRight, false);
    // power lamp
    g.setColour (col::red.withAlpha (0.3f)); g.fillEllipse (1070, 23, 14, 14);
    g.setColour (col::red); g.fillEllipse (1073, 26, 8, 8);

    for (auto& [rr, t] : sections) section (g, rr, t);

    // keyboard bed + labels
    auto bed = Rectangle<float> (28, 734, 1224, 260);
    g.setColour (Colour (0xff0b0c0e)); g.fillRoundedRectangle (bed, 12.0f);
    g.setColour (Colour (0xff2e3136)); g.drawRoundedRectangle (bed, 12.0f, 1.0f);
    g.setColour (col::silkDim); g.setFont (OmniLook::sans (9.5f));
    g.drawText ("CHORD BUTTONS  -  CIRCLE OF FIFTHS", Rectangle<float> (40, 722, 400, 10), Justification::centredLeft, false);
}

} // namespace omniui

// =============================================================================
//  Editor: scales the fixed front panel into the window
// =============================================================================
OmniEditor::OmniEditor (OmniProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (panel);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) omniui::FrontPanel::W / omniui::FrontPanel::H);
    setResizeLimits (640, 430, 2560, 1720);
    setSize (1152, 774);
}

OmniEditor::~OmniEditor() { setLookAndFeel (nullptr); }

void OmniEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff0a0a0b)); }

void OmniEditor::resized()
{
    const float s = (float) getWidth() / (float) omniui::FrontPanel::W;
    panel.setBounds (0, 0, omniui::FrontPanel::W, omniui::FrontPanel::H);
    panel.setTransform (juce::AffineTransform::scale (s));
}
