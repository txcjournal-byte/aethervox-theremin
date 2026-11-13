#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include "DSP/ChordEngine.h"

/**
    OmniCore 3000 processor.

    MIDI map (also used by the on-screen hardware, so everything is playable from a controller):
      channels 1..14   notes  -> keyboard (through the chord engine)
      channel 16       notes 0..35 -> chord buttons  (row = note / 12: Maj, min, 7th; pitch class = note % 12)
      channel 15       notes 0..12 -> touch-plate strings (velocity = pluck strength)
*/
class OmniProcessor : public juce::AudioProcessor
{
public:
    static constexpr int chordChannel = 16, stringChannel = 15;

    OmniProcessor();
    ~OmniProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OmniCore 3000"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    /** Clicking the eye: randomise the sound into a new (musical) patch. */
    void mutate();

    /** Curated sounds (the pyramid has no knobs - presets + DAW automation). */
    static constexpr int numPresets = 8;
    static const char* presetName (int i);
    void applyPreset (int i);
    std::atomic<int> currentPreset { 0 };
    /** COSMOS key: 0 off, 1 drift, 2 full cosmos. */
    void setCosmos (int level);
    std::atomic<int> cosmosLevel { 0 };

    /** On-screen keys, chord buttons and strings write here (message thread); drained in processBlock. */
    juce::MidiKeyboardState keyboardState;

    // --- display feed (audio thread -> GUI, lock free)
    std::atomic<int>   dispRoot { -1 }, dispType { 0 }, dispVoices { 0 }, dispArpStep { 0 };
    std::atomic<float> dispPeak { 0.0f };
    std::array<std::atomic<float>, 128> dispScope {};
    std::atomic<double> dispBpm { 120.0 };

    /** Live take feed: every note the pyramid plays (mouse, MIDI, chords, strum, arp),
        stamped with seconds and, while the host plays, with the song position in quarter notes. */
    struct TakeEvent { double sec, ppq; int note; float vel; bool on; };
    juce::AbstractFifo takeFifo { 4096 };
    std::array<TakeEvent, 4096> takeBuf {};
    std::atomic<bool> hostPlaying { false };
    /** Esc on the pyramid: silence everything now. */
    std::atomic<bool> panic { false };

    /** Notes that sound right now (for lighting the keys). */
    std::array<std::atomic<bool>, 128> noteLit {};

private:
    oc::OmniSynth synth;
    oc::ChordEngine chords;
    std::vector<float> scratchR;
    int64_t samplesSeen = 0;
    juce::MidiBuffer outMidi;

    void pushParams();
    void handle (const juce::MidiMessage&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OmniProcessor)
};
