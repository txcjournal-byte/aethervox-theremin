#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>
#include "Parameters.h"
#include "DSP/ThereminEngine.h"
#include "EchoGuard.h"

class AetherVoxProcessor : public juce::AudioProcessor
{
public:
    AetherVoxProcessor();
    ~AetherVoxProcessor() override = default;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                    { return true; }

    const juce::String getName() const override        { return JucePlugin_Name; }
    bool acceptsMidi() const override                  { return true; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    bool supportsMPE() const override                  { return true; }
    double getTailLengthSeconds() const override       { return 12.0; }

    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override              {}
    const juce::String getProgramName (int) override   { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared with the GUI ------------------------------------------------
    juce::AudioProcessorValueTreeState apvts;

    /** 2D pad / antenna field (written by the GUI, read by audio thread). */
    std::atomic<bool>  padActive { false };
    std::atomic<float> padPitch  { 0.5f };   // 0 = far from pitch antenna, 1 = touching
    std::atomic<float> padVolume { 0.0f };   // 0 = silent, 1 = full

    /** Host transport (written by audio thread): recorded hand automation only sounds while the host plays. */
    std::atomic<bool>   hostPlaying { false };
    std::atomic<double> hostBpm     { 120.0 };
    /** Song position (quarter notes) at the last audio block + when it was taken, for the synced MIDI take. */
    std::atomic<double> songPpq     { -1.0 };
    /** 0 = DRAG MIDI, 1 = REC -> PIANO ROLL: mouse playing goes out as MIDI notes (FL records them via a loopback port). */
    std::atomic<int> recMode { 0 };
    std::atomic<double> songStampMs { 0.0 };
    double songPpqNow() const
    {
        const double q = songPpq.load();
        if (! hostPlaying.load() || q < 0.0) return -1.0;
        return q + (juce::Time::getMillisecondCounterHiRes() - songStampMs.load()) * 0.001 * hostBpm.load() / 60.0;
    }

    enum class Source { Idle = 0, Pad, Midi, CC };
    /** Display state (written by audio thread). */
    std::atomic<float> dispHz        { 0.0f };
    std::atomic<float> dispPitchProx { 0.5f };
    std::atomic<float> dispVolume    { 0.0f };
    std::atomic<int>   dispSource    { 0 };

    static constexpr int scopeSize = 2048;
    void copyScope (float* dest) const;

private:
    void handleMidi (const juce::MidiMessage& m);
    void updateTargets();
    av::EngineParams readParams() const;
    void pushScope (const float* data, int n);

    av::ThereminEngine engine;
    std::vector<float> mono, scratchR;

    // ---- MIDI / MPE state ---------------------------------------------------
    struct HeldNote { int note; int channel; float velocity; };
    std::vector<HeldNote> held;
    std::array<float, 16> chanBend {}, chanPressure {}, chanTimbre {};
    std::array<bool, 16>  pressureSeen {}, timbreSeen {};
    float globalBend = 0.0f;
    float ccPitch = 0.0f, ccVolume = 0.0f;
    bool  ccVolumeSeen = false;
    int   rpnMsb = 127, rpnLsb = 127;      // RPN selection (CC101 / CC100)
    float rpnBendRange = -1.0f;            // pitch bend range set over MIDI (RPN 0), < 0 = use the knob
    float lastMidi = 60.0f;

    // ---- cached parameter pointers -----------------------------------------
    struct Raw
    {
        std::atomic<float>* pitchTune; std::atomic<float>* pitchSens; std::atomic<float>* glide;
        std::atomic<float>* quantAmount; std::atomic<float>* scale; std::atomic<float>* root;
        std::atomic<float>* volSens; std::atomic<float>* attack; std::atomic<float>* release;
        std::atomic<float>* coupling; std::atomic<float>* drift;
        std::atomic<float>* morph; std::atomic<float>* drive; std::atomic<float>* brightness;
        std::atomic<float>* formant; std::atomic<float>* formantMix;
        std::atomic<float>* vibDepth; std::atomic<float>* vibRate; std::atomic<float>* tremolo; std::atomic<float>* humanize;
        std::atomic<float>* delayTime; std::atomic<float>* delayFb; std::atomic<float>* delayMix;
        std::atomic<float>* revSize; std::atomic<float>* revMix; std::atomic<float>* output;
        std::atomic<float>* mpe; std::atomic<float>* pbRange;
        std::atomic<float>* playMode; std::atomic<float>* layer; std::atomic<float>* sub;
        std::atomic<float>* handPitch; std::atomic<float>* handVol;
    } raw {};

    // polyphonic voice allocation
    struct Slot { int note = -1, channel = 1; float velocity = 0.0f; bool on = false; uint32_t age = 0; juce::uint64 offTime = 0; };
    std::array<Slot, av::ThereminEngine::maxVoices> slots {};
    uint32_t noteCounter = 0;
    juce::uint64 sampleClock = 0, eventTime = 0;
    // REC -> piano roll: the mouse melody as notes (back-to-back, a new note at each semitone, like DRAG MIDI)
    EchoGuard echo;
    juce::MidiBuffer hostMidi, outMidi;
    int liveNote = -1;   // for legato detection (sample time of the current MIDI event)
    void polyNoteOn (int note, int channel, float velocity);
    void polyNoteOff (int note, int channel);
    float noteMidi (int note, int channel, bool mpe, float tune, float pbRange) const;

    std::array<float, scopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AetherVoxProcessor)
};
