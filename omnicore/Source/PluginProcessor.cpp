#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

OmniProcessor::OmniProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OmniCore", Params::createLayout())
{
    for (auto& a : dispScope) a.store (0.0f);
    for (auto& a : noteLit) a.store (false);
}

bool OmniProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void OmniProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
    chords.prepare (sampleRate);
    scratchR.assign ((size_t) samplesPerBlock, 0.0f);
    pushParams();
}

static float pv (AudioProcessorValueTreeState& s, const char* id) { return s.getRawParameterValue (id)->load(); }

void OmniProcessor::pushParams()
{
    auto& s = apvts;
    oc::SynthParams p;
    p.oscAShape = pv (s, PID::oscA);
    p.oscBWave  = (int) pv (s, PID::oscBWave);
    p.oscBBits  = pv (s, PID::oscBBits);
    p.oscBSemi  = pv (s, PID::oscBSemi);
    p.oscMix    = pv (s, PID::oscMix);
    p.detune    = pv (s, PID::detune);
    p.noise     = pv (s, PID::noise);
    p.glide     = pv (s, PID::glide);
    p.cutoff    = pv (s, PID::cutoff);
    p.resonance = pv (s, PID::reso);
    p.envAmt    = pv (s, PID::envAmt);
    p.keyTrack  = pv (s, PID::keyTrack);
    p.drive     = pv (s, PID::drive);
    p.attack    = pv (s, PID::attack);
    p.decay     = pv (s, PID::decay);
    p.sustain   = pv (s, PID::sustain);
    p.release   = pv (s, PID::release);
    p.fAttack   = std::min (p.attack, 0.02f);
    p.fDecay    = pv (s, PID::fDecay);
    p.fSustain  = 0.2f;
    p.fRelease  = p.release;
    p.tapeMix   = pv (s, PID::tapeMix);
    p.tapeRate  = pv (s, PID::tapeRate);
    p.tapeDepth = pv (s, PID::tapeDepth);
    p.tapeWow   = pv (s, PID::tapeWow);
    p.crushBits = pv (s, PID::crushBits);
    p.crushDown = pv (s, PID::crushDown);
    p.crushMix  = pv (s, PID::crushMix);
    p.delayTime = (float) (Params::delayBeats ((int) pv (s, PID::dlySync)) * 60.0 / jmax (30.0, dispBpm.load()));
    p.delayFb   = pv (s, PID::dlyFb);
    p.delayMix  = pv (s, PID::dlyMix);
    p.delayTone = pv (s, PID::dlyTone);
    p.spaceSize = pv (s, PID::spcSize);
    p.spaceDamp = pv (s, PID::spcDamp);
    p.spaceMix  = pv (s, PID::spcMix);
    p.outputDb  = pv (s, PID::output);
    p.shimmer   = pv (s, PID::shimmer);
    p.warpMix   = pv (s, PID::warpMix);
    p.warpHz    = pv (s, PID::warpHz);
    p.mutAmt    = pv (s, PID::mutAmt);
    p.mutRate   = pv (s, PID::mutRate);
    p.orbitMix  = pv (s, PID::orbitMix);
    p.orbitRate = pv (s, PID::orbitRate);
    synth.setParams (p);

    oc::ChordSettings c;
    c.mode       = (oc::PlayMode) (int) pv (s, PID::mode);
    c.chordType  = (int) pv (s, PID::chordType);
    c.bass       = pv (s, PID::bass) > 0.5f;
    c.hold       = pv (s, PID::hold) > 0.5f;
    c.strumMs    = pv (s, PID::strumMs);
    c.pluck      = pv (s, PID::pluck);
    c.arpRate    = (int) pv (s, PID::arpRate);
    c.arpPattern = (oc::ArpPattern) (int) pv (s, PID::arpPat);
    c.arpOctaves = (int) pv (s, PID::arpOct);
    c.arpGate    = pv (s, PID::arpGate);
    chords.setSettings (synth, c);
}

