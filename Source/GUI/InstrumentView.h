#pragma once
// =============================================================================
//  InstrumentView - the AetherVox theremin, presented like a luxury product page
//
//  * The interface IS a physically rendered product photograph of the
//    instrument (Tools/render/ThereminRender.cpp, embedded as BinaryData).
//  * Opening sequence "sketch -> render": the instrument first appears as a
//    chalk drawing (edges extracted from the same render), then the real
//    render is wiped in left -> right with a 2 px red line on the wipe edge.
//  * Play it like a theremin with the mouse: hold the button anywhere in the
//    field and move -
//      horizontal: distance to the vertical rod (closer = higher pitch)
//      vertical:   height of the volume hand above the loop (higher = louder)
//    Releasing the button lets the note fade out with the Release time.
//  * While it sounds the field comes alive: radiating rings, beat waves
//    between fingertip and antenna (density = pitch, height = volume),
//    sparks, and a live waveform on the instrument's own display.
//  Coordinates are pixels of the 1600 x 1000 artwork; the editor scales.
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"

namespace Art
{
    constexpr float W = 1600.0f, H = 1000.0f;
    // pitch rod (projected, top -> bottom)
    constexpr float rodTopX = 216.2f, rodTopY = 56.0f, rodBotX = 257.0f, rodBotY = 640.0f;
    // volume loop (ellipse)
    constexpr float loopCX = 1285.6f, loopCY = 711.8f, loopRX = 175.6f, loopRY = 29.0f;
    // knobs
    constexpr float knobY = 816.6f, capR = 21.8f;
    constexpr float knobX[6] = { 250.4f, 347.4f, 444.4f, 800.0f, 897.0f, 994.0f };
    // display glass
    constexpr float dispX0 = 535.2f, dispY0 = 774.2f, dispX1 = 712.6f, dispY1 = 843.2f;
    // engraved wordmark under the display
    constexpr float wordX = 625.6f, wordY = 856.0f;
    // play zones
    constexpr float pitchFarX = 1480.0f, playTopY = 120.0f, playBotY = 700.0f;
}

class InstrumentView : public juce::Component
{
public:
    explicit InstrumentView (AetherVoxProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    void tick (float dtSeconds);
    std::function<void()> onDisplayClicked;

    /** x position of the red "drawing" line during the intro (artwork pixels). */
    float getRevealX() const;

private:
    enum class Grab { None, Play };
    void applyMouse (juce::Point<float>);
    void syncPad();
    static float rodXAt (float y);
    float pitchTipX (float prox, float y) const;
    float volumeTipY (float vol) const;

    void drawIntro (juce::Graphics&);
    void drawTypography (juce::Graphics&);
    void drawPitchField (juce::Graphics&, juce::Point<float> tip, float energy);
    void drawVolumeField (juce::Graphics&, juce::Point<float> tip, float energy);
    void drawFingertip (juce::Graphics&, juce::Point<float> tip, float energy);
    void drawDisplay (juce::Graphics&);
    void drawSparks (juce::Graphics&);

    AetherVoxProcessor& proc;
    juce::Image render, sketch, renderScaled;
    float renderScaledFor = 0.0f;

    float handPitch = 0.45f, handVolume = 0.0f, pitchY = 330.0f;
    Grab grab = Grab::None;

    float introT = 0.0f, wavePhase = 0.0f, ringPhase = 0.0f;
    float shownProx = 0.5f, shownVol = 0.0f, shownHz = 0.0f;
    std::array<float, AetherVoxProcessor::scopeSize> scope {};

    struct Spark { juce::Point<float> p, v; float life; };
    std::vector<Spark> sparks;
    juce::Random rng { 11 };
};

/** A knob on the instrument's front panel: the cap is part of the render,
    this draws only the turning indicator line and a subtle hover ring. */
class PanelKnob : public juce::Slider
{
public:
    PanelKnob();
    void paint (juce::Graphics&) override;
};
