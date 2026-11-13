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
            if (on && note >= 100) chords.strumSweep (synth, note == 100, jmax (0.5f, vel));
            else if (on) chords.strum (synth, note, vel);
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
    hostPlaying.store (ppq >= 0.0);
    pushParams();
    const int64_t clock0 = synth.clock;

    if (panic.exchange (false)) { chords.allOff (synth); keyboardState.reset(); }

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

    // --- what was played -> the live take for DRAG MIDI
    {
        const double fs = getSampleRate();
        for (int i = 0; i < synth.logCount; ++i)
        {
            const auto& e = synth.noteLog[(size_t) i];
            const int off = (int) jlimit<int64_t> (0, n - 1, e.clock - clock0);
            const auto scope = takeFifo.write (1);
            if (scope.blockSize1 > 0)
                takeBuf[(size_t) scope.startIndex1] = { (double) (samplesSeen + off) / fs,
                                                        ppq < 0.0 ? -1.0 : ppq + off * beatsPerSample, e.note, e.vel, e.on };
        }
        synth.logCount = 0;
    }
    samplesSeen += n;
    midi.clear();

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

const char* OmniProcessor::presetName (int i)
{
    static const char* n[numPresets] = { "OMNI GLASS", "TAPE DREAM", "8-BIT TEMPLE", "COSMIC HARP",
                                         "MUTANT ORGAN", "WARP ORACLE", "DARK RITUAL", "NEON ARP" };
    return n[jlimit (0, numPresets - 1, i)];
}

void OmniProcessor::applyPreset (int i)
{
    i = (i % numPresets + numPresets) % numPresets;
    currentPreset.store (i);
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    auto reset = [&]
    {
        for (auto* prm : getParameters())
            if (auto* r = dynamic_cast<RangedAudioParameter*> (prm))
            {
                const String id = r->getParameterID();
                if (id == PID::mode || id == PID::chordType || id == PID::hold || id == PID::bass || id == PID::arpRate || id == PID::arpPat) continue;
                r->beginChangeGesture(); r->setValueNotifyingHost (r->getDefaultValue()); r->endChangeGesture();
            }
    };
    reset();
    switch (i)
    {
        case 0: // OMNI GLASS - the classic omnichord: soft square-ish + sine bells, tape, space
            set (PID::oscA, 0.15f); set (PID::oscBWave, 0); set (PID::oscBSemi, 12); set (PID::oscMix, 0.4f);
            set (PID::cutoff, 3200); set (PID::release, 1.2f); set (PID::tapeMix, 0.4f); set (PID::spcMix, 0.35f); break;
        case 1: // TAPE DREAM - warm saws through a wobbly cassette
            set (PID::oscA, 0.66f); set (PID::oscMix, 0.15f); set (PID::cutoff, 1500); set (PID::drive, 0.45f);
            set (PID::attack, 0.08f); set (PID::release, 1.6f); set (PID::tapeMix, 0.6f); set (PID::tapeWow, 0.7f);
            set (PID::crushMix, 0.2f); set (PID::crushBits, 10); set (PID::spcMix, 0.4f); break;
        case 2: // 8-BIT TEMPLE - console pulses, crushed, stereo delay
            set (PID::oscA, 1.0f); set (PID::oscBWave, 3); set (PID::oscBBits, 3); set (PID::oscMix, 0.75f);
            set (PID::cutoff, 9000); set (PID::envAmt, 0.1f); set (PID::crushMix, 0.5f); set (PID::crushBits, 6);
            set (PID::crushDown, 6); set (PID::dlyMix, 0.35f); set (PID::spcMix, 0.2f); break;
        case 3: // COSMIC HARP - plucky, shimmering to the sky
            set (PID::oscA, 0.4f); set (PID::oscBWave, 6); set (PID::oscMix, 0.45f); set (PID::decay, 0.6f); set (PID::sustain, 0.0f);
            set (PID::release, 1.8f); set (PID::shimmer, 0.65f); set (PID::spcMix, 0.55f); set (PID::spcSize, 0.9f);
            set (PID::orbitMix, 0.35f); set (PID::dlyMix, 0.25f); break;
        case 4: // MUTANT ORGAN - organ table, sample&hold filter mutations, rotary-ish orbit
            set (PID::oscA, 0.3f); set (PID::oscBWave, 5); set (PID::oscMix, 0.65f); set (PID::cutoff, 2500);
            set (PID::mutAmt, 0.55f); set (PID::mutRate, 0.45f); set (PID::orbitMix, 0.6f); set (PID::orbitRate, 0.55f);
            set (PID::release, 0.4f); break;
        case 5: // WARP ORACLE - ring-modulated metal, delays
            set (PID::oscA, 0.6f); set (PID::oscBWave, 7); set (PID::oscMix, 0.5f); set (PID::warpMix, 0.45f); set (PID::warpHz, 310);
            set (PID::reso, 0.55f); set (PID::cutoff, 1800); set (PID::dlyMix, 0.4f); set (PID::dlyFb, 0.55f); set (PID::spcMix, 0.4f); break;
        case 6: // DARK RITUAL - low drones, resonant, huge space
            set (PID::oscA, 0.75f); set (PID::oscBSemi, -12); set (PID::oscMix, 0.3f); set (PID::cutoff, 700); set (PID::reso, 0.6f);
            set (PID::envAmt, 0.3f); set (PID::drive, 0.7f); set (PID::attack, 0.4f); set (PID::release, 3.0f);
            set (PID::spcMix, 0.6f); set (PID::spcSize, 0.95f); set (PID::shimmer, 0.25f); set (PID::mutAmt, 0.2f); break;
        case 7: // NEON ARP - snappy for the arpeggiator
            set (PID::oscA, 0.9f); set (PID::oscBWave, 4); set (PID::oscMix, 0.35f); set (PID::cutoff, 1100); set (PID::reso, 0.5f);
            set (PID::envAmt, 0.7f); set (PID::fDecay, 0.18f); set (PID::decay, 0.2f); set (PID::sustain, 0.25f); set (PID::release, 0.25f);
            set (PID::dlyMix, 0.35f); set (PID::spcMix, 0.25f); break;
        default: break;
    }
    if (cosmosLevel.load() > 0) setCosmos (cosmosLevel.load());
}

void OmniProcessor::setCosmos (int level)
{
    cosmosLevel.store (jlimit (0, 2, level));
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); prm->endChangeGesture(); }
    };
    const float k = level == 0 ? 0.0f : (level == 1 ? 0.45f : 1.0f);
    set (PID::shimmer, 0.7f * k); set (PID::orbitMix, 0.6f * k); set (PID::mutAmt, 0.35f * k); set (PID::warpMix, level == 2 ? 0.18f : 0.0f);
    set (PID::spcMix, 0.28f + 0.3f * k);
}

AudioProcessorEditor* OmniProcessor::createEditor() { return new OmniEditor (*this); }
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new OmniProcessor(); }
