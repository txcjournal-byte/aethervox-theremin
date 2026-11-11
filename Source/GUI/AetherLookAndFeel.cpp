#include "AetherLookAndFeel.h"

using namespace juce;

AetherLookAndFeel::AetherLookAndFeel()
{
    setColour (ResizableWindow::backgroundColourId, Palette::bg);
    setColour (Label::textColourId, Palette::text);

    setColour (Slider::textBoxTextColourId, Palette::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Slider::textBoxHighlightColourId, Palette::lacquer.withAlpha (0.5f));
    setColour (Slider::rotarySliderOutlineColourId, Palette::panelEdge);

    setColour (ComboBox::backgroundColourId, Palette::bgLight);
    setColour (ComboBox::outlineColourId, Palette::panelEdge);
    setColour (ComboBox::textColourId, Palette::text);
    setColour (ComboBox::arrowColourId, Palette::textDim);

    setColour (PopupMenu::backgroundColourId, Palette::bgLight);
    setColour (PopupMenu::textColourId, Palette::text);
    setColour (PopupMenu::highlightedBackgroundColourId, Palette::lacquer);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);

    setColour (TextEditor::backgroundColourId, Palette::bgLight);
    setColour (TextEditor::textColourId, Palette::text);
    setColour (TextEditor::highlightColourId, Palette::pitch.withAlpha (0.3f));
    setColour (CaretComponent::caretColourId, Palette::pitch);
}

void AetherLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                          float pos, float startAngle, float endAngle, Slider& s)
{
    const auto accent = s.findColour (Slider::rotarySliderFillColourId);
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool  hover = s.isMouseOverOrDragging();

    // track
    Path track;
    track.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (Palette::panelEdge);
    g.strokePath (track, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));

    // value arc + glow
    Path arc;
    arc.addCentredArc (c.x, c.y, radius - 2.0f, radius - 2.0f, 0.0f, startAngle, angle, true);
    g.setColour (accent.withAlpha (hover ? 0.35f : 0.22f));
    g.strokePath (arc, PathStrokeType (10.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (arc, PathStrokeType (4.5f, PathStrokeType::curved, PathStrokeType::rounded));

    // knob body: black knurled skirt + brushed chrome cap
    const float kr = radius * 0.72f;
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillEllipse (c.x - kr, c.y - kr + 2.0f, kr * 2.0f, kr * 2.0f);
    g.setColour (Colour (0xff141416));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
    g.setColour (Colour (0xff3a3b3f));
    for (int i = 0; i < 36; ++i)
    {
        const float a = (float) i / 36.0f * MathConstants<float>::twoPi;
        g.drawLine ({ c.getPointOnCircumference (kr * 0.86f, a), c.getPointOnCircumference (kr, a) }, 1.0f);
    }
    const float cr = kr * 0.74f;
    ColourGradient cap (Colour (0xfff4f5f6), c.x - cr * 0.5f, c.y - cr * 0.7f,
                        Colour (0xff6f7378), c.x + cr * 0.6f, c.y + cr, false);
    cap.addColour (0.5, Colour (0xffb7bbc0));
    g.setGradientFill (cap);
    g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
    g.setColour (Colours::white.withAlpha (0.10f));
    for (float rr = cr * 0.25f; rr < cr; rr += 2.0f)          // brushed rings
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.5f);

    // pointer
    const Point<float> p1 = c.getPointOnCircumference (cr * 0.30f, angle);
    const Point<float> p2 = c.getPointOnCircumference (cr * 0.92f, angle);
    g.setColour (Colour (0xff1a1a1c));
    g.drawLine ({ p1, p2 }, 2.2f);
}

void AetherLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool,
                                      int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);
    g.setColour (box.findColour (ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.isMouseOver() ? box.findColour (ComboBox::outlineColourId).brighter (0.4f)
                                   : box.findColour (ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    Path arrow;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (ComboBox::arrowColourId));
    g.fillPath (arrow);
}

void AetherLookAndFeel::drawToggleButton (Graphics& g, ToggleButton& b, bool highlighted, bool)
{
    const auto accent = b.findColour (ToggleButton::tickColourId);
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    // caption
    g.setColour (Palette::text);
    g.setFont (uiFont (19.0f, true));
    g.drawText (b.getButtonText(), r.removeFromTop (26.0f), Justification::centred);

    // pill switch
    const auto pill = Rectangle<float> (44.0f, 22.0f).withCentre (r.getCentre().translated (0.0f, -6.0f));
    g.setColour (on ? accent.withAlpha (0.30f) : Palette::bgLight);
    g.fillRoundedRectangle (pill, 11.0f);
    g.setColour (on ? accent : (highlighted ? Palette::textDim.brighter() : Palette::panelEdge));
    g.drawRoundedRectangle (pill, 11.0f, 1.2f);

    const float kx = on ? pill.getRight() - 19.0f : pill.getX() + 3.0f;
    g.setColour (on ? accent : Palette::textDim);
    g.fillEllipse (kx, pill.getY() + 3.0f, 16.0f, 16.0f);

    g.setColour (on ? accent : Palette::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText (on ? "ON" : "OFF", pill.translated (0.0f, 22.0f), Justification::centred);
}

Label* AetherLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (uiFont (19.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::outlineColourId, Colours::transparentBlack);
    l->setColour (Label::backgroundColourId, Colours::transparentBlack);
    l->setColour (Label::textColourId, Palette::text.withAlpha (0.8f));
    return l;
}
