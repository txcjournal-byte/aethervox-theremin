#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/AetherLookAndFeel.h"
#include "GUI/BabickaView.h"
#include "GUI/Controls.h"

/** The row of settings under the photo, framed by a folk embroidery band. */
class ControlStrip : public juce::Component
{
public:
    explicit ControlStrip (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Component* knob   (const char* id, const juce::String& caption);
    juce::Component* choice (const char* id, const juce::String& caption, const juce::StringArray& shortNames = {});

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<juce::Component*> order;
};

/**
    Babiččin Theremin: an animated photograph of a folk grandmother playing a
    giant 1929 theremin, with the cabinet's six knobs as real controls and a
    settings strip below. Drawn at 1600 x 1081 and scaled uniformly.
*/
class AetherVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AetherVoxEditor (AetherVoxProcessor&);
    ~AetherVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int stripHeight  = 180;
    static constexpr int designWidth  = BabickaView::designW;
    static constexpr int designHeight = BabickaView::designH + stripHeight;

private:
    void timerCallback() override;

    AetherVoxProcessor& proc;
    AetherLookAndFeel lnf;            // must outlive all child components

    juce::Component body;
    BabickaView view;
    std::array<PanelKnob, 6> panelKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    ControlStrip strip;
    juce::TooltipWindow tooltips { this, 500 };
    juce::int64 lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherVoxEditor)
};
