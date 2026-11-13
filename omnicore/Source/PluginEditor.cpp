#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

namespace
{
    // circle of fifths, left to right on the photo
    const int rootsTen[10] = { 8, 3, 10, 5, 0, 7, 2, 9, 4, 11 };     // Ab Eb Bb F C G D A E B
    const int rootsNine[9] = { 3, 10, 5, 0, 7, 2, 9, 4, 11 };        // Eb .. B (row 2 has 9 keys)

    // eye geometry on the photo (1254 px)
    const Rectangle<float> eyeBox { 470.0f, 350.0f, 320.0f, 198.0f };
    const Rectangle<float> ledBox { 398.0f, 690.0f, 458.0f, 34.0f };

    Path almond (Rectangle<float> e, float inset = 0.0f)
    {
        e = e.reduced (inset);
        Path p;
        p.startNewSubPath (e.getX(), e.getCentreY());
        p.quadraticTo (e.getCentreX(), e.getY() - e.getHeight() * 0.42f, e.getRight(), e.getCentreY());
        p.quadraticTo (e.getCentreX(), e.getBottom() + e.getHeight() * 0.42f, e.getX(), e.getCentreY());
        p.closeSubPath();
        return p;
    }
}

OmniEditor::OmniEditor (OmniProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    photo = ImageCache::getFromMemory (BinaryData::pyramid_jpg, BinaryData::pyramid_jpgSize);
    // "lit" twin of the photo: same pixels, hotter reds (used for glowing keys)
    photoLit = photo.createCopy();
    {
        Image::BitmapData bd (photoLit, Image::BitmapData::readWrite);
        for (int y = 680; y < 980; ++y)
            for (int x = 380; x < 880; ++x)
            {
                auto c = bd.getPixelColour (x, y);
                bd.setPixelColour (x, y, Colour::fromFloatRGBA (jmin (1.0f, c.getFloatRed() * 1.45f + 0.10f),
                                                                 jmin (1.0f, c.getFloatGreen() * 1.25f + 0.04f),
                                                                 jmin (1.0f, c.getFloatBlue() * 1.2f + 0.03f), 1.0f));
            }
    }
    buildKeys();
    litCache.assign (keys.size(), false);

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (1.0);
    setResizeLimits (480, 480, 1254, 1254);
    setSize (760, 760);
    startTimerHz (40);
    showLed ("OMNICORE 3000", OmniProcessor::presetName (proc.currentPreset.load()));
}

