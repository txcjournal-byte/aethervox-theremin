#include "InstrumentView.h"
#include "AetherLookAndFeel.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    constexpr float kPi = MathConstants<float>::pi, kTwoPi = MathConstants<float>::twoPi;
    const Colour red   { 0xffd42f3a };
    const Colour chalk { 0xfff2f2f0 };
    const char* kNoteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    float smooth (float t) { t = jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }
}

InstrumentView::InstrumentView (AetherVoxProcessor& p) : proc (p)
{
    render = ImageCache::getFromMemory (BinaryData::aethervox_render_jpg, BinaryData::aethervox_render_jpgSize);
    sketch = ImageCache::getFromMemory (BinaryData::aethervox_sketch_jpg, BinaryData::aethervox_sketch_jpgSize);
    setMouseCursor (MouseCursor::CrosshairCursor);
    sparks.reserve (256);
}

// =============================================================================
//  Geometry helpers
// =============================================================================
float InstrumentView::rodXAt (float y)
{
    const float t = (y - Art::rodTopY) / (Art::rodBotY - Art::rodTopY);
    return Art::rodTopX + (Art::rodBotX - Art::rodTopX) * t;
}

float InstrumentView::pitchTipX (float prox, float y) const
{
    const float near = rodXAt (y) + 22.0f;
    return near + (1.0f - prox) * (Art::pitchFarX - near);
}

float InstrumentView::volumeTipY (float vol) const
{
    const float lowY = Art::loopCY - Art::loopRY - 34.0f, highY = 420.0f;
    return lowY - vol * (lowY - highY);
}

// =============================================================================
//  Playing
// =============================================================================
void InstrumentView::mouseDown (const MouseEvent& e)
{
    const auto p = e.position;
    if (p.x > Art::dispX0 && p.x < Art::dispX1 && p.y > Art::dispY0 && p.y < Art::dispY1)
    {
        if (onDisplayClicked) onDisplayClicked();
        return;
    }
    grab = Grab::Play;
    applyMouse (p);
}

void InstrumentView::mouseDrag (const MouseEvent& e) { if (grab != Grab::None) applyMouse (e.position); }

void InstrumentView::mouseUp (const MouseEvent&)
{
    if (grab == Grab::None) return;
    grab = Grab::None;
    handVolume = 0.0f;                  // hands leave the field -> note fades out (Release)
    syncPad();
}

void InstrumentView::applyMouse (Point<float> p)
{
    pitchY = jlimit (Art::rodTopY + 60.0f, Art::rodBotY - 40.0f, p.y);
    const float near = rodXAt (pitchY) + 22.0f;
    handPitch  = jlimit (0.0f, 1.0f, 1.0f - (p.x - near) / (Art::pitchFarX - near));
    handVolume = jlimit (0.0f, 1.0f, (Art::playBotY - p.y) / (Art::playBotY - Art::playTopY));
    syncPad();
}

void InstrumentView::syncPad()
{
    proc.padPitch.store (handPitch);
    proc.padVolume.store (handVolume);
    proc.padActive.store (grab != Grab::None);
}

// =============================================================================
//  Animation
// =============================================================================
void InstrumentView::tick (float dt)
{
    introT += dt;
    shownProx += (proc.dispPitchProx.load() - shownProx) * 0.35f;
    shownVol  += (proc.dispVolume.load()    - shownVol)  * 0.30f;
    shownHz    = proc.dispHz.load();
    proc.copyScope (scope.data());

    const float octs = jlimit (0.0f, 1.0f, std::log2 (jmax (shownHz, 20.0f) / 20.0f) / 7.2f);
    wavePhase += 0.05f + 0.35f * octs;  wavePhase -= std::floor (wavePhase);
    ringPhase += 0.004f + 0.02f * octs; ringPhase -= std::floor (ringPhase);

    const int spawn = (int) std::floor (shownVol * 2.5f + rng.nextFloat());
    for (int i = 0; i < spawn && sparks.size() < 220; ++i)
    {
        if (rng.nextBool())
        {
            const float y = Art::rodTopY + 30.0f + rng.nextFloat() * (Art::rodBotY - Art::rodTopY - 60.0f);
            sparks.push_back ({ { rodXAt (y), y }, { 0.6f + 2.2f * rng.nextFloat() * (0.3f + octs), (rng.nextFloat() - 0.5f) * 0.7f }, 1.0f });
        }
        else
        {
            const float a = rng.nextFloat() * kTwoPi;
            sparks.push_back ({ { Art::loopCX + std::cos (a) * Art::loopRX, Art::loopCY + std::sin (a) * Art::loopRY },
                                { (rng.nextFloat() - 0.5f) * 0.6f, -(0.5f + 1.6f * rng.nextFloat()) }, 1.0f });
        }
    }
    for (auto& s : sparks) { s.p += s.v; s.life -= 0.016f; }
    sparks.erase (std::remove_if (sparks.begin(), sparks.end(), [] (const Spark& s) { return s.life <= 0.0f; }), sparks.end());
    repaint();
}

