#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/AetherLookAndFeel.h"
#include "GUI/BabickaView.h"

/**
    Alpine Theremin (TrapWin): the whole interface is one photograph - a folk
    grandmother playing a giant 1929 theremin in her cottage. She is animated by
    the music, and the six knobs on the cabinet are the only controls.
    Drawn at 1600 x 901 and scaled uniformly.
*/
class AetherVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AetherVoxEditor (AetherVoxProcessor&);
    ~AetherVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth  = BabickaView::designW;
    static constexpr int designHeight = BabickaView::designH;

private:
    void timerCallback() override;

    AetherVoxProcessor& proc;
    AetherLookAndFeel lnf;            // must outlive all child components

    juce::Component body;
    BabickaView view;
    std::array<PanelKnob, 6> panelKnobs;
    DragMidiButton dragMidi;
    RecModeButton recMode;
    bool wasHostPlaying = false;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    juce::int64 lastTick = 0;
    int dragRepaint = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherVoxEditor)
};
