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
    // a small LED window set into the metal frame under the keys: drag it into the piano roll
    const Rectangle<float> dragBox { 492.0f, 990.0f, 270.0f, 34.0f };

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
    setResizeLimits (520, 520, 1254, 1254);
    setSize (780, 780);
    setWantsKeyboardFocus (true);
    startTimerHz (25);
    showLed ("OMNICORE 3000", OmniProcessor::presetName (proc.currentPreset.load()));
    take.setSync ((bool) proc.apvts.state.getProperty ("takeSync", false));
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

void OmniEditor::resized()
{
    const int w = jmax (1, getWidth()), h = jmax (1, getHeight());
    photoScaled = photo.getClippedImage (Rectangle<int> ((int) viewX, (int) viewY, (int) viewW, (int) viewW))
                       .rescaled (w, h, Graphics::highResamplingQuality);
    repaint();
}

int OmniEditor::keyForCode (int code) const
{
    if (code >= 'A' && code <= 'Z') code = code - 'A' + 'a';
    const String row1 = "1234567890", row2 = "qweryuiop", row3 = "asdfghjkl", row4 = "zxcvbnm";
    if (row1.containsChar ((juce_wchar) code)) return row1.indexOfChar ((juce_wchar) code);
    if (row2.containsChar ((juce_wchar) code)) return 10 + row2.indexOfChar ((juce_wchar) code);
    if (row3.containsChar ((juce_wchar) code)) return 19 + row3.indexOfChar ((juce_wchar) code);
    if (code == KeyPress::returnKey) return 28;
    if (row4.containsChar ((juce_wchar) code)) return 29 + row4.indexOfChar ((juce_wchar) code);
    if (code == ',' || code == '.' || code == ';' || code == ':') return 36;
    if (code == KeyPress::leftKey || code == '[') return 37;
    if (code == KeyPress::rightKey || code == ']') return 39;
    return -1;
}

String OmniEditor::keyLetter (int idx) const
{
    const String row1 = "1234567890", row2 = "QWERYUIOP", row3 = "ASDFGHJKL", row4 = "ZXCVBNM";
    if (idx < 10) return row1.substring (idx, idx + 1);
    if (idx < 19) return row2.substring (idx - 10, idx - 9);
    if (idx < 28) return row3.substring (idx - 19, idx - 18);
    if (idx == 28) return "ENTER";
    if (idx < 36) return row4.substring (idx - 29, idx - 28);
    if (idx == 36) return ",";
    if (idx == 37) return "<-";
    if (idx == 39) return "->";
    if (idx == 40 || idx == 38) return "SPACE";
    return {};
}

void OmniEditor::toggleArp (bool forceOn)
{
    const int mode = roundToInt (proc.apvts.getRawParameterValue (PID::mode)->load());
    auto setHold = [this] (bool on)
    {
        auto* prm = proc.apvts.getParameter (PID::hold);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (on ? 1.0f : 0.0f); prm->endChangeGesture();
    };
    if (mode == 3 && ! forceOn)
    {
        setChoice (PID::mode, modeBeforeArp == 3 ? 1 : modeBeforeArp);
        setHold (holdBeforeArp);
        showLed ("ARP OFF", Params::modeNames[modeBeforeArp == 3 ? 1 : modeBeforeArp]);
        return;
    }
    if (mode != 3)
    {
        modeBeforeArp = mode;
        holdBeforeArp = proc.apvts.getRawParameterValue (PID::hold)->load() > 0.5f;
        setChoice (PID::mode, 3);
    }
    const int r = roundToInt (proc.apvts.getRawParameterValue (PID::arpRate)->load());
    showLed ("ARP ON", String (Params::arpRateNames[r]) + "   V = OFF");
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
            if (k.a == 3) { toggleArp (false); break; }
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
            showLed ("BASS", on ? "ON  +ROOT LOW" : "OFF"); break;
        }
        case KeyKind::ArpRate:
        {
            if (roundToInt (proc.apvts.getRawParameterValue (PID::mode)->load()) != 3) { toggleArp (true); break; }
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
    grabKeyboardFocus();
    const auto pp = toPhoto (e.position);
    if (e.mods.isPopupMenu() && (ledBox.contains (pp) || dragBox.contains (pp))) { showTakeMenu(); return; }
    if (ledBox.contains (pp) || dragBox.contains (pp))
    {
        ledDrag = true;
        showLed (take.isEmpty() ? "PLAY FIRST" : "DRAG ME", take.isEmpty() ? "THEN DRAG THIS INTO THE PIANO ROLL"
                                                                          : String (take.countNotes()) + " NOTES > PIANO ROLL");
        return;
    }
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
    if (ledDrag)
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        const auto f = writeTake();
        if (! f.existsAsFile()) return;
        dragging = true;
        DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this]
        {
            dragging = false;
            take.markFinished();                   // play again = a fresh take
            showLed ("MIDI DROPPED", "PLAY AGAIN = NEW TAKE");
        });
        return;
    }
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

void OmniEditor::mouseUp (const MouseEvent&) { ledDrag = false; dragging = false; releaseKey(); }