float InstrumentView::getRevealX() const
{
    return Art::W * smooth ((introT - 0.25f) / 0.8f);
}

// =============================================================================
//  Painting
// =============================================================================
void InstrumentView::paint (Graphics& g)
{
    g.fillAll (Colours::black);
    drawIntro (g);                       // sketch -> render (render stays once revealed)

    const float reveal = smooth ((introT - 0.25f) / 0.8f);
    if (reveal <= 0.0f) return;

    const bool padMode = grab != Grab::None;
    const float prox = padMode ? handPitch  : shownProx;
    const float vol  = padMode ? handVolume : shownVol;
    const float energy = 0.08f + 0.92f * shownVol;

    const Point<float> pitchTip (pitchTipX (prox, pitchY), pitchY);
    const Point<float> volTip   (Art::loopCX, volumeTipY (vol));
    ignoreUnused (vol);

    // UI layers fade in with the reveal (no extra layer once fully shown: cheaper)
    const bool layered = reveal < 1.0f;
    if (layered) g.beginTransparencyLayer (reveal);
    drawTypography (g);
    drawPitchField (g, pitchTip, energy);
    drawVolumeField (g, volTip, energy);
    drawSparks (g);
    drawFingertip (g, pitchTip, energy);
    drawFingertip (g, volTip, energy);
    drawDisplay (g);
    if (layered) g.endTransparencyLayer();
}

void InstrumentView::drawIntro (Graphics& g)
{
    const auto full = Rectangle<float> (0, 0, Art::W, Art::H);
    const float hold = 0.25f, wipe = 0.8f;
    const float p = smooth ((introT - hold) / wipe);

    if (p < 1.0f)
    {
        // the chalk drawing fades in, then waits to be "rendered"
        g.setOpacity (smooth (introT / 0.2f));
        g.drawImage (sketch, full);
        g.setOpacity (1.0f);
    }
    if (p >= 1.0f)
    {
        // fully revealed: blit a copy pre-scaled to the real pixel size (no per-frame resampling)
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (std::abs (scale - renderScaledFor) > 0.001f || renderScaled.isNull())
        {
            const int w = jmax (1, roundToInt (Art::W * scale)), h = jmax (1, roundToInt (Art::H * scale));
            renderScaled = render.rescaled (w, h, Graphics::highResamplingQuality);
            renderScaledFor = scale;
        }
        g.drawImageTransformed (renderScaled, AffineTransform::scale (1.0f / scale));
        return;
    }
    if (p > 0.0f)
    {
        const float x = Art::W * p;
        g.saveState();
        g.reduceClipRegion (Rectangle<float> (0, 0, x, Art::H).toNearestInt());
        g.drawImage (render, full);
        g.restoreState();
        if (p < 1.0f)
        {
            g.setColour (red.withAlpha (0.35f));
            g.fillRect (x - 6.0f, 0.0f, 12.0f, Art::H);
            g.setColour (red);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, Art::H);          // the 2 px red line "drawing it live"
        }
    }
}

