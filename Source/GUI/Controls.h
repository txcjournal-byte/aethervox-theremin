#pragma once
// Small reusable labelled controls bound to the APVTS.
#include <juce_audio_processors/juce_audio_processors.h>
#include "AetherLookAndFeel.h"

class LabelledKnob : public juce::Component
{
public:
    LabelledKnob (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                  const juce::String& caption, juce::Colour accent)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, Palette::textDim.brighter (0.3f));
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (slider);

        label.setText (caption.toUpperCase(), juce::dontSendNotification);
        label.setFont (uiFont (11.0f, true));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Palette::text);
        label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramID, slider);

        if (auto* p = state.getParameter (paramID))
        {
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            slider.setTooltip (p->getName (64));
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        slider.setBounds (r.reduced (2, 0));
    }

    juce::Slider slider;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class LabelledChoice : public juce::Component
{
public:
    LabelledChoice (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                    const juce::String& caption, juce::Colour accent,
                    const juce::StringArray& shortNames = {})
    {
        // optional short display names (the host still sees the full choice names)
        if (! shortNames.isEmpty())
            box.addItemList (shortNames, 1);
        else if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramID)))
            box.addItemList (p->choices, 1);
        box.setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.55f));
        box.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (box);

        label.setText (caption.toUpperCase(), juce::dontSendNotification);
        label.setFont (uiFont (11.0f, true));
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Palette::text);
        addAndMakeVisible (label);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, paramID, box);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds (r.removeFromTop (16));
        box.setBounds (r.withSizeKeepingCentre (r.getWidth() - 6, 26).translated (0, -8));
    }

    juce::ComboBox box;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class LabelledToggle : public juce::Component
{
public:
    LabelledToggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramID,
                    const juce::String& caption, juce::Colour accent)
    {
        button.setButtonText (caption.toUpperCase());
        button.setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (button);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramID, button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