void OmniEditor::buildKeys()
{
    auto R = [] (float x0, float y0, float x1, float y1) { return Rectangle<float>::leftTopRightBottom (x0, y0, x1 + 1, y1 + 1); };
    keys.clear();
    // row 1: major
    const float r1[10][2] = { {407,446},{450,491},{493,536},{539,580},{584,625},{629,670},{674,715},{718,759},{762,805},{807,847} };
    for (int i = 0; i < 10; ++i) keys.push_back ({ R (r1[i][0], 730, r1[i][1], 766), KeyKind::Chord, 0, rootsTen[i], {} });
    // row 2: minor (9 keys on the photo)
    const float r2[9][2] = { {428,466},{472,511},{518,556},{561,602},{605,647},{651,694},{697,740},{742,782},{787,825} };
    for (int i = 0; i < 9; ++i) keys.push_back ({ R (r2[i][0], 769, r2[i][1], 805), KeyKind::Chord, 1, rootsNine[i], {} });
    // row 3: 7th (9 small + big enter)
    const float r3[9][2] = { {401,440},{445,485},{489,531},{535,578},{582,625},{629,672},{675,718},{721,765},{767,797} };
    for (int i = 0; i < 9; ++i) keys.push_back ({ R (r3[i][0], 808, r3[i][1], 844), KeyKind::Chord, 2, rootsTen[i], {} });
    keys.push_back ({ R (799, 808, 853, 873), KeyKind::Chord, 2, rootsTen[9], {} });
    // row 4: play controls
    const float r4[8][2] = { {398,454},{461,503},{510,551},{554,600},{603,649},{652,698},{701,746},{749,797} };
    keys.push_back ({ R (r4[0][0], 847, r4[0][1], 883), KeyKind::Mode, 0, 0, "NOTES" });
    keys.push_back ({ R (r4[1][0], 847, r4[1][1], 883), KeyKind::Mode, 1, 0, "CHORD" });
    keys.push_back ({ R (r4[2][0], 847, r4[2][1], 883), KeyKind::Mode, 2, 0, "STRUM" });
    keys.push_back ({ R (r4[3][0], 847, r4[3][1], 883), KeyKind::Mode, 3, 0, "ARP" });
    keys.push_back ({ R (r4[4][0], 847, r4[4][1], 883), KeyKind::Hold, 0, 0, "HOLD" });
    keys.push_back ({ R (r4[5][0], 847, r4[5][1], 883), KeyKind::Bass, 0, 0, "BASS" });
    keys.push_back ({ R (r4[6][0], 847, r4[6][1], 883), KeyKind::ArpRate, 0, 0, "ARP RATE" });
    keys.push_back ({ R (r4[7][0], 847, r4[7][1], 883), KeyKind::Cosmos, 0, 0, "COSMOS" });
    // row 5 + 6
    keys.push_back ({ R (432, 888, 491, 911), KeyKind::PresetDown, 0, 0, "PRESET -" });
    keys.push_back ({ R (495, 888, 762, 912), KeyKind::StrumHigh, 0, 0, "STRUM" });
    keys.push_back ({ R (766, 887, 827, 912), KeyKind::PresetUp, 0, 0, "PRESET +" });
    keys.push_back ({ R (490, 927, 763, 951), KeyKind::Strum, 0, 0, "STRUM" });
}

int OmniEditor::keyAt (Point<float> p) const
{
    for (int i = (int) keys.size() - 1; i >= 0; --i)
        if (keys[(size_t) i].r.expanded (2.0f).contains (p)) return i;
    return -1;
}

void OmniEditor::showLed (const String& a, const String& b)
{
    led1 = a; led2 = b; ledUntil = Time::getMillisecondCounterHiRes() + 1600.0;
}

void OmniEditor::setChoice (const char* id, int index)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

bool OmniEditor::isLit (const Key& k) const
{
    auto& s = proc.apvts;
    switch (k.kind)
    {
        case KeyKind::Chord:
        {
            const int type = proc.dispType.load();
            const int rowType = k.a == 1 ? 1 : (k.a == 2 ? 2 : 0);
            return proc.dispRoot.load() == k.b && type == rowType;
        }
        case KeyKind::Mode:    return roundToInt (s.getRawParameterValue (PID::mode)->load()) == k.a;
        case KeyKind::Hold:    return s.getRawParameterValue (PID::hold)->load() > 0.5f;
        case KeyKind::Bass:    return s.getRawParameterValue (PID::bass)->load() > 0.5f;
        case KeyKind::Cosmos:  return proc.cosmosLevel.load() > 0;
        case KeyKind::ArpRate: return roundToInt (s.getRawParameterValue (PID::mode)->load()) == 3;
        default: return false;
    }
}

// -----------------------------------------------------------------------------
void OmniEditor::strumAt (const Key& k, float x, float speed)
{
    const float t = jlimit (0.0f, 0.999f, (x - k.r.getX()) / k.r.getWidth());
    const int n = oc::ChordEngine::numStrings;
    int s = (int) (t * n);
    if (k.kind == KeyKind::StrumHigh) s = jmin (n - 1, s / 2 + n / 2);   // upper bar = top half of the strings
    if (lastString < 0) lastString = s;
    const int step = s >= lastString ? 1 : -1;
    for (int i = lastString; ; i += step)
    {
        if (i != lastString || speed == 0.0f)
        {
            const float vel = jlimit (0.3f, 1.0f, 0.55f + speed * 0.4f);
            proc.keyboardState.noteOn (OmniProcessor::stringChannel, i, vel);
            proc.keyboardState.noteOff (OmniProcessor::stringChannel, i, 0.0f);
            stringGlow[(size_t) i] = 1.0f;
        }
        if (i == s) break;
    }
    lastString = s;
}

