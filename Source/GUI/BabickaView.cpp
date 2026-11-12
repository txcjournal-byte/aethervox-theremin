#include "BabickaView.h"
#include "AetherLookAndFeel.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    constexpr float kPi = MathConstants<float>::pi;
    const Colour amber { 0xffffb238 };
    const Colour cream { 0xfff6e7c4 };

    Image loadResource (const String& file)
    {
        const auto resName = file.replaceCharacter ('.', '_');
        int size = 0;
        if (auto* data = BinaryData::getNamedResource (resName.toRawUTF8(), size))
            return ImageFileFormat::loadFrom (data, (size_t) size);
        jassertfalse;
        return {};
    }

    // frame-rate independent exponential follow
    float follow (float cur, float target, float rate, float dt) { return cur + (target - cur) * (1.0f - std::exp (-rate * dt)); }

    Point<float> toDesign (float x, float y) { return { x * Photo::S, y * Photo::S }; }
}

// =============================================================================
//  Loading
// =============================================================================
BabickaView::BabickaView (AetherVoxProcessor& p) : proc (p)
{
    base = loadResource ("base.jpg");
    // regions come from Tools/babicka/build_assets.py (regions.txt)
    loadLayer (pitchLayer, "pitch", 25, "mask_pitch.png", { 274, 29, 297, 335 });
    loadLayer (volLayer,   "vol",   13, "mask_vol.png",   { 664, 189, 207, 142 });
    loadLayer (blinkLayer, "blink",  5, "mask_blink.png", { 519, 129, 92, 70 });
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void BabickaView::loadLayer (Layer& l, const char* prefix, int count, const char* maskName, Rectangle<int> region)
{
    l.region = region;
    const Image mask = loadResource (maskName).convertedToFormat (Image::SingleChannel);
    for (int i = 0; i < count; ++i)
    {
        const Image rgb = loadResource (String (prefix) + "_" + String (i).paddedLeft ('0', 2) + ".jpg");
        Image out (Image::ARGB, region.getWidth(), region.getHeight(), true);
        if (rgb.isValid() && mask.isValid())
        {
            const Image::BitmapData src (rgb, Image::BitmapData::readOnly);
            const Image::BitmapData m (mask, Image::BitmapData::readOnly);
            Image::BitmapData dst (out, Image::BitmapData::writeOnly);
            const int w = jmin (region.getWidth(), rgb.getWidth(), mask.getWidth());
            const int h = jmin (region.getHeight(), rgb.getHeight(), mask.getHeight());
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    dst.setPixelColour (x, y, src.getPixelColour (x, y).withAlpha (m.getPixelColour (x, y).getAlpha()));
        }
        l.frames.push_back (out);
    }
}

void BabickaView::rescaleIfNeeded (float physicalScale)
{
    if (std::abs (physicalScale - scaledFor) < 0.001f && baseScaled.isValid()) return;
    scaledFor = physicalScale;
    const float k = Photo::S * physicalScale;                 // photo px -> physical px
    baseScaled = base.rescaled (jmax (1, roundToInt (Photo::W * k)), jmax (1, roundToInt (Photo::H * k)), Graphics::highResamplingQuality);
    for (auto* l : { &pitchLayer, &volLayer, &blinkLayer })
    {
        l->scaled.clear();
        for (auto& f : l->frames)
            l->scaled.push_back (f.rescaled (jmax (1, roundToInt ((float) f.getWidth() * k)),
                                             jmax (1, roundToInt ((float) f.getHeight() * k)), Graphics::mediumResamplingQuality));
    }
}

// =============================================================================
//  Playing with the mouse: hold and move. Toward the rod = higher, up = louder.
// =============================================================================
void BabickaView::mouseDown (const MouseEvent& e)
{
    playing = true;
    // tell the host a gesture starts, so FL / any DAW records the hand movement as automation
    for (auto* id : { ParamIDs::handPitch, ParamIDs::handVol })
        if (auto* prm = proc.apvts.getParameter (id)) prm->beginChangeGesture();
    applyMouse (e.position);
}
void BabickaView::mouseDrag (const MouseEvent& e) { if (playing) applyMouse (e.position); }

void BabickaView::mouseUp (const MouseEvent&)
{
    playing = false;
    handVolume = 0.0f;
    syncPad();
    for (auto* id : { ParamIDs::handPitch, ParamIDs::handVol })
        if (auto* prm = proc.apvts.getParameter (id)) prm->endChangeGesture();
}

void BabickaView::applyMouse (Point<float> d)
{
    const float x = d.x / Photo::S, y = d.y / Photo::S;
    handPitch  = jlimit (0.0f, 1.0f, 1.0f - (x - (Photo::rodX + 20.0f)) / (Photo::playFarX - Photo::rodX - 20.0f));
    handVolume = jlimit (0.0f, 1.0f, (Photo::H - 60.0f - y) / (Photo::H - 120.0f));
    syncPad();
}

void BabickaView::syncPad()
{
    proc.padPitch.store (handPitch);
    proc.padVolume.store (handVolume);
    proc.padActive.store (playing);
    // mirror the hands into the automatable parameters (recorded by the host while it records)
    if (auto* prm = proc.apvts.getParameter (ParamIDs::handPitch)) prm->setValueNotifyingHost (handPitch);
    if (auto* prm = proc.apvts.getParameter (ParamIDs::handVol))   prm->setValueNotifyingHost (handVolume);
}

// =============================================================================
//  Animation
// =============================================================================
void BabickaView::tick (float dt)
{
    t += dt;
    fadeIn = jmin (1.0f, fadeIn + dt / 0.35f);
    shownVol = follow (shownVol, proc.dispVolume.load(), 18.0f, dt);
    shownHz  = proc.dispHz.load();
    needle   = follow (needle, shownVol, 9.0f, dt);

    const bool sounding = shownVol > 0.02f;
    // pitch hand follows the note (C1 at the bottom of the rod, C7 at the top);
    // idle: hands float gently, as if she is listening
    const float pitchTarget = sounding ? proc.dispPitchProx.load()
                                       : 0.42f + 0.07f * std::sin (t * 0.55f) + 0.03f * std::sin (t * 1.7f);
    const float volTarget   = sounding ? 0.25f + 0.75f * jlimit (0.0f, 1.0f, shownVol * 1.3f)
                                       : 0.78f + 0.08f * std::sin (t * 0.42f + 1.0f);
    pitchPose = follow (pitchPose, jlimit (0.0f, 1.0f, pitchTarget), sounding ? 9.0f : 2.0f, dt);
    volPose   = follow (volPose,   jlimit (0.0f, 1.0f, volTarget),   sounding ? 8.0f : 2.0f, dt);

    // blink every few seconds (a bit longer, savouring the tone, while she plays)
    if (blinkT < 0.0f && t > nextBlink) blinkT = 0.0f;
    if (blinkT >= 0.0f)
    {
        const float dur = sounding ? 0.55f : 0.28f;
        blinkT += dt;
        const float u = blinkT / dur;
        blinkPose = u < 0.5f ? u * 2.0f : jmax (0.0f, 2.0f - u * 2.0f);
        if (u >= 1.0f) { blinkT = -1.0f; blinkPose = 0.0f; nextBlink = t + 2.5f + rng.nextFloat() * 3.5f; }
    }
    repaint();
}

// =============================================================================
//  Painting
// =============================================================================
void BabickaView::paint (Graphics& g)
{
    g.fillAll (Colour (0xff1a0f08));
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    rescaleIfNeeded (phys);

    if (fadeIn < 1.0f) g.beginTransparencyLayer (fadeIn);
    g.drawImageTransformed (baseScaled, AffineTransform::scale (1.0f / phys));
    drawLayer (g, pitchLayer, pitchPose);
    drawLayer (g, volLayer, volPose);
    if (blinkPose > 0.001f) drawLayer (g, blinkLayer, blinkPose);
    drawGlow (g);
    drawMeter (g);
    if (fadeIn < 1.0f) g.endTransparencyLayer();
}

void BabickaView::drawLayer (Graphics& g, const Layer& l, float pose) const
{
    if (l.scaled.empty()) return;
    const int n = (int) l.scaled.size();
    const float f = jlimit (0.0f, 1.0f, pose) * (float) (n - 1);
    const int i = jmin ((int) f, n - 2);
    const float a = f - (float) i;
    const float phys = scaledFor;
    const auto at = AffineTransform::scale (1.0f / phys)
                        .translated ((float) l.region.getX() * Photo::S, (float) l.region.getY() * Photo::S);
    g.setOpacity (1.0f);
    g.drawImageTransformed (l.scaled[(size_t) i], at);
    if (a > 0.01f)
    {
        g.setOpacity (a);
        g.drawImageTransformed (l.scaled[(size_t) i + 1], at);
        g.setOpacity (1.0f);
    }
}

void BabickaView::drawGlow (Graphics& g)
{
    if (shownVol < 0.01f) return;
    // warm valve glow around the pitch rod, brightest near her hand
    const float handY = Photo::rodBotY - pitchPose * (Photo::rodBotY - Photo::rodTopY - 30.0f);
    const auto c = toDesign (Photo::rodX, handY);
    const float r = (60.0f + 70.0f * shownVol) * Photo::S;
    ColourGradient halo (amber.withAlpha (0.30f * shownVol), c.x, c.y, amber.withAlpha (0.0f), c.x + r, c.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour (amber.withAlpha (0.25f * shownVol));
    const auto top = toDesign (Photo::rodX, Photo::rodTopY), bot = toDesign (Photo::rodX, Photo::rodBotY);
    g.drawLine ({ top, bot }, 7.0f);

    // shimmer on the volume loop
    const auto lc = toDesign (Photo::loopCX, Photo::loopCY);
    g.setColour (amber.withAlpha (0.35f * shownVol));
    g.drawEllipse (lc.x - Photo::loopRX * Photo::S, lc.y - Photo::loopRY * Photo::S,
                   Photo::loopRX * 2.0f * Photo::S, Photo::loopRY * 2.0f * Photo::S, 4.0f);
}

void BabickaView::drawMeter (Graphics& g)
{
    // the amber meter on the cabinet becomes a live VU meter with the note name
    const auto r = Rectangle<float> (toDesign (Photo::meterX0, Photo::meterY0), toDesign (Photo::meterX1, Photo::meterY1));
    const float glow = 0.55f + 0.45f * needle;
    ColourGradient face (Colour (0xffffd27a).withMultipliedBrightness (glow), r.getCentreX(), r.getBottom(),
                         Colour (0xffc46a12).withMultipliedBrightness (glow), r.getCentreX(), r.getY() - 10.0f, true);
    g.setGradientFill (face);
    g.fillRoundedRectangle (r, 6.0f);

    // scale ticks
    const Point<float> pivot (r.getCentreX(), r.getBottom() + 2.0f);
    const float len = r.getHeight() * 0.95f;
    g.setColour (Colour (0xff3a1d05).withAlpha (0.7f));
    for (int i = 0; i <= 8; ++i)
    {
        const float a = -0.75f + 1.5f * (float) i / 8.0f;
        const auto p1 = pivot.getPointOnCircumference (len * 0.80f, a), p2 = pivot.getPointOnCircumference (len * (i % 4 == 0 ? 0.62f : 0.70f), a);
        g.drawLine ({ p1, p2 }, 1.4f);
    }
    // needle
    const float ang = -0.75f + 1.5f * jlimit (0.0f, 1.0f, needle);
    g.setColour (Colour (0xff2a1203));
    g.drawLine ({ pivot, pivot.getPointOnCircumference (len * 0.9f, ang) }, 2.0f);

    // halo spilling onto the walnut
    g.setColour (amber.withAlpha (0.10f + 0.20f * needle));
    g.drawRoundedRectangle (r.expanded (3.0f), 8.0f, 4.0f);
}

// =============================================================================
//  PanelKnob
// =============================================================================
PanelKnob::PanelKnob()
{
    setSliderStyle (Slider::RotaryVerticalDrag);
    setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
    setMouseCursor (MouseCursor::UpDownResizeCursor);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (220);
}

void PanelKnob::paint (Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = Photo::capR * Photo::S * (float) getWidth() / 72.0f;
    const auto rp = getRotaryParameters();
    const float prop = (float) valueToProportionOfLength (getValue());
    const float angle = rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);

    const auto p1 = c.getPointOnCircumference (r * 0.25f, angle), p2 = c.getPointOnCircumference (r * 0.88f, angle);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.drawLine ({ p1, p2 }, 4.0f);
    g.setColour (cream);
    g.drawLine ({ p1, p2 }, 2.4f);
}
