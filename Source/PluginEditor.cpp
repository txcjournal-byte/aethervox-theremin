#include "PluginEditor.h"

using namespace juce;

namespace
{
    // the six knobs on the cabinet, left to right
    const char* const kPanelKnobs[6] = { ParamIDs::pitchTune, ParamIDs::glide, ParamIDs::vibDepth,
                                         ParamIDs::drive, ParamIDs::revMix, ParamIDs::output };
}

AetherVoxEditor::AetherVoxEditor (AetherVoxProcessor& p)
    : AudioProcessorEditor (&p), proc (p), view (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (body);
    body.setBounds (0, 0, designWidth, designHeight);

    view.setBounds (0, 0, designWidth, designHeight);
    body.addAndMakeVisible (view);

    for (int i = 0; i < 6; ++i)
    {
        auto& k = panelKnobs[(size_t) i];
        k.setBounds (Rectangle<int> (72, 72).withCentre ({ roundToInt (Photo::knobX[i] * Photo::S), roundToInt (Photo::knobY * Photo::S) }));
        body.addAndMakeVisible (k);
        knobAttachments.push_back (std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kPanelKnobs[i], k));
        if (auto* prm = p.apvts.getParameter (kPanelKnobs[i]))
            k.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }

    // DRAG MIDI: what was played with the mouse, as notes for the piano roll
    dragMidi.setBounds (36, designHeight - 84, 340, 52);
    dragMidi.noteCount = [this] { return view.getTake().countNotes(); };
    dragMidi.makeFile  = [this]
    {
        auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("Alpine Theremin take.mid");
        if (! view.getTake().writeTo (f, proc.hostBpm.load())) f.deleteFile();
        return f;
    };
    dragMidi.onReset     = [this] { view.getTake().clear(); };
    dragMidi.isSync      = [this] { return view.getTake().isSync(); };
    dragMidi.hostPlaying = [this] { return proc.hostPlaying.load(); };
    dragMidi.setSync     = [this] (bool on) { view.getTake().setSync (on); proc.apvts.state.setProperty ("takeSync", on, nullptr); };
    view.getTake().setSync ((bool) proc.apvts.state.getProperty ("takeSync", false));
    body.addAndMakeVisible (dragMidi);

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
    const bool playing = proc.hostPlaying.load();
    if (playing && ! wasHostPlaying) view.hostStarted();
    wasHostPlaying = playing;
    if (++dragRepaint % 10 == 0) dragMidi.repaint();
}

void AetherVoxEditor::paint (Graphics& g) { g.fillAll (Colours::black); }

void AetherVoxEditor::resized()
{
    body.setTransform (AffineTransform::scale ((float) getWidth() / (float) designWidth));
}
