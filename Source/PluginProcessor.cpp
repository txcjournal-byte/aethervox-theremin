#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdint>

AetherVoxProcessor::AetherVoxProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AETHERVOX", Params::createLayout())
{
    auto get = [this] (const char* id)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    using namespace ParamIDs;
    raw = { get (pitchTune), get (pitchSens), get (glide), get (quantAmount), get (scale), get (root),
            get (volSens), get (attack), get (release),
            get (coupling), get (drift),
            get (morph), get (drive), get (brightness), get (formant), get (formantMix),
            get (vibDepth), get (vibRate), get (tremolo), get (humanize),
            get (delayTime), get (delayFb), get (delayMix), get (revSize), get (revMix), get (output),
            get (mpe), get (pbRange), get (playMode), get (layer), get (sub),
            get (handPitch), get (handVol) };

    held.reserve (128);
}

// =============================================================================
void AetherVoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int maxBlock = juce::jmax (samplesPerBlock, 512);
    mono.assign ((size_t) maxBlock, 0.0f);
    scratchR.assign ((size_t) maxBlock, 0.0f);

    engine.setParameters (readParams());
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatencySamples());

    held.clear();
    for (auto& sl : slots) sl = Slot {};
    chanBend.fill (0.0f); chanPressure.fill (0.0f); chanTimbre.fill (0.5f);
    pressureSeen.fill (false); timbreSeen.fill (false);
    globalBend = 0.0f;
}

void AetherVoxProcessor::releaseResources()
{
    held.clear();
}

bool AetherVoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// =============================================================================
av::EngineParams AetherVoxProcessor::readParams() const
{
    av::EngineParams p;
    p.glideMs     = raw.glide->load();
    p.quantAmount = raw.quantAmount->load();
    p.scale       = (int) raw.scale->load();
    p.root        = (int) raw.root->load();
    p.attackMs    = raw.attack->load();
    p.releaseMs   = raw.release->load();
    p.coupling    = raw.coupling->load();
    p.drift       = raw.drift->load();
    p.morph       = raw.morph->load();
    p.drive       = raw.drive->load();
    p.brightness  = raw.brightness->load();
    p.formant     = (int) raw.formant->load();
    p.formantMix  = raw.formantMix->load();
    p.vibDepth    = raw.vibDepth->load();
    p.vibRate     = raw.vibRate->load();
    p.tremolo     = raw.tremolo->load();
    p.humanize    = raw.humanize->load();
    p.delayMs     = raw.delayTime->load();
    p.delayFb     = raw.delayFb->load();
    p.delayMix    = raw.delayMix->load();
    p.reverbSize  = raw.revSize->load();
    p.reverbMix   = raw.revMix->load();
    p.outputDb    = raw.output->load();
    p.layer       = raw.layer->load();
    p.sub         = raw.sub->load();
    return p;
}

// =============================================================================
//  MIDI
//   Note On/Off        -> monophonic, last-note priority, legato glide
//   Pitch Bend         -> global (or per-note in MPE mode)
//   CC 1  / CC 74      -> Pitch Distance  (hand to pitch antenna)
//   CC 7  / CC 11      -> Volume Distance (hand to volume loop)
//   MPE (lower zone, master ch 1, members 2-16):
//       per-note pitch bend (+/-48 st), channel pressure -> volume,
//       CC74 "slide" -> timbre (brightness + formant shift)
// =============================================================================
void AetherVoxProcessor::handleMidi (const juce::MidiMessage& m)
{
    const int ch = juce::jlimit (1, 16, m.getChannel());
    const auto ci = (size_t) (ch - 1);
    const bool mpe = raw.mpe->load() > 0.5f;

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        held.erase (std::remove_if (held.begin(), held.end(),
                                    [&] (const HeldNote& h) { return h.note == note && h.channel == ch; }),
                    held.end());
        held.push_back ({ note, ch, m.getFloatVelocity() });
        polyNoteOn (note, ch, m.getFloatVelocity());
        if (mpe && ch != 1) { pressureSeen[ci] = false; timbreSeen[ci] = false; }
    }
    else if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        polyNoteOff (note, ch);
        held.erase (std::remove_if (held.begin(), held.end(),
                                    [&] (const HeldNote& h) { return h.note == note && h.channel == ch; }),
                    held.end());
    }
    else if (m.isPitchWheel())
    {
        const float v = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
        if (mpe) chanBend[ci] = v;
        else     globalBend   = v;
    }
    else if (m.isChannelPressure())
    {
        chanPressure[ci] = (float) m.getChannelPressureValue() / 127.0f;
        pressureSeen[ci] = true;
    }
    else if (m.isAftertouch())
    {
        for (const auto& h : held)
            if (h.note == m.getNoteNumber() && h.channel == ch)
            {
                chanPressure[ci] = (float) m.getAfterTouchValue() / 127.0f;
                pressureSeen[ci] = true;
            }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        held.clear();
        for (auto& sl : slots) sl.on = false;
    }
    else if (m.isResetAllControllers())
    {
        globalBend = 0.0f; chanBend.fill (0.0f); ccVolumeSeen = false;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        const float v = (float) m.getControllerValue() / 127.0f;

        if (mpe && ch != 1 && cc == 74)        { chanTimbre[ci] = v; timbreSeen[ci] = true; }
        else if (cc == 1 || cc == 74)          { ccPitch = v; }
        else if (cc == 7 || cc == 11)          { ccVolume = v; ccVolumeSeen = true; }
    }
}