void InstrumentView::drawTypography (Graphics& g)
{
    // product-page typography in the empty studio space
    const float x = 900.0f;
    g.setColour (chalk.withAlpha (0.92f));
    g.setFont (uiFont (64.0f).withExtraKerningFactor (0.32f));
    g.drawText ("AETHERVOX", Rectangle<float> (x, 70.0f, 640.0f, 74.0f), Justification::centredLeft);

    g.setColour (red);
    g.fillRect (x + 3.0f, 152.0f, 46.0f, 2.0f);
    g.setColour (chalk.withAlpha (0.6f));
    g.setFont (uiFont (14.0f, true).withExtraKerningFactor (0.45f));
    g.drawText ("HETERODYNE THEREMIN", Rectangle<float> (x + 62.0f, 144.0f, 500.0f, 18.0f), Justification::centredLeft);

    // spec sheet
    g.setFont (uiFont (12.5f).withExtraKerningFactor (0.18f));
    g.setColour (chalk.withAlpha (0.38f));
    const char* specs[] = { "DUAL RF OSCILLATORS  170 kHz", "20 Hz  -  3 kHz", "4x OVERSAMPLED TUBE STAGE", "TAPE ECHO  /  FDN HALL" };
    for (int i = 0; i < 4; ++i)
        g.drawText (specs[i], Rectangle<float> (x + 3.0f, 186.0f + (float) i * 22.0f, 500.0f, 16.0f), Justification::centredLeft);

    // live reading, like an instrument cluster
    int cents = 0;
    String note = "--", hz = "--";
    if (shownHz > 0.0f)
    {
        const float m = 69.0f + 12.0f * std::log2 (shownHz / 440.0f);
        const int n = roundToInt (m);
        cents = roundToInt ((m - (float) n) * 100.0f);
        note = String (kNoteNames[((n % 12) + 12) % 12]) + String (n / 12 - 1);
        hz = String (shownHz, 1);
    }
    g.setColour (chalk.withAlpha (0.92f));
    g.setFont (uiFont (54.0f));
    g.drawText (note, Rectangle<float> (x, 300.0f, 220.0f, 60.0f), Justification::centredLeft);
    g.setColour (chalk.withAlpha (0.55f));
    g.setFont (uiFont (15.0f).withExtraKerningFactor (0.2f));
    g.drawText (hz + " HZ", Rectangle<float> (x + 200.0f, 306.0f, 200.0f, 20.0f), Justification::centredLeft);
    g.drawText ((shownHz > 0.0f ? (cents >= 0 ? "+" : "") + String (cents) : String ("--")) + " CENT",
                Rectangle<float> (x + 200.0f, 330.0f, 200.0f, 20.0f), Justification::centredLeft);

    // level bar
    const auto bar = Rectangle<float> (x + 3.0f, 380.0f, 300.0f, 2.0f);
    g.setColour (chalk.withAlpha (0.12f));
    g.fillRect (bar);
    g.setColour (red);
    g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, shownVol)));

    // discreet hint
    g.setColour (chalk.withAlpha (0.30f));
    g.setFont (uiFont (11.0f).withExtraKerningFactor (0.15f));
    g.drawText ("HOLD THE MOUSE AND MOVE  -  TOWARD THE ROD = HIGHER  -  UP = LOUDER  -  RELEASE = SILENCE  -  TOUCH THE DISPLAY FOR THE ENGINE",
                Rectangle<float> (0, Art::H - 34.0f, Art::W, 16.0f), Justification::centred);

    // engraved knob legends on the front panel
    const char* legends[6] = { "TUNE", "GLIDE", "VIBRATO", "DRIVE", "REVERB", "VOLUME" };
    g.setColour (Colours::white.withAlpha (0.55f));
    g.setFont (uiFont (8.0f, true).withExtraKerningFactor (0.45f));
    for (int i = 0; i < 6; ++i)
        g.drawText (legends[i], Rectangle<float> (Art::knobX[i] - 40.0f, Art::knobY - 41.0f, 80.0f, 10.0f), Justification::centred);

    // engraved wordmark on the front panel
    g.setColour (Colours::white.withAlpha (0.42f));
    g.setFont (uiFont (8.5f, true).withExtraKerningFactor (0.6f));
    g.drawText ("AETHERVOX", Rectangle<float> (Art::wordX - 80.0f, Art::wordY - 6.0f, 160.0f, 12.0f), Justification::centred);
}