void OmniEditor::mouseMove (const MouseEvent& e)
{
    const auto pp = toPhoto (e.position);
    const bool overLed = ledBox.contains (pp) || dragBox.contains (pp);
    setMouseCursor (overLed && ! take.isEmpty() ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    // hovering a key tells you what it does (and which computer key plays it)
    const int i = keyAt (pp);
    if (i != hoverKey)
    {
        hoverKey = i;
        if (i >= 0)
        {
            const auto& k = keys[(size_t) i];
            const String name = k.kind == KeyKind::Chord
                ? String (oc::ChordEngine::pcName (k.b)) + (k.a == 1 ? "m" : k.a == 2 ? "7" : "")
                : (k.kind == KeyKind::StrumHigh ? String ("STRUM HIGH") : k.name);
            showLed (name, "KEY  " + keyLetter (i));
        }
    }
}

void OmniEditor::mouseExit (const MouseEvent&) { hoverKey = -1; }

bool OmniEditor::keyPressed (const KeyPress& kp)
{
    const int code = kp.getKeyCode();
    if (code == KeyPress::spaceKey)
    {
        proc.keyboardState.noteOn (OmniProcessor::stringChannel, kp.getModifiers().isShiftDown() ? 101 : 100, 0.85f);
        proc.keyboardState.noteOff (OmniProcessor::stringChannel, kp.getModifiers().isShiftDown() ? 101 : 100, 0.0f);
        for (auto& gl : stringGlow) gl = 1.0f;
        return true;
    }
    if (code == KeyPress::escapeKey)
    {
        releaseKeyboardChords (true);
        proc.panic.store (true);
        auto* prm = proc.apvts.getParameter (PID::hold);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (0.0f); prm->endChangeGesture();
        showLed ("ALL OFF", "HOLD OFF");
        return true;
    }
    if (kp.getModifiers().isCommandDown() || kp.getModifiers().isAltDown()) return false;
    const int idx = keyForCode (code);
    if (idx < 0) return false;
    auto& k = keys[(size_t) idx];
    if (k.kind == KeyKind::Chord)
    {
        if (kbHeld.count (code)) return true;          // auto-repeat
        kbHeld[code] = idx;
        proc.keyboardState.noteOn (OmniProcessor::chordChannel, k.a * 12 + k.b, 0.85f);
        showLed (String (oc::ChordEngine::pcName (k.b)) + (k.a == 1 ? "m" : k.a == 2 ? "7" : ""), "KEY  " + keyLetter (idx));
        return true;
    }
    if (kbHeld.count (code)) return true;
    kbHeld[code] = -1;
    const int saved = downKey;
    pressKey (idx, k.r.getCentre());
    downKey = saved;
    return true;
}

void OmniEditor::focusLost (FocusChangeType) { releaseKeyboardChords (true); }

bool OmniEditor::keyStateChanged (bool)
{
    const bool had = ! kbHeld.empty();
    releaseKeyboardChords (false);
    return had;
}

/** Lets go of chords held on the computer keyboard once their key is up (or all of them). */
void OmniEditor::releaseKeyboardChords (bool all)
{
    for (auto it = kbHeld.begin(); it != kbHeld.end();)
    {
        if (all || ! KeyPress::isKeyCurrentlyDown (it->first))
        {
            if (it->second >= 0)
            {
                const auto& k = keys[(size_t) it->second];
                proc.keyboardState.noteOff (OmniProcessor::chordChannel, k.a * 12 + k.b, 0.0f);
            }
            it = kbHeld.erase (it);
        }
        else ++it;
    }
}

void OmniEditor::mouseDoubleClick (const MouseEvent& e)
{
    const auto pp = toPhoto (e.position);
    if (ledBox.contains (pp) || dragBox.contains (pp)) { take.clear(); showLed ("MIDI TAKE", "CLEARED"); }
}

void OmniEditor::showTakeMenu()
{
    PopupMenu m;
    m.addSectionHeader ("MIDI TAKE  (" + String (take.countNotes()) + " notes)");
    m.addItem (1, "Reset - start again");
    m.addSeparator();
    m.addItem (2, "Free: catch what I play, drag it anywhere", true, ! take.isSync());
    m.addItem (3, "Sync with FL: press Record / Play, play, stop, drop at bar 1", true, take.isSync());
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [this] (int r)
    {
        if (r == 1) { take.clear(); showLed ("MIDI TAKE", "RESET - PLAY AGAIN"); }
        if (r == 2 || r == 3)
        {
            take.setSync (r == 3);
            proc.apvts.state.setProperty ("takeSync", r == 3, nullptr);
            showLed (r == 3 ? "SYNC WITH FL" : "FREE TAKE", r == 3 ? "REC IN FL, PLAY, STOP, DRAG" : "PLAY, THEN DRAG");
        }
        repaint();
    });
}

File OmniEditor::writeTake()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("OmniCore 3000 take.mid");
    if (! take.writeTo (f, proc.dispBpm.load())) f.deleteFile();
    return f;
}