void OmniProcessor::handle (const MidiMessage& m)
{
    const int ch = m.getChannel();
    if (m.isNoteOn() || m.isNoteOff())
    {
        const bool on = m.isNoteOn();
        const int note = m.getNoteNumber();
        const float vel = on ? m.getFloatVelocity() : 0.0f;
        if (ch == chordChannel)
        {
            if (note < 36) chords.chordButton (synth, note / 12, note % 12, on, on ? jmax (0.5f, vel) : 0.0f);
        }
        else if (ch == stringChannel)
        {
            if (on) chords.strum (synth, note, vel);
        }
        else chords.key (synth, note, vel, on);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        chords.allOff (synth);
}

void OmniProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if ((int) scratchR.size() < n) scratchR.assign ((size_t) n, 0.0f);

    double bpm = 120.0, ppq = -1.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) if (*b > 1.0) bpm = *b;
            if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    dispBpm.store (bpm);
    pushParams();

    // on-screen keys / chord buttons / strings join the incoming MIDI
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : scratchR.data();
    const double beatsPerSample = bpm / 60.0 / getSampleRate();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int ev = jlimit (0, n, meta.samplePosition);
        if (ev > pos)
        {
            chords.render (synth, L + pos, R + pos, ev - pos, bpm, ppq < 0.0 ? -1.0 : ppq + pos * beatsPerSample);
            pos = ev;
        }
        handle (meta.getMessage());
    }
    if (pos < n) chords.render (synth, L + pos, R + pos, n - pos, bpm, ppq < 0.0 ? -1.0 : ppq + pos * beatsPerSample);

    if (buffer.getNumChannels() == 1) { FloatVectorOperations::add (L, R, n); FloatVectorOperations::multiply (L, 0.5f, n); }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // --- display feed
    dispRoot.store (chords.currentRoot());
    dispType.store (chords.currentType());
    dispVoices.store (synth.activeVoices());
    dispArpStep.store (chords.arpStep());
    dispPeak.store (synth.peak);
    for (int i = 0; i < 128; ++i) dispScope[(size_t) i].store (synth.scope[(size_t) ((synth.scopePos + i * 4) & 511)]);
    bool lit[128]; synth.soundingNotes (lit);
    for (int i = 0; i < 128; ++i) noteLit[(size_t) i].store (lit[i]);
}

void OmniProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void OmniProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (ValueTree::fromXml (*xml));
}

void OmniProcessor::mutate()
{
    // the eye's gift: a fresh, musical random patch (sound only - play settings stay)
    Random r;
    auto set = [&] (const char* id, float lo, float hi)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            const float v = lo + (hi - lo) * r.nextFloat();
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture();
        }
    };
    set (PID::oscA, 0.0f, 1.0f);      set (PID::oscBWave, 0.0f, 7.0f);  set (PID::oscBBits, 3.0f, 8.0f);
    set (PID::oscBSemi, -12.0f, 19.0f); set (PID::oscMix, 0.1f, 0.8f);  set (PID::detune, 0.1f, 0.7f);
    set (PID::cutoff, 400.0f, 7000.0f); set (PID::reso, 0.05f, 0.7f);   set (PID::envAmt, -0.2f, 0.8f);
    set (PID::drive, 0.1f, 0.8f);     set (PID::attack, 0.001f, 0.3f); set (PID::release, 0.15f, 2.5f);
    set (PID::tapeMix, 0.0f, 0.7f);   set (PID::tapeWow, 0.0f, 0.7f);  set (PID::crushMix, 0.0f, 0.5f);
    set (PID::crushBits, 5.0f, 12.0f); set (PID::dlyMix, 0.05f, 0.45f); set (PID::spcMix, 0.1f, 0.6f);
    set (PID::shimmer, 0.0f, 0.7f);   set (PID::warpMix, 0.0f, 0.35f); set (PID::warpHz, 30.0f, 900.0f);
    set (PID::mutAmt, 0.0f, 0.6f);    set (PID::orbitMix, 0.0f, 0.7f);
}

AudioProcessorEditor* OmniProcessor::createEditor() { return new OmniEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OmniProcessor(); }