void InstrumentView::drawPitchField (Graphics& g, Point<float> tip, float energy)
{
    // radiating rings around the rod (following its perspective tilt)
    const float midY = (Art::rodTopY + Art::rodBotY) * 0.5f, halfLen = (Art::rodBotY - Art::rodTopY) * 0.5f;
    const float tilt = std::atan2 (Art::rodBotX - Art::rodTopX, Art::rodBotY - Art::rodTopY);
    for (int i = 0; i < 8; ++i)
    {
        float t = (float) i / 8.0f + ringPhase;
        t -= std::floor (t);
        const float r = 18.0f + t * 720.0f;
        Path ring;
        ring.addRoundedRectangle (-r, -halfLen - r * 0.10f, r * 2.0f, halfLen * 2.0f + r * 0.2f, jmin (r, halfLen));
        ring.applyTransform (AffineTransform::rotation (-tilt).translated (rodXAt (midY), midY));
        g.setColour (chalk.withAlpha ((1.0f - t) * (1.0f - t) * 0.16f * energy));
        g.strokePath (ring, PathStrokeType (1.0f));
    }

    // beat waves between fingertip and rod
    const float x0 = rodXAt (tip.y) + 8.0f, x1 = tip.x - 10.0f;
    if (x1 - x0 < 6.0f) return;
    const float density = 2.0f + 18.0f * shownProx, amp = 2.0f + 18.0f * shownVol;
    for (int line = -1; line <= 1; ++line)
    {
        Path w;
        for (int i = 0; i <= 160; ++i)
        {
            const float u = (float) i / 160.0f;
            const float env = std::sin (u * kPi);
            const float y = tip.y + (float) line * 16.0f * u
                          + amp * env * std::sin (kTwoPi * (density * u - wavePhase * 2.0f) + (float) line * 1.3f);
            if (i == 0) w.startNewSubPath (x0 + (x1 - x0) * u, y); else w.lineTo (x0 + (x1 - x0) * u, y);
        }
        if (line == 0)
        {
            g.setColour (red.withAlpha (0.25f * energy));
            g.strokePath (w, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (red.withAlpha (0.35f + 0.6f * shownVol));
            g.strokePath (w, PathStrokeType (1.8f));
        }
        else
        {
            g.setColour (chalk.withAlpha (0.08f + 0.30f * shownVol));
            g.strokePath (w, PathStrokeType (1.0f));
        }
    }
}

void InstrumentView::drawVolumeField (Graphics& g, Point<float> tip, float energy)
{
    // stacked ellipses rising from the loop (the field above the volume antenna)
    for (int i = 0; i < 6; ++i)
    {
        float t = (float) i / 6.0f + ringPhase * 0.8f;
        t -= std::floor (t);
        const float rise = t * (Art::loopCY - 430.0f);
        const float sx = 1.0f + 0.25f * t;
        g.setColour (chalk.withAlpha ((1.0f - t) * 0.16f * energy));
        g.drawEllipse (Art::loopCX - Art::loopRX * sx, Art::loopCY - rise - Art::loopRY * sx,
                       Art::loopRX * 2.0f * sx, Art::loopRY * 2.0f * sx, 1.0f);
    }

    // glowing loop when sounding
    if (shownVol > 0.01f)
    {
        g.setColour (red.withAlpha (0.28f * shownVol));
        g.drawEllipse (Art::loopCX - Art::loopRX, Art::loopCY - Art::loopRY, Art::loopRX * 2.0f, Art::loopRY * 2.0f, 6.0f);
    }

    // vertical beat wave between loop and fingertip
    const float y0 = Art::loopCY - Art::loopRY - 4.0f, y1 = tip.y + 12.0f;
    if (y0 - y1 < 6.0f) return;
    const float amp = 2.0f + 14.0f * shownVol;
    for (int line = -1; line <= 1; ++line)
    {
        Path w;
        for (int i = 0; i <= 100; ++i)
        {
            const float u = (float) i / 100.0f;
            const float env = std::sin (u * kPi);
            const float x = tip.x + (float) line * 60.0f * (1.0f - u)
                          + amp * env * std::sin (kTwoPi * (4.0f * u - wavePhase) + (float) line);
            if (i == 0) w.startNewSubPath (x, y0 + (y1 - y0) * u); else w.lineTo (x, y0 + (y1 - y0) * u);
        }
        if (line == 0)
        {
            g.setColour (red.withAlpha (0.25f * energy));
            g.strokePath (w, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));
            g.setColour (red.withAlpha (0.35f + 0.6f * shownVol));
            g.strokePath (w, PathStrokeType (1.6f));
        }
        else
        {
            g.setColour (chalk.withAlpha (0.08f + 0.28f * shownVol));
            g.strokePath (w, PathStrokeType (1.0f));
        }
    }
}

void InstrumentView::drawSparks (Graphics& g)
{
    for (const auto& s : sparks)
    {
        g.setColour (red.withAlpha (s.life * (0.25f + 0.75f * shownVol) * 0.85f));
        const float r = 0.8f + 1.4f * s.life;
        g.fillEllipse (s.p.x - r, s.p.y - r, r * 2.0f, r * 2.0f);
    }
}

