#pragma once
// =============================================================================
//  BabickaView - "Babiččin Theremin"
//
//  The interface is a photograph of a folk grandmother in her cottage playing
//  a giant 1929-style theremin. She is animated from pose photographs:
//    * her pitch hand travels along the rod with the note being played
//      (25 optical-flow in-between frames, low -> high),
//    * her volume hand rises and sinks above the loop with the loudness
//      (13 frames), and she blinks now and then (5 frames).
//  Each animated region is a small masked crop drawn over the base photo, so it
//  stays cheap: everything is pre-scaled once per display scale.
//  The six knobs on the cabinet are real controls, the amber meter is live.
//  Coordinates are pixels of the 1158 x 652 photo; the view is drawn at
//  1600 design pixels wide (Photo::S) and the editor scales the whole body.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace Photo
{
    constexpr float W = 1158.0f, H = 652.0f;
    constexpr float S = 1600.0f / W;                       // photo px -> design px
    constexpr float rodX = 343.0f, rodTopY = 95.0f, rodBotY = 318.0f;
    constexpr float loopCX = 824.0f, loopCY = 296.0f, loopRX = 68.0f, loopRY = 14.0f;
    constexpr float knobY = 416.0f, capR = 15.0f;
    constexpr float knobX[6] = { 405.0f, 465.7f, 525.7f, 586.7f, 647.3f, 707.7f };
    constexpr float meterX0 = 532.0f, meterY0 = 356.0f, meterX1 = 581.0f, meterY1 = 381.0f;
    constexpr float playFarX = 1100.0f;                    // far right = lowest pitch
}

class BabickaView : public juce::Component
{
public:
    explicit BabickaView (AetherVoxProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    void tick (float dtSeconds);

    static constexpr int designW = 1600;
    static constexpr int designH = 901;

private:
    struct Layer
    {
        juce::Rectangle<int> region;               // in photo pixels
        std::vector<juce::Image> frames;           // ARGB, alpha = soft mask, photo resolution
        std::vector<juce::Image> scaled;           // same, pre-scaled to physical pixels
    };
    void loadLayer (Layer&, const char* prefix, int count, const char* maskName, juce::Rectangle<int> region);
    void rescaleIfNeeded (float physicalScale);
    void drawLayer (juce::Graphics&, const Layer&, float pose) const;
    void drawGlow (juce::Graphics&);
    void drawMeter (juce::Graphics&);
    void applyMouse (juce::Point<float> designPos);
    void syncPad();

    AetherVoxProcessor& proc;
    juce::Image base, baseScaled;
    float scaledFor = 0.0f;
    Layer pitchLayer, volLayer, blinkLayer;

    // animation state
    float t = 0.0f, fadeIn = 0.0f;
    float pitchPose = 0.45f, volPose = 0.8f, blinkPose = 0.0f;
    float nextBlink = 2.5f, blinkT = -1.0f;
    float shownVol = 0.0f, shownHz = 0.0f, needle = 0.0f;
    juce::Random rng { 1929 };

    // mouse playing
    bool playing = false;
    float handPitch = 0.5f, handVolume = 0.0f;
};

/** A knob on the cabinet: the bakelite cap is part of the photo, this draws only
    the turning indicator and a value arc while touched. */
class PanelKnob : public juce::Slider
{
public:
    PanelKnob();
    void paint (juce::Graphics&) override;
};