void AetherVoxProcessor::polyNoteOn (int note, int channel, float velocity)
{
    // same note retriggered -> reuse its voice
    int best = -1;
    for (int i = 0; i < (int) slots.size(); ++i)
        if (slots[(size_t) i].note == note && slots[(size_t) i].channel == channel) { best = i; break; }

    // otherwise the quietest free voice, otherwise steal the oldest held one
    if (best < 0)
    {
        float quietest = 1.0e9f;
        for (int i = 0; i < (int) slots.size(); ++i)
            if (! slots[(size_t) i].on && engine.getVoiceEnvelope (i) < quietest) { quietest = engine.getVoiceEnvelope (i); best = i; }
    }
    if (best < 0)
    {
        uint32_t oldest = UINT32_MAX;
        for (int i = 0; i < (int) slots.size(); ++i)
            if (slots[(size_t) i].age < oldest) { oldest = slots[(size_t) i].age; best = i; }
    }
    slots[(size_t) best] = { note, channel, velocity, true, ++noteCounter };
}

void AetherVoxProcessor::polyNoteOff (int note, int channel)
{
    for (auto& sl : slots)
        if (sl.on && sl.note == note && sl.channel == channel) sl.on = false;
}

float AetherVoxProcessor::noteMidi (int note, int channel, bool mpe, float tune, float pbRange) const
{
    const auto ci = (size_t) (channel - 1);
    const float bend = mpe ? (channel != 1 ? chanBend[ci] * 48.0f : 0.0f) + chanBend[0] * pbRange
                           : globalBend * pbRange;
    return (float) note + tune + bend;
}

void AetherVoxProcessor::updateTargets()
{
    const float tune     = raw.pitchTune->load();
    const float octaves  = raw.pitchSens->load();
    const float baseMidi = Params::pitchFieldBaseMidi + tune;
    const float span     = octaves * 12.0f;
    const bool  mpe      = raw.mpe->load() > 0.5f;
    const float pbRange  = raw.pbRange->load();
    const bool  poly     = raw.playMode->load() < 0.5f;
    const float gamma    = juce::jmap (raw.volSens->load(), 2.5f, 0.4f);   // volume antenna response
    auto shapeAmp = [gamma] (float a) { return std::pow (juce::jlimit (0.0f, 1.0f, a), gamma); };

    auto noteAmp = [&] (int channel, float velocity)
    {
        const auto ci = (size_t) (channel - 1);
        float a = std::pow (velocity, 0.7f);
        if (mpe && pressureSeen[ci]) a = chanPressure[ci];
        if (ccVolumeSeen)            a *= ccVolume;
        return shapeAmp (a);
    };

    float timbre = -1.0f;
    Source src = Source::Idle;

    // hands: live mouse playing, or the host playing back recorded Hand Pitch / Hand Volume automation
    const bool mouseHands = padActive.load();
    const bool autoHands  = ! mouseHands && hostPlaying.load() && raw.handVol->load() > 0.0005f;
    if (mouseHands || autoHands)
    {
        // the "hands" play voice 0 like a classic theremin; other voices fall silent
        const float hp = mouseHands ? padPitch.load()  : raw.handPitch->load();
        const float hv = mouseHands ? padVolume.load() : raw.handVol->load();
        lastMidi = baseMidi + hp * span;
        engine.setVoiceTarget (0, lastMidi, shapeAmp (hv));
        for (int v = 1; v < av::ThereminEngine::maxVoices; ++v) engine.setVoiceTarget (v, lastMidi, 0.0f);
        engine.setLeadVoice (0);
        src = Source::Pad;
    }
    else if (poly)
    {
        int leadVoice = -1; uint32_t newest = 0;
        for (int v = 0; v < (int) slots.size(); ++v)
        {
            const auto& sl = slots[(size_t) v];
            if (sl.note < 0) { engine.setVoiceTarget (v, lastMidi, 0.0f); continue; }
            const float midi = noteMidi (sl.note, sl.channel, mpe, tune, pbRange);
            engine.setVoiceTarget (v, midi, sl.on ? noteAmp (sl.channel, sl.velocity) : 0.0f);
            if (sl.on && sl.age >= newest) { newest = sl.age; leadVoice = v; }
            if (sl.on && mpe && timbreSeen[(size_t) (sl.channel - 1)]) timbre = chanTimbre[(size_t) (sl.channel - 1)];
        }
        if (leadVoice >= 0) { engine.setLeadVoice (leadVoice); src = Source::Midi; }
        else if (ccVolumeSeen && ccVolume > 0.0f)
        {
            lastMidi = baseMidi + ccPitch * span;
            engine.setVoiceTarget (0, lastMidi, shapeAmp (ccVolume));
            engine.setLeadVoice (0);
            src = Source::CC;
        }
    }
    else    // MONO: classic theremin, last-note priority with legato glide on voice 0
    {
        float midi = lastMidi, amp = 0.0f;
        if (! held.empty())
        {
            const auto& n = held.back();
            midi = noteMidi (n.note, n.channel, mpe, tune, pbRange);
            amp  = noteAmp (n.channel, n.velocity);
            if (mpe && timbreSeen[(size_t) (n.channel - 1)]) timbre = chanTimbre[(size_t) (n.channel - 1)];
            src = Source::Midi;
        }
        else if (ccVolumeSeen && ccVolume > 0.0f)
        {
            midi = baseMidi + ccPitch * span;
            amp  = shapeAmp (ccVolume);
            src  = Source::CC;
        }
        lastMidi = midi;
        engine.setVoiceTarget (0, midi, amp);
        for (int v = 1; v < av::ThereminEngine::maxVoices; ++v) engine.setVoiceTarget (v, midi, 0.0f);
        engine.setLeadVoice (0);
    }

    engine.setTimbreModulation (timbre);
    dispSource.store ((int) src);
}