void InstrumentView::drawFingertip (Graphics& g, Point<float> tip, float energy)
{
    // a soft "presence" of the hand in the field: halo, ring and core
    const float r = 16.0f;
    ColourGradient halo (red.withAlpha (0.45f * energy), tip.x, tip.y, red.withAlpha (0.0f), tip.x + r * 3.2f, tip.y, true);
    g.setGradientFill (halo);
    g.fillEllipse (tip.x - r * 3.2f, tip.y - r * 3.2f, r * 6.4f, r * 6.4f);
    g.setColour (chalk.withAlpha (0.25f + 0.5f * energy));
    g.drawEllipse (tip.x - r, tip.y - r, r * 2.0f, r * 2.0f, 1.0f);
    g.setColour (chalk.withAlpha (0.85f));
    g.fillEllipse (tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f);
}

void InstrumentView::drawDisplay (Graphics& g)
{
    const auto d = Rectangle<float> (Art::dispX0, Art::dispY0, Art::dispX1 - Art::dispX0, Art::dispY1 - Art::dispY0).reduced (8.0f, 6.0f);

    // waveform with phosphor glow
    const double sr = proc.getSampleRate() > 0.0 ? proc.getSampleRate() : 48000.0;
    const int len = jlimit (64, AetherVoxProcessor::scopeSize / 2, (int) (2.0 * sr / jmax (shownHz, 20.0f)));
    int start = AetherVoxProcessor::scopeSize - len - 1;
    for (int i = start; i > 1; --i)
        if (scope[(size_t) i - 1] < 0.0f && scope[(size_t) i] >= 0.0f) { start = i; break; }
    float peak = 0.02f;
    for (int i = 0; i < len; ++i) peak = jmax (peak, std::abs (scope[(size_t) (start + i)]));

    const auto wave = d.withTrimmedTop (14.0f);
    Path w;
    for (int i = 0; i < len; ++i)
    {
        const float x = wave.getX() + wave.getWidth() * (float) i / (float) (len - 1);
        const float y = wave.getCentreY() - scope[(size_t) (start + i)] / peak * wave.getHeight() * 0.42f;
        if (i == 0) w.startNewSubPath (x, y); else w.lineTo (x, y);
    }
    g.saveState();
    g.reduceClipRegion (d.toNearestInt());
    g.setColour (red.withAlpha (0.30f));
    g.strokePath (w, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (red.brighter (0.4f));
    g.strokePath (w, PathStrokeType (1.2f));
    g.restoreState();

    String note = "--";
    if (shownHz > 0.0f)
    {
        const int n = roundToInt (69.0f + 12.0f * std::log2 (shownHz / 440.0f));
        note = String (kNoteNames[((n % 12) + 12) % 12]) + String (n / 12 - 1);
    }
    g.setColour (chalk.withAlpha (0.8f));
    g.setFont (uiFont (10.0f, true).withExtraKerningFactor (0.2f));
    g.drawText (note, d.withHeight (12.0f), Justification::centredLeft);
    g.setColour (chalk.withAlpha (0.45f));
    g.drawText (shownHz > 0.0f ? String (shownHz, 1) + " HZ" : String ("ENGINE"), d.withHeight (12.0f), Justification::centredRight);
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
    const float s = (float) getWidth() / 72.0f;                // component is 72 x 72 artwork pixels
    const float r = Art::capR * s;
    const auto rp = getRotaryParameters();
    const float prop = (float) valueToProportionOfLength (getValue());
    const float angle = rp.startAngleRadians + prop * (rp.endAngleRadians - rp.startAngleRadians);

    // value arc around the skirt, only while touched
    if (isMouseOverOrDragging())
    {
        Path arc;
        arc.addCentredArc (c.x, c.y, r * 1.55f, r * 1.55f, 0.0f, rp.startAngleRadians, angle, true);
        g.setColour (red.withAlpha (isMouseButtonDown() ? 0.95f : 0.6f));
        g.strokePath (arc, PathStrokeType (2.0f * s, PathStrokeType::curved, PathStrokeType::rounded));
    }

    // engraved indicator line on the chrome cap (shadow + white enamel)
    const auto p1 = c.getPointOnCircumference (r * 0.30f, angle), p2 = c.getPointOnCircumference (r * 0.86f, angle);
    g.setColour (Colours::black.withAlpha (0.45f));
    g.drawLine ({ p1, p2 }, 3.4f * s);
    g.setColour (Colours::white.withAlpha (0.95f));
    g.drawLine ({ p1, p2 }, 1.9f * s);
}
