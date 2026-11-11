#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/AetherLookAndFeel.h"
#include "GUI/InstrumentView.h"
#include "GUI/Controls.h"

/** Slide-up "engine room" with every parameter, opened from the instrument's display. */
class EnginePanel : public juce::Component
{
public:
    explicit EnginePanel (juce::AudioProcessorValueTreeState& state);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    struct Section
    {
        juce::String title;
        std::vector<juce::Component*> items;
        juce::Rectangle<int> bounds;
    };
    juce::Component* knob   (const char* id, const juce::String& caption);
    juce::Component* choice (const char* id, const juce::String& caption, const juce::StringArray& shortNames = {});
    juce::Component* toggle (const char* id, const juce::String& caption);
    static void layoutRow (std::vector<Section>& row, juce::Rectangle<int> area, int maxCell);

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<std::vector<Section>> rows;
    juce::TextButton close { "CLOSE" };
};

/**
    The editor is the instrument: a physically rendered product shot of the
    AetherVox theremin with working knobs, a live display and playable antennas.
    Drawn at 1600 x 1000 and scaled uniformly (fixed aspect ratio, resizable).
*/
class AetherVoxEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AetherVoxEditor (AetherVoxProcessor&);
    ~AetherVoxEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int designWidth  = 1600;
    static constexpr int designHeight = 1000;

private:
    void timerCallback() override;
    void setEngineOpen (bool open);

    AetherVoxProcessor& proc;
    AetherLookAndFeel lnf;            // must outlive all child components

    juce::Component body;
    InstrumentView instrument;
    std::array<PanelKnob, 6> panelKnobs;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> knobAttachments;
    EnginePanel engine;
    juce::TooltipWindow tooltips { this, 500 };

    bool engineOpen = false;
    float enginePos = 0.0f;           // 0 = hidden, 1 = fully open
    juce::int64 lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherVoxEditor)
};