// =============================================================================
void AetherVoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;

    if ((int) mono.size() < n)           // host exceeded the announced block size
    {
        mono.resize ((size_t) n);
        scratchR.resize ((size_t) n);
    }

    engine.setParameters (readParams());

    bool playingNow = false;
    if (auto* ph = getPlayHead())
        if (auto posInfo = ph->getPosition())
        {
            playingNow = posInfo->getIsPlaying();
            if (auto b = posInfo->getBpm()) if (*b > 1.0) hostBpm.store (*b);
        }
    hostPlaying.store (playingNow);

    // sample-accurate MIDI: render the voice in segments between events
    int pos = 0;
    for (const auto meta : midi)
    {
        const int ev = juce::jlimit (0, n, meta.samplePosition);
        if (ev > pos)
        {
            updateTargets();
            engine.renderVoice (mono.data() + pos, ev - pos);
            pos = ev;
        }
        handleMidi (meta.getMessage());
    }
    if (pos < n)
    {
        updateTargets();
        engine.renderVoice (mono.data() + pos, n - pos);
    }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratchR.data();
    engine.processPost (mono.data(), L, R, n);

    if (buffer.getNumChannels() == 1)            // mono output bus: fold stereo FX down
    {
        juce::FloatVectorOperations::add (L, R, n);
        juce::FloatVectorOperations::multiply (L, 0.5f, n);
    }

    for (int c = 2; c < buffer.getNumChannels(); ++c)
        buffer.clear (c, 0, n);

    pushScope (mono.data(), n);

    // display state
    const float span = raw.pitchSens->load() * 12.0f;
    const float base = Params::pitchFieldBaseMidi + raw.pitchTune->load();
    dispHz.store (engine.getEnvelope() > 0.002f ? engine.getCurrentHz() : 0.0f);   // "--" when silent
    dispPitchProx.store (juce::jlimit (0.0f, 1.0f, (engine.getCurrentMidi() - base) / span));
    dispVolume.store (engine.getEnvelope());
}

void AetherVoxProcessor::pushScope (const float* data, int n)
{
    int w = scopeWrite.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        scope[(size_t) w] = data[i];
        w = (w + 1) & (scopeSize - 1);
    }
    scopeWrite.store (w, std::memory_order_release);
}

void AetherVoxProcessor::copyScope (float* dest) const
{
    const int w = scopeWrite.load (std::memory_order_acquire);
    for (int k = 0; k < scopeSize; ++k)
        dest[k] = scope[(size_t) ((w + k) & (scopeSize - 1))];
}

// =============================================================================
void AetherVoxProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AetherVoxProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* AetherVoxProcessor::createEditor()
{
    return new AetherVoxEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AetherVoxProcessor();
}