void OmniEditor::pressKey (int idx, Point<float> pp)
{
    auto& k = keys[(size_t) idx];
    downKey = idx;
    switch (k.kind)
    {
        case KeyKind::Chord:
            proc.keyboardState.noteOn (OmniProcessor::chordChannel, k.a * 12 + k.b, 0.85f);
            showLed (String (oc::ChordEngine::pcName (k.b)) + (k.a == 1 ? "m" : k.a == 2 ? "7" : ""), {});
            break;
        case KeyKind::Mode:
            setChoice (PID::mode, k.a); showLed ("MODE", k.name); break;
        case KeyKind::Hold:
        {
            auto* prm = proc.apvts.getParameter (PID::hold);
            const bool on = prm->getValue() < 0.5f;
            prm->beginChangeGesture(); prm->setValueNotifyingHost (on ? 1.0f : 0.0f); prm->endChangeGesture();
            showLed ("HOLD", on ? "ON" : "OFF"); break;
        }
        case KeyKind::Bass:
        {
            auto* prm = proc.apvts.getParameter (PID::bass);
            const bool on = prm->getValue() < 0.5f;
            prm->beginChangeGesture(); prm->setValueNotifyingHost (on ? 1.0f : 0.0f); prm->endChangeGesture();
            showLed ("BASS", on ? "ON" : "OFF"); break;
        }
        case KeyKind::ArpRate:
        {
            const int r = (roundToInt (proc.apvts.getRawParameterValue (PID::arpRate)->load()) + 1) % Params::arpRateNames.size();
            setChoice (PID::arpRate, r); showLed ("ARP RATE", Params::arpRateNames[r]); break;
        }
        case KeyKind::Cosmos:
        {
            const int l = (proc.cosmosLevel.load() + 1) % 3;
            proc.setCosmos (l);
            showLed ("COSMOS", l == 0 ? "OFF" : l == 1 ? "DRIFT" : "FULL COSMOS"); break;
        }
        case KeyKind::PresetDown:
        case KeyKind::PresetUp:
        {
            const int i = proc.currentPreset.load() + (k.kind == KeyKind::PresetUp ? 1 : -1);
            proc.applyPreset (i);
            showLed ("PRESET " + String (proc.currentPreset.load() + 1), OmniProcessor::presetName (proc.currentPreset.load())); break;
        }
        case KeyKind::StrumHigh:
        case KeyKind::Strum:
            lastString = -1; lastStrumTime = Time::getMillisecondCounterHiRes(); lastStrumX = pp.x;
            strumAt (k, pp.x, 0.0f); break;
        default: break;
    }
    repaint();
}

void OmniEditor::releaseKey()
{
    if (downKey < 0) return;
    auto& k = keys[(size_t) downKey];
    if (k.kind == KeyKind::Chord) proc.keyboardState.noteOff (OmniProcessor::chordChannel, k.a * 12 + k.b, 0.0f);
    downKey = -1; lastString = -1;
    repaint();
}

void OmniEditor::mouseDown (const MouseEvent& e)
{
    const auto pp = toPhoto (e.position);
    if (almond (eyeBox).contains (pp))
    {
        proc.mutate(); flash = 1.0f;
        showLed ("THE EYE", "MUTATED");
        return;
    }
    const int i = keyAt (pp);
    if (i >= 0) pressKey (i, pp);
}