// -----------------------------------------------------------------------------
void OmniEditor::timerCallback()
{
    // SYNC take: a new take every time the host starts (FL Record / Play)
    {
        const bool playing = proc.hostPlaying.load();
        if (playing && ! wasHostPlaying) take.hostStarted();
        wasHostPlaying = playing;
    }
    // collect what the pyramid played since the last tick
    {
        const auto scope = proc.takeFifo.read (proc.takeFifo.getNumReady());
        auto feed = [&] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& e = proc.takeBuf[(size_t) (start + i)];
                take.add ({ e.sec, e.ppq, e.note, e.vel, e.on });
            }
        };
        feed (scope.startIndex1, scope.blockSize1);
        feed (scope.startIndex2, scope.blockSize2);
    }

    // eye: gaze follows the mouse anywhere on the screen, glow follows the sound
    const auto m = toPhoto (getLocalPoint (nullptr, Desktop::getMousePosition().toFloat()));
    auto d = m - eyeBox.getCentre();
    const float len = std::sqrt (d.x * d.x + d.y * d.y);
    gazeTarget = len > 1.0f ? d * (jmin (1.0f, len / 500.0f) / len) : Point<float>();
    gaze += (gazeTarget - gaze) * 0.15f;
    pulse += (jlimit (0.0f, 1.0f, proc.dispPeak.load() * 3.0f) - pulse) * 0.25f;
    flash *= 0.9f;
    for (auto& gl : stringGlow) gl *= 0.86f;

    if (! kbHeld.empty()) releaseKeyboardChords (false);   // key-up can be swallowed by the host
    for (size_t i = 0; i < keys.size(); ++i) litCache[i] = isLit (keys[i]);

    // repaint only what changed: the eye while it moves or burns, the keyboard when a key / LED / string changes
    const auto c = eyeBox.getCentre();
    if (gaze.getDistanceFrom (gazeTarget) > 0.004f || pulse > 0.01f || flash > 0.01f || eyeWasLive)
        repaint (toScreen ({ c.x - 340.0f, c.y - 240.0f, 680.0f, 480.0f }));
    eyeWasLive = pulse > 0.01f || flash > 0.01f;

    int64 h = downKey + 7;
    for (size_t i = 0; i < litCache.size(); ++i) if (litCache[i]) h = h * 31 + (int64) i + 1;
    for (auto gl : stringGlow) h = h * 7 + (int64) (gl * 20.0f);
    const bool msg = Time::getMillisecondCounterHiRes() < ledUntil;
    h = h * 131 + (msg ? led1.hashCode64() * 3 + led2.hashCode64() : proc.dispRoot.load() * 17 + proc.dispType.load());
    h = h * 3 + (int64) (std::fmod (Time::getMillisecondCounterHiRes() / 1000.0, 5.0) < 2.5) + take.countNotes() * 2 + (take.isFinished() ? 1 : 0) + (proc.hostPlaying.load() ? 7 : 0) + (take.isSync() ? 13 : 0);
    if (h != lastPaintHash) { lastPaintHash = h; repaint (toScreen ({ 380.0f, 680.0f, 500.0f, 360.0f })); }
}

void OmniEditor::paint (Graphics& g)
{
    const float s = scale();
    if (photoScaled.isValid()) g.drawImageAt (photoScaled, 0, 0);
    g.addTransform (AffineTransform::translation (-viewX, -viewY).scaled (s));
    g.setImageResamplingQuality (Graphics::mediumResamplingQuality);

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
            b = String (proc.apvts.getRawParameterValue (PID::hold)->load() > 0.5f ? "HOLD  " : "")
              + Params::modeNames[roundToInt (proc.apvts.getRawParameterValue (PID::mode)->load())] + "   "
              + OmniProcessor::presetName (proc.currentPreset.load());
            const bool hint = ! take.isEmpty() && ! take.isFinished() && ! take.isSounding()
                              && std::fmod (Time::getMillisecondCounterHiRes() / 1000.0, 5.0) < 2.5;
            if (hint) b = "DRAG ME > MIDI   " + String (take.countNotes()) + " NOTES";
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

        // DRAG MIDI window in the frame
        const int n = take.countNotes();
        g.setColour (Colour (0xff07090f));
        g.fillRoundedRectangle (dragBox, 5.0f);
        g.setColour (Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (dragBox.reduced (0.5f), 5.0f, 1.5f);
        g.setColour (Colours::white.withAlpha (0.18f));
        g.drawLine (dragBox.getX() + 4.0f, dragBox.getBottom() + 0.5f, dragBox.getRight() - 4.0f, dragBox.getBottom() + 0.5f, 1.0f);
        const Font mid (FontOptions (Font::getDefaultMonospacedFontName(), 15.0f, Font::bold));
        String t;
        if (take.isSync())
            t = proc.hostPlaying.load() ? "REC  " + String (n) + " NOTES" : (n > 0 ? "DRAG MIDI > BAR 1  " + String (n) : "SYNC: PRESS REC IN FL");
        else
            t = n > 0 && ! take.isFinished() ? "DRAG MIDI >  " + String (n) + " NOTES" : (n > 0 ? "DROPPED - PLAY = NEW TAKE" : "PLAY, THEN DRAG MIDI");
        draw (t, dragBox, mid, Justification::centred);
    }
}