void OmniEditor::mouseDrag (const MouseEvent& e)
{
    const auto pp = toPhoto (e.position);
    if (downKey >= 0)
    {
        auto& k = keys[(size_t) downKey];
        if (k.kind == KeyKind::Strum || k.kind == KeyKind::StrumHigh)
        {
            const double now = Time::getMillisecondCounterHiRes();
            const float speed = (float) jlimit (0.0, 2.0, std::abs (pp.x - lastStrumX) / jmax (1.0, now - lastStrumTime) * 0.5);
            lastStrumTime = now; lastStrumX = pp.x;
            strumAt (k, jlimit (k.r.getX(), k.r.getRight() - 1.0f, pp.x), speed);
            return;
        }
        // glissando across chord keys
        if (k.kind == KeyKind::Chord)
        {
            const int i = keyAt (pp);
            if (i >= 0 && i != downKey && keys[(size_t) i].kind == KeyKind::Chord) { releaseKey(); pressKey (i, pp); }
        }
    }
}

void OmniEditor::mouseUp (const MouseEvent&) { releaseKey(); }

// -----------------------------------------------------------------------------
void OmniEditor::timerCallback()
{
    // eye: gaze follows the mouse anywhere on the screen, glow follows the sound
    const auto m = toPhoto (getLocalPoint (nullptr, Desktop::getMousePosition().toFloat()));
    auto d = m - eyeBox.getCentre();
    const float len = std::sqrt (d.x * d.x + d.y * d.y);
    gazeTarget = len > 1.0f ? d * (jmin (1.0f, len / 500.0f) / len) : Point<float>();
    gaze += (gazeTarget - gaze) * 0.15f;
    pulse += (jlimit (0.0f, 1.0f, proc.dispPeak.load() * 3.0f) - pulse) * 0.25f;
    flash *= 0.9f;
    for (auto& gl : stringGlow) gl *= 0.86f;

    for (size_t i = 0; i < keys.size(); ++i) litCache[i] = isLit (keys[i]);
    repaint();
}

void OmniEditor::paint (Graphics& g)
{
    const float s = scale();
    g.addTransform (AffineTransform::scale (s));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.drawImageAt (photo, 0, 0);

    // ---------------------------------------------------------------- the eye
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (almond (eyeBox, 6.0f));
        // the photo's own iris, moved: real pixels, so it stays photographic
        const auto off = gaze * Point<float> (22.0f, 12.0f);
        const auto src = eyeBox.expanded (34.0f).toNearestInt();
        g.drawImage (photo, roundToInt ((float) src.getX() + off.x), roundToInt ((float) src.getY() + off.y), src.getWidth(), src.getHeight(),
                     src.getX(), src.getY(), src.getWidth(), src.getHeight());
        // the pupil breathes with the sound: darken a slit around the moved centre
        const auto c = eyeBox.getCentre() + off;
        const float pw = 12.0f + 18.0f * pulse, ph = eyeBox.getHeight() * 0.86f;
        g.setGradientFill (ColourGradient (Colours::black.withAlpha (0.85f * pulse), c.x, c.y, Colours::transparentBlack, c.x + pw, c.y, true));
        g.fillEllipse (c.x - pw, c.y - ph * 0.5f, pw * 2.0f, ph);
        // inner fire: a ring of light in the iris, the slit stays black
        ColourGradient fire (Colours::transparentBlack, c.x, c.y, Colours::transparentBlack, c.x + eyeBox.getWidth() * 0.5f, c.y, true);
        fire.addColour (0.25, Colour (0xffff4a20).withAlpha (0.06f + 0.22f * pulse + 0.35f * flash));
        fire.addColour (0.6,  Colour (0xffff2a10).withAlpha (0.03f + 0.12f * pulse + 0.2f * flash));
        g.setGradientFill (fire);
        g.fillRect (eyeBox);
        g.setColour (Colours::black.withAlpha (0.55f + 0.3f * pulse));
        g.fillEllipse (c.x - 5.0f - 6.0f * pulse, c.y - ph * 0.46f, 10.0f + 12.0f * pulse, ph * 0.92f);
    }
    // glow spilling onto the stone
    {
        const auto c = eyeBox.getCentre();
        g.setGradientFill (ColourGradient (Colour (0xffff2010).withAlpha (0.08f + 0.22f * pulse + 0.35f * flash), c.x, c.y,
                                           Colours::transparentBlack, c.x + 330.0f, c.y, true));
        g.fillEllipse (c.x - 330.0f, c.y - 230.0f, 660.0f, 460.0f);
    }

    // ---------------------------------------------------------------- keys
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto& k = keys[i];
        const bool down = (int) i == downKey;
        const bool lit = litCache[i];
        const auto r = k.r.toNearestInt();
        if (lit || down)
        {
            g.setColour (Colour (0xffff2a1a).withAlpha (down ? 0.55f : 0.35f));
            g.fillRoundedRectangle (k.r.expanded (5.0f), 7.0f);
            g.drawImage (photoLit, r.getX(), r.getY() + (down ? 2 : 0), r.getWidth(), r.getHeight(),
                         r.getX(), r.getY(), r.getWidth(), r.getHeight());
            if (down) { g.setColour (Colours::black.withAlpha (0.18f)); g.fillRect (k.r.withHeight (2.0f)); }
        }
    }
    // strum bars: strings light up as you swipe
    for (const auto& k : keys)
        if (k.kind == KeyKind::Strum)
            for (int sIdx = 0; sIdx < oc::ChordEngine::numStrings; ++sIdx)
            {
                const float gl = stringGlow[(size_t) sIdx];
                if (gl < 0.02f) continue;
                const float x = k.r.getX() + k.r.getWidth() * ((float) sIdx + 0.5f) / (float) oc::ChordEngine::numStrings;
                g.setColour (Colour (0xffffb090).withAlpha (0.75f * gl));
                g.fillRect (x - 1.5f, k.r.getY() + 3.0f, 3.0f, k.r.getHeight() - 6.0f);
                g.setColour (Colour (0xffff2a1a).withAlpha (0.35f * gl));
                g.fillRect (x - 7.0f, k.r.getY() - 4.0f, 14.0f, k.r.getHeight() + 8.0f);
            }

    // ---------------------------------------------------------------- LED read-out
    {
        const bool msg = Time::getMillisecondCounterHiRes() < ledUntil;
        String a, b;
        if (msg) { a = led1; b = led2; }
        else
        {
            const int root = proc.dispRoot.load();
            a = root < 0 ? String ("--") : String (oc::ChordEngine::pcName (root)) + oc::ChordEngine::typeName (proc.dispType.load());
            b = Params::modeNames[roundToInt (proc.apvts.getRawParameterValue (PID::mode)->load())] + "   "
              + OmniProcessor::presetName (proc.currentPreset.load());
        }
        const Font big (FontOptions (Font::getDefaultMonospacedFontName(), 24.0f, Font::bold));
        const Font small (FontOptions (Font::getDefaultMonospacedFontName(), 14.0f, Font::bold));
        auto lr = ledBox;
        auto left = lr.removeFromLeft (250.0f);
        auto draw = [&] (const String& t, Rectangle<float> r, const Font& f, Justification j)
        {
            g.setFont (f);
            g.setColour (Colour (0xffff2a1a).withAlpha (0.18f));
            for (auto dd : { Point<float> (-1.5f, 0), Point<float> (1.5f, 0), Point<float> (0, -1.5f), Point<float> (0, 1.5f) })
                g.drawText (t, r.translated (dd.x, dd.y), j, false);
            g.setColour (Colour (0xffff4a30));
            g.drawText (t, r, j, false);
        };
        draw (a, left.reduced (6.0f, 0.0f), big, Justification::centredLeft);
        draw (b, lr.reduced (6.0f, 0.0f), small, Justification::centredRight);
    }
}
