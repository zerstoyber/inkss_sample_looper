#include "SampleEngine.h"

namespace
{
float equalPowerLeft (float pan)
{
    const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f)
                        * juce::MathConstants<float>::halfPi * 0.5f;
    return std::cos (angle);
}

float equalPowerRight (float pan)
{
    const float angle = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f)
                        * juce::MathConstants<float>::halfPi * 0.5f;
    return std::sin (angle);
}

void wrapLoopPosition (double& position, double loopStart, double loopEnd)
{
    const double span = loopEnd - loopStart;

    if (span < 1.0)
        return;

    while (position >= loopEnd)
        position -= span;

    while (position < loopStart)
        position += span;
}

double beatsForDivision (int division)
{
    switch (division)
    {
        case 1: return 4.0;
        case 2: return 2.0;
        case 3: return 1.0;
        case 4: return 0.5;
        case 5: return 0.25;
        default: return 0.0;
    }
}

double nextQuantizePoint (double ppq, double beats)
{
    if (beats <= 0.0)
        return ppq;

    return std::ceil ((ppq + 0.01) / beats) * beats;
}

double barsToRecord (int division)
{
    switch (division)
    {
        case 1: return 1.0;
        case 2: return 2.0;
        case 3: return 4.0;
        case 4: return 8.0;
        case 5: return 16.0;
        default: return 0.0;
    }
}
}

SampleEngine::SampleEngine()
{
    formatManager.registerBasicFormats();
   #if JUCE_MAC || JUCE_IOS
    formatManager.registerFormat (new juce::CoreAudioFormat(), false);
   #endif

    for (auto& playing : slotPlaying)
        playing.store (false);

    for (auto& position : slotPosition)
        position.store (-1);

    finishedTakes.reserve (8);
}

void SampleEngine::prepare (double sampleRate, int)
{
    if (sampleRate < 1.0)
        sampleRate = 44100.0;

    hostSampleRate = sampleRate;
    fadeStep = 1.0f / juce::jmax (1.0f, (float) (0.015 * sampleRate));
    gainCoeff = 1.0f - std::exp (-1.0f / (float) (0.01 * sampleRate));
    smoothedGain.reset (sampleRate, 0.02);
    smoothedGain.setCurrentAndTargetValue (0.6f);

    if (capturingInput || capturingOutput || liveTake != nullptr)
        finalizeCapture();

    for (auto& voice : voices)
    {
        voice.audio.reset();
        voice.active = false;
        voice.releasing = false;
        voice.sustained = false;
        voice.preview = false;
        voice.env = 0.0f;
    }

    for (auto& playing : slotPlaying)
        playing.store (false);

    for (auto& position : slotPosition)
        position.store (-1);

    const juce::SpinLock::ScopedLockType guard (retiredLock);
    retired.reserve (64);
}

void SampleEngine::reset()
{
    if (capturingInput || capturingOutput || liveTake != nullptr)
        finalizeCapture();

    sustain = false;

    for (auto& voice : voices)
    {
        voice.audio.reset();
        voice.active = false;
        voice.releasing = false;
        voice.sustained = false;
        voice.preview = false;
        voice.env = 0.0f;
        voice.pos = 0.0;
    }

    for (auto& playing : slotPlaying)
        playing.store (false);

    for (auto& position : slotPosition)
        position.store (-1);
}

int SampleEngine::loadFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFileName();
        return -1;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        error = "Cannot read " + file.getFileName();
        return -1;
    }

    if (reader->lengthInSamples < 2)
    {
        error = file.getFileName() + " is empty.";
        return -1;
    }

    const auto maxSamples = (juce::int64) std::llround (reader->sampleRate * 60.0 * 8.0);

    if (reader->lengthInSamples > maxSamples)
    {
        error = file.getFileName() + " is longer than 8 minutes.";
        return -1;
    }

    const int length = (int) reader->lengthInSamples;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    auto buffer = std::make_shared<juce::AudioBuffer<float>> (channels, length);

    if (! reader->read (buffer.get(), 0, length, 0, true, true))
    {
        error = "Could not decode " + file.getFileName();
        return -1;
    }

    const int loopEnd = findBestLoopEnd (*buffer, reader->sampleRate);
    int slotIndex = -1;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);
        slotIndex = appendSlot();

        if (slotIndex < 0)
        {
            error = "Sixteen samples are already loaded.";
            return -1;
        }

        auto& slot = slots[(size_t) slotIndex];
        slot.name = file.getFileNameWithoutExtension();
        slot.path = file.getFullPathName();
        slot.gain.store (1.0f);
        slot.pan.store (0.0f);
        slot.tune.store (0.0f);
        slot.fine.store (0.0f);
        slot.lowDb.store (0.0f);
        slot.midDb.store (0.0f);
        slot.highDb.store (0.0f);
        slot.attackSeconds.store (0.01f);
        slot.releaseSeconds.store (0.05f);
        slot.armed.store (false);
        slot.synced.store (false);
        slot.reversed.store (false);
        slot.speed.store (false);
        slot.playQuantize.store (0);
        slot.loopStart.store (0);
        slot.loopEnd.store (loopEnd);
        slot.length.store (length);
        slot.sourceSampleRate.store (reader->sampleRate);
        slot.audio = std::move (buffer);
        slot.epoch.fetch_add (1);
    }

    return slotIndex;
}

void SampleEngine::clearSlot (int index)
{
    if (index < 0 || index >= maxSlots)
        return;

    std::shared_ptr<juce::AudioBuffer<float>> dropped;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);
        auto& slot = slots[(size_t) index];
        dropped = std::move (slot.audio);
        slot.name.clear();
        slot.path.clear();
        slot.length.store (0);
        slot.epoch.fetch_add (1);
        packSlots();
    }
}

void SampleEngine::moveSlot (int index, int direction)
{
    if (index < 0 || index >= maxSlots || direction == 0)
        return;

    const juce::SpinLock::ScopedLockType guard (slotLock);

    if (slots[(size_t) index].audio == nullptr)
        return;

    const int step = direction < 0 ? -1 : 1;
    int target = index + step;

    while (target >= 0 && target < maxSlots && slots[(size_t) target].audio == nullptr)
        target += step;

    if (target < 0 || target >= maxSlots)
        return;

    exchangeSlots (index, target);
}

bool SampleEngine::canMoveSlot (int index, int direction) const
{
    if (index < 0 || index >= maxSlots || direction == 0)
        return false;

    const juce::SpinLock::ScopedLockType guard (slotLock);

    if (slots[(size_t) index].audio == nullptr)
        return false;

    const int step = direction < 0 ? -1 : 1;

    for (int target = index + step; target >= 0 && target < maxSlots; target += step)
        if (slots[(size_t) target].audio != nullptr)
            return true;

    return false;
}

void SampleEngine::exchangeSlots (int first, int second)
{
    auto& a = slots[(size_t) first];
    auto& b = slots[(size_t) second];
    std::swap (a.audio, b.audio);
    std::swap (a.name, b.name);
    std::swap (a.path, b.path);

    auto exchange = [] (auto& left, auto& right)
    {
        const auto value = left.load();
        left.store (right.load());
        right.store (value);
    };

    exchange (a.gain, b.gain);
    exchange (a.pan, b.pan);
    exchange (a.tune, b.tune);
    exchange (a.fine, b.fine);
    exchange (a.attackSeconds, b.attackSeconds);
    exchange (a.releaseSeconds, b.releaseSeconds);
    exchange (a.lowDb, b.lowDb);
    exchange (a.midDb, b.midDb);
    exchange (a.highDb, b.highDb);
    exchange (a.armed, b.armed);
    exchange (a.synced, b.synced);
    exchange (a.reversed, b.reversed);
    exchange (a.speed, b.speed);
    exchange (a.playQuantize, b.playQuantize);
    exchange (a.loopStart, b.loopStart);
    exchange (a.loopEnd, b.loopEnd);
    exchange (a.length, b.length);
    exchange (a.sourceSampleRate, b.sourceSampleRate);
    exchange (a.epoch, b.epoch);

    auto follow = [first, second] (int& slot)
    {
        if (slot == first)
            slot = second;
        else if (slot == second)
            slot = first;
    };

    for (auto& voice : voices)
        follow (voice.slotIndex);

    for (auto& pending : pendingVoices)
        follow (pending.trigger.slot);
}

void SampleEngine::packSlots()
{
    int write = 0;

    for (int read = 0; read < maxSlots; ++read)
    {
        if (slots[(size_t) read].audio == nullptr)
            continue;

        if (write != read)
            exchangeSlots (write, read);

        ++write;
    }
}

int SampleEngine::appendSlot()
{
    int last = -1;

    for (int i = 0; i < maxSlots; ++i)
        if (slots[(size_t) i].audio != nullptr)
            last = i;

    if (last + 1 < maxSlots)
        return last + 1;

    packSlots();

    for (int i = 0; i < maxSlots; ++i)
        if (slots[(size_t) i].audio == nullptr)
            return i;

    return -1;
}

void SampleEngine::clearAll()
{
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> dropped;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);

        for (auto& slot : slots)
        {
            if (slot.audio != nullptr)
                dropped.push_back (std::move (slot.audio));

            slot.name.clear();
            slot.path.clear();
            slot.length.store (0);
            slot.epoch.fetch_add (1);
        }
    }
}

void SampleEngine::setSlotGain (int index, float gain)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].gain.store (juce::jlimit (0.0f, 1.0f, gain));
}

void SampleEngine::setSlotPan (int index, float pan)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].pan.store (juce::jlimit (-1.0f, 1.0f, pan));
}

void SampleEngine::setSlotTune (int index, float semitones)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].tune.store (juce::jlimit (-24.0f, 24.0f, semitones));
}

void SampleEngine::setSlotFine (int index, float cents)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].fine.store (juce::jlimit (-100.0f, 100.0f, cents));
}

void SampleEngine::setSlotEq (int index, int band, float db)
{
    if (index < 0 || index >= maxSlots || band < 0 || band > 2)
        return;

    const float limited = juce::jlimit (-24.0f, 24.0f, db);
    auto& slot = slots[(size_t) index];

    if (band == 0)
        slot.lowDb.store (limited);
    else if (band == 1)
        slot.midDb.store (limited);
    else
        slot.highDb.store (limited);
}

void SampleEngine::setSlotAttack (int index, float seconds)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].attackSeconds.store (juce::jlimit (0.0f, 4.0f, seconds));
}

void SampleEngine::setSlotRelease (int index, float seconds)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].releaseSeconds.store (juce::jlimit (0.0f, 4.0f, seconds));
}

void SampleEngine::setSlotName (int index, const juce::String& name)
{
    if (index < 0 || index >= maxSlots)
        return;

    const auto cleaned = name.trim().substring (0, 80);

    if (cleaned.isEmpty())
        return;

    const juce::SpinLock::ScopedLockType guard (slotLock);

    if (slots[(size_t) index].audio != nullptr)
        slots[(size_t) index].name = cleaned;
}

void SampleEngine::setSlotArmed (int index, bool armed)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].armed.store (armed);
}

void SampleEngine::setSlotSync (int index, bool synced)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].synced.store (synced);
}

void SampleEngine::setSlotReverse (int index, bool reversed)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].reversed.store (reversed);
}

void SampleEngine::setSlotSpeed (int index, bool speed)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].speed.store (speed);
}

void SampleEngine::setSlotPlayQuantize (int index, int division)
{
    if (index >= 0 && index < maxSlots)
        slots[(size_t) index].playQuantize.store (juce::jlimit (0, 5, division));
}

void SampleEngine::setSlotLoop (int index, int start, int end)
{
    if (index < 0 || index >= maxSlots)
        return;

    auto& slot = slots[(size_t) index];
    const int length = slot.length.load();

    if (length < 2)
        return;

    const int gap = juce::jmax (2, juce::jmin (64, length / 8));
    start = juce::jlimit (0, length - gap, start);
    end = juce::jlimit (start + gap, length, end);
    slot.loopStart.store (start);
    slot.loopEnd.store (end);
}

std::vector<float> SampleEngine::copyPeaks (int index, int buckets) const
{
    std::shared_ptr<juce::AudioBuffer<float>> audio;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);

        if (index < 0 || index >= maxSlots)
            return {};

        audio = slots[(size_t) index].audio;
    }

    if (audio == nullptr || audio->getNumSamples() < 2)
        return {};

    const int length = audio->getNumSamples();
    const int count = juce::jlimit (16, 256, buckets);
    std::vector<float> peaks ((size_t) count, 0.0f);
    const float* left = audio->getReadPointer (0);
    const float* right = audio->getNumChannels() > 1 ? audio->getReadPointer (1) : left;

    for (int i = 0; i < count; ++i)
    {
        const int first = (int) ((juce::int64) i * length / count);
        const int last = juce::jmin (length, juce::jmax (first + 1, (int) ((juce::int64) (i + 1) * length / count)));
        float peak = 0.0f;

        for (int sample = first; sample < last; ++sample)
            peak = juce::jmax (peak, std::fabs (left[sample]), std::fabs (right[sample]));

        peaks[(size_t) i] = peak;
    }

    return peaks;
}

SampleEngine::SlotInfo SampleEngine::getSlotInfo (int index) const
{
    SlotInfo info;

    if (index < 0 || index >= maxSlots)
        return info;

    const juce::SpinLock::ScopedLockType guard (slotLock);
    const auto& slot = slots[(size_t) index];
    info.occupied = slot.audio != nullptr;
    info.name = slot.name;
    info.path = slot.path;
    info.gain = slot.gain.load();
    info.pan = slot.pan.load();
    info.tune = slot.tune.load();
    info.fine = slot.fine.load();
    info.lowDb = slot.lowDb.load();
    info.midDb = slot.midDb.load();
    info.highDb = slot.highDb.load();
    info.attackSeconds = slot.attackSeconds.load();
    info.releaseSeconds = slot.releaseSeconds.load();
    info.armed = slot.armed.load();
    info.synced = slot.synced.load();
    info.reversed = slot.reversed.load();
    info.speed = slot.speed.load();
    info.playQuantize = slot.playQuantize.load();
    info.length = slot.length.load();
    info.loopStart = slot.loopStart.load();
    info.loopEnd = slot.loopEnd.load();

    const double rate = slot.sourceSampleRate.load();
    info.durationSeconds = rate > 0.0 ? (double) info.length / rate : 0.0;
    return info;
}

int SampleEngine::getOccupiedCount() const
{
    const juce::SpinLock::ScopedLockType guard (slotLock);
    int count = 0;

    for (const auto& slot : slots)
        if (slot.audio != nullptr)
            ++count;

    return count;
}

std::unique_ptr<juce::XmlElement> SampleEngine::createStateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> ("SAMPLES");
    const juce::SpinLock::ScopedLockType guard (slotLock);

    for (const auto& slot : slots)
    {
        if (slot.audio == nullptr)
            continue;

        auto* element = xml->createNewChildElement ("SAMPLE");
        element->setAttribute ("path", slot.path);
        element->setAttribute ("gain", (double) slot.gain.load());
        element->setAttribute ("pan", (double) slot.pan.load());
        element->setAttribute ("tune", (double) slot.tune.load());
        element->setAttribute ("fine", (double) slot.fine.load());
        element->setAttribute ("low", (double) slot.lowDb.load());
        element->setAttribute ("mid", (double) slot.midDb.load());
        element->setAttribute ("high", (double) slot.highDb.load());
        element->setAttribute ("attack", (double) slot.attackSeconds.load());
        element->setAttribute ("release", (double) slot.releaseSeconds.load());
        element->setAttribute ("name", slot.name);
        element->setAttribute ("armed", slot.armed.load());
        element->setAttribute ("sync", slot.synced.load());
        element->setAttribute ("reverse", slot.reversed.load());
        element->setAttribute ("speed", slot.speed.load());
        element->setAttribute ("playQuantize", slot.playQuantize.load());
        element->setAttribute ("loopStart", slot.loopStart.load());
        element->setAttribute ("loopEnd", slot.loopEnd.load());
    }

    return xml;
}

juce::String SampleEngine::restoreFromXml (const juce::XmlElement& xml)
{
    clearAll();
    juce::String missing;

    for (auto* element = xml.getFirstChildElement(); element != nullptr; element = element->getNextElement())
    {
        if (! element->hasTagName ("SAMPLE"))
            continue;

        const juce::File file (element->getStringAttribute ("path"));
        juce::String error;
        const int slot = loadFile (file, error);

        if (slot < 0)
        {
            missing << file.getFileName() << "  ";
            continue;
        }

        setSlotGain (slot, (float) element->getDoubleAttribute ("gain", 1.0));
        setSlotPan (slot, (float) element->getDoubleAttribute ("pan", 0.0));
        setSlotTune (slot, (float) element->getDoubleAttribute ("tune", 0.0));
        setSlotFine (slot, (float) element->getDoubleAttribute ("fine", 0.0));
        setSlotEq (slot, 0, (float) element->getDoubleAttribute ("low", 0.0));
        setSlotEq (slot, 1, (float) element->getDoubleAttribute ("mid", 0.0));
        setSlotEq (slot, 2, (float) element->getDoubleAttribute ("high", 0.0));
        setSlotAttack (slot, (float) element->getDoubleAttribute ("attack", 0.01));
        setSlotRelease (slot, (float) element->getDoubleAttribute ("release", 0.05));
        setSlotName (slot, element->getStringAttribute ("name"));
        setSlotArmed (slot, element->getBoolAttribute ("armed", false));
        setSlotSync (slot, element->getBoolAttribute ("sync", true));
        setSlotReverse (slot, element->getBoolAttribute ("reverse", false));
        setSlotSpeed (slot, element->getBoolAttribute ("speed", false));
        setSlotPlayQuantize (slot, element->getIntAttribute ("playQuantize", 0));

        if (element->hasAttribute ("loopEnd"))
            setSlotLoop (slot, element->getIntAttribute ("loopStart", 0), element->getIntAttribute ("loopEnd"));
    }

    return missing;
}

void SampleEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                            float globalGain, float globalTuneSemitones, bool shouldLoop, bool shouldSync,
                            double bpm, double ppq, bool playing, int root, int numInputChannels,
                            float eqLowDb, float eqMidDb, float eqHighDb)
{
    juce::ScopedNoDenormals noDenormals;
    globalTune = globalTuneSemitones;
    loop = shouldLoop;
    sync = shouldSync;
    hostBpm = bpm > 1.0 ? bpm : 120.0;
    hostPpq = ppq;
    hostPlaying = playing;
    renderBlockSamples = buffer.getNumSamples();
    rootNote = root;
    masterLowDb = eqLowDb;
    masterMidDb = eqMidDb;
    masterHighDb = eqHighDb;
    smoothedGain.setTargetValue (juce::jlimit (0.0f, 1.5f, globalGain));

    drainCommands();

    const int blockSamples = buffer.getNumSamples();
    launchQuantized (blockSamples);

    int captureFirst = captureOffset;
    int captureLast = blockSamples;
    bool finishCapture = false;

    if (stopOnGrid && (capturingInput || capturingOutput))
    {
        const double perSample = (hostBpm / 60.0) / juce::jmax (1.0, hostSampleRate);
        const double blockEnd = hostPpq + perSample * (double) blockSamples;
        const bool clock = sync && hostPlaying && hostPpq >= 0.0 && captureQuantizeBeats > 0.0;

        if (! clock || stopAtPpq <= blockEnd)
        {
            const int stopSample = clock ? juce::jlimit (captureFirst, blockSamples,
                                                          (int) std::ceil ((stopAtPpq - hostPpq) / juce::jmax (1.0e-12, perSample)))
                                         : blockSamples;
            captureLast = stopSample;
            finishCapture = true;
            stopOnGrid = false;
            stopWaiting.store (false);
        }
    }

    if (capturingInput)
        appendCapture (buffer, numInputChannels, captureFirst, captureLast - captureFirst);

    buffer.clear();

    const int numSamples = blockSamples;
    int position = 0;

    for (const auto metadata : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (eventPos > position)
        {
            render (buffer, position, eventPos);
            position = eventPos;
        }

        handleMidi (metadata.getMessage(), eventPos);
    }

    if (position < numSamples)
        render (buffer, position, numSamples);

    applyMasterEq (buffer);

    if (capturingOutput)
        appendCapture (buffer, buffer.getNumChannels(), captureFirst, captureLast - captureFirst);

    if (finishCapture)
        finalizeCapture();

    captureOffset = 0;

    publishSlotActivity();
    midi.clear();
}

void SampleEngine::collectRetired()
{
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> dropped;

    {
        const juce::SpinLock::ScopedLockType guard (retiredLock);
        dropped.swap (retired);
    }

    {
        const juce::SpinLock::ScopedLockType guard (retiredLock);

        if (retired.capacity() < 64)
            retired.reserve (64);
    }
}

void SampleEngine::handleMidi (const juce::MidiMessage& message, int samplePosition)
{
    if (message.isNoteOn())
    {
        if (message.getVelocity() == 0)
            noteOff (message.getNoteNumber());
        else
            noteOn (message.getNoteNumber(), message.getFloatVelocity(), samplePosition);
    }
    else if (message.isNoteOff())
    {
        noteOff (message.getNoteNumber());
    }
    else if (message.isAllSoundOff())
    {
        killAll();
    }
    else if (message.isAllNotesOff())
    {
        releaseAll();
    }
    else if (message.isSustainPedalOn())
    {
        sustain = true;
    }
    else if (message.isSustainPedalOff())
    {
        sustain = false;

        for (int i = (int) pendingVoices.size(); --i >= 0;)
            if (! pendingVoices[(size_t) i].preview && pendingVoices[(size_t) i].released)
                pendingVoices.erase (pendingVoices.begin() + i);

        for (auto& voice : voices)
        {
            if (voice.active && voice.sustained)
            {
                voice.sustained = false;
                voice.releasing = true;
                voice.shortRelease = voice.preview;
            }
        }
    }
}

void SampleEngine::noteOn (int note, float velocity, int samplePosition)
{
    struct Hit
    {
        Trigger trigger;
        int quantize = 0;
    };

    std::array<Hit, maxSlots> hits;
    int count = 0;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);

        for (int i = 0; i < maxSlots; ++i)
        {
            auto& slot = slots[(size_t) i];

            if (slot.audio == nullptr || ! slot.armed.load())
                continue;

            auto& hit = hits[(size_t) count++];
            hit.quantize = slot.playQuantize.load();
            hit.trigger.audio = slot.audio;
            hit.trigger.slot = i;
            hit.trigger.epoch = slot.epoch.load();
            hit.trigger.loopStart = slot.loopStart.load();
            hit.trigger.loopEnd = slot.loopEnd.load();
            hit.trigger.length = slot.length.load();
            hit.trigger.sourceRate = slot.sourceSampleRate.load();
            hit.trigger.gain = slot.gain.load();
            hit.trigger.pan = slot.pan.load();
            hit.trigger.tune = slot.tune.load();
            hit.trigger.synced = slot.synced.load();
            hit.trigger.reversed = slot.reversed.load();
        }
    }

    const float vel = juce::jlimit (0.0f, 1.0f, velocity);
    const double perSample = (hostBpm / 60.0) / juce::jmax (1.0, hostSampleRate);
    const bool clock = sync && hostPlaying && hostPpq >= 0.0 && hostBpm > 1.0 && perSample > 0.0;
    const double notePpq = hostPpq + perSample * (double) juce::jmax (0, samplePosition);

    for (int i = 0; i < count; ++i)
    {
        auto& hit = hits[(size_t) i];

        for (int pending = (int) pendingVoices.size(); --pending >= 0;)
            if (! pendingVoices[(size_t) pending].preview
                && pendingVoices[(size_t) pending].note == note
                && pendingVoices[(size_t) pending].trigger.slot == hit.trigger.slot)
                pendingVoices.erase (pendingVoices.begin() + pending);

        const double beats = clock ? beatsForDivision (hit.quantize) : 0.0;

        if (beats <= 0.0)
        {
            startVoice (hit.trigger, note, vel, false);
            continue;
        }

        const double boundary = nextQuantizePoint (notePpq, beats);
        const int boundarySample = juce::jmax (samplePosition,
                                                (int) std::ceil ((boundary - hostPpq) / perSample));

        if (boundarySample < renderBlockSamples)
        {
            startVoice (hit.trigger, note, vel, false, boundarySample - samplePosition);
            continue;
        }

        PendingVoice item;
        item.trigger = std::move (hit.trigger);
        item.startPpq = boundary;
        item.note = note;
        item.velocity = vel;
        item.preview = false;
        pendingVoices.push_back (std::move (item));
    }
}

void SampleEngine::noteOff (int note)
{
    for (int i = (int) pendingVoices.size(); --i >= 0;)
    {
        auto& item = pendingVoices[(size_t) i];

        if (item.preview || item.note != note)
            continue;

        if (sustain)
            item.released = true;
        else
            pendingVoices.erase (pendingVoices.begin() + i);
    }

    for (auto& voice : voices)
    {
        if (! voice.active || voice.preview || voice.note != note)
            continue;

        if (voice.delaySamples > 0 && ! sustain)
        {
            if (voice.audio != nullptr)
                retire (std::move (voice.audio));

            voice.active = false;
            voice.releasing = false;
            voice.sustained = false;
            voice.env = 0.0f;
            continue;
        }

        if (sustain)
            voice.sustained = true;
        else
        {
            voice.releasing = true;
            voice.shortRelease = false;
        }
    }
}

void SampleEngine::releaseAll()
{
    pendingVoices.clear();

    for (auto& voice : voices)
        if (voice.active)
        {
            voice.releasing = true;
            voice.shortRelease = voice.preview;
        }
}

void SampleEngine::killAll()
{
    pendingVoices.clear();

    for (auto& voice : voices)
    {
        if (voice.audio != nullptr)
            retire (std::move (voice.audio));

        voice.active = false;
        voice.releasing = false;
        voice.sustained = false;
        voice.preview = false;
        voice.env = 0.0f;
    }
}

void SampleEngine::startVoice (const Trigger& trigger, int note, float velocity, bool preview, int delaySamples)
{
    Voice* voice = nullptr;

    for (auto& candidate : voices)
    {
        if (candidate.active && candidate.preview == preview && candidate.note == note
            && candidate.slotIndex == trigger.slot)
        {
            voice = &candidate;
            break;
        }
    }

    if (voice == nullptr)
        voice = allocateVoice();

    if (voice == nullptr || trigger.audio == nullptr)
        return;

    if (voice->audio != nullptr && voice->audio.get() != trigger.audio.get())
        retire (std::move (voice->audio));

    voice->audio = trigger.audio;
    voice->note = note;
    voice->slotIndex = trigger.slot;
    voice->epoch = trigger.epoch;
    voice->length = juce::jmax (1, trigger.length);
    voice->loopStart = juce::jlimit (0, juce::jmax (0, voice->length - 2), trigger.loopStart);
    voice->loopEnd = juce::jlimit (voice->loopStart + 1, voice->length, juce::jmax (voice->loopStart + 1, trigger.loopEnd));
    voice->sourceRate = trigger.sourceRate > 0.0 ? trigger.sourceRate : 44100.0;
    voice->age = ageCounter++;
    voice->reversed = trigger.reversed;
    const bool inLoop = loop && voice->loopEnd > voice->loopStart;
    voice->pos = voice->reversed
                     ? (inLoop ? (double) voice->loopEnd - 1.0 : (double) voice->length - 1.0)
                     : (inLoop ? (double) voice->loopStart : 0.0);
    voice->delaySamples = juce::jmax (0, delaySamples);
    voice->headA = voice->pos;
    voice->headB = voice->pos;
    voice->grainPhase = 0.0;
    voice->stretch = false;
    voice->velocity = velocity;
    voice->gain = trigger.gain;
    voice->appliedGain = trigger.gain;
    voice->pan = trigger.pan;
    voice->panL = equalPowerLeft (trigger.pan);
    voice->panR = equalPowerRight (trigger.pan);
    voice->sampleTune = trigger.tune;
    voice->synced = trigger.synced;
    voice->low = {};
    voice->mid = {};
    voice->high = {};
    voice->attackSeconds = 0.01f;
    voice->releaseSeconds = 0.05f;
    voice->env = 0.0f;
    voice->releasing = false;
    voice->shortRelease = false;
    voice->sustained = false;
    voice->preview = preview;
    voice->active = true;
    refreshVoice (*voice);
}

SampleEngine::Voice* SampleEngine::allocateVoice()
{
    Voice* oldest = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.active)
            return &voice;

        if (oldest == nullptr || voice.age < oldest->age)
            oldest = &voice;
    }

    if (oldest == nullptr)
        return nullptr;

    if (oldest->audio != nullptr)
        retire (std::move (oldest->audio));

    oldest->active = false;
    return oldest;
}

void SampleEngine::render (juce::AudioBuffer<float>& buffer, int start, int end)
{
    if (end <= start)
        return;

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (auto& voice : voices)
        if (voice.active)
            refreshVoice (voice);

    for (int i = start; i < end; ++i)
    {
        const float master = smoothedGain.getNextValue();

        for (auto& voice : voices)
        {
            if (! voice.active || voice.audio == nullptr)
                continue;

            if (voice.delaySamples > 0)
            {
                --voice.delaySamples;
                continue;
            }

            const int length = voice.audio->getNumSamples();

            if (length < 2)
            {
                voice.active = false;
                retire (std::move (voice.audio));
                continue;
            }

            const int loopStart = juce::jlimit (0, length - 1, voice.loopStart);
            const int loopEnd = juce::jlimit (loopStart + 1, length, voice.loopEnd);
            const bool looping = voice.looping && (loopEnd - loopStart) > 1;
            const double step = std::abs (voice.increment);
            const double samplesLeft = (voice.reversed ? voice.pos
                                                       : (double) (length - 1) - voice.pos)
                                        / juce::jmax (0.001, step);

            if (! looping && ! voice.releasing && samplesLeft <= (double) (1.0f / fadeStep))
            {
                voice.releasing = true;
                voice.shortRelease = true;
            }

            const float releaseStep = (voice.preview || voice.shortRelease)
                                           ? fadeStep
                                           : (voice.releaseSeconds <= 0.0005f
                                                  ? 1.0f
                                                  : 1.0f / (float) juce::jmax (1.0, voice.releaseSeconds * hostSampleRate));
            const float attackStep = voice.preview
                                          ? fadeStep
                                          : (voice.attackSeconds <= 0.0005f
                                                 ? 1.0f
                                                 : 1.0f / (float) juce::jmax (1.0, voice.attackSeconds * hostSampleRate));

            if (voice.releasing)
            {
                voice.env -= releaseStep;

                if (voice.env <= 0.0f)
                {
                    voice.env = 0.0f;
                    voice.active = false;
                    voice.releasing = false;
                    voice.sustained = false;
                    retire (std::move (voice.audio));
                    continue;
                }
            }
            else if (voice.env < 1.0f)
            {
                voice.env = juce::jmin (1.0f, voice.env + attackStep);
            }

            if (! looping && (voice.reversed ? voice.pos <= 0.0 : voice.pos >= (double) (length - 1)))
            {
                voice.active = false;
                voice.releasing = false;
                voice.sustained = false;
                retire (std::move (voice.audio));
                continue;
            }

            const auto* channel0 = voice.audio->getReadPointer (0);
            const auto* channel1 = voice.audio->getNumChannels() > 1
                                        ? voice.audio->getReadPointer (1)
                                        : channel0;

            float sampleL = 0.0f;
            float sampleR = 0.0f;

            if (voice.stretch)
            {
                float aL = 0.0f, aR = 0.0f, bL = 0.0f, bR = 0.0f;
                readPosition (voice, channel0, channel1, length, voice.headA, aL, aR);
                readPosition (voice, channel0, channel1, length, voice.headB, bL, bR);
                const float window = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) voice.grainPhase));
                sampleL = aL * window + bL * (1.0f - window);
                sampleR = aR * window + bR * (1.0f - window);

                const double previousPhase = voice.grainPhase;
                voice.headA += voice.pitchIncrement;
                voice.headB += voice.pitchIncrement;

                if (looping)
                {
                    wrapLoopPosition (voice.headA, (double) loopStart, (double) loopEnd);
                    wrapLoopPosition (voice.headB, (double) loopStart, (double) loopEnd);
                }

                voice.grainPhase += voice.grainStep;

                if (voice.grainPhase >= 1.0)
                {
                    voice.grainPhase -= 1.0;
                    voice.headA = voice.pos + voice.increment;
                }
                else if (previousPhase < 0.5 && voice.grainPhase >= 0.5)
                {
                    voice.headB = voice.pos + voice.increment;
                }
            }
            else
            {
                readPosition (voice, channel0, channel1, length, voice.pos, sampleL, sampleR);
            }

            if (std::fabs (voice.low.db) > 0.05f || std::fabs (voice.mid.db) > 0.05f || std::fabs (voice.high.db) > 0.05f)
            {
                sampleL = runBand (voice.low, sampleL, voice.low.z1L, voice.low.z2L);
                sampleL = runBand (voice.mid, sampleL, voice.mid.z1L, voice.mid.z2L);
                sampleL = runBand (voice.high, sampleL, voice.high.z1L, voice.high.z2L);
                sampleR = runBand (voice.low, sampleR, voice.low.z1R, voice.low.z2R);
                sampleR = runBand (voice.mid, sampleR, voice.mid.z1R, voice.mid.z2R);
                sampleR = runBand (voice.high, sampleR, voice.high.z1R, voice.high.z2R);
            }

            voice.appliedGain += (voice.gain - voice.appliedGain) * gainCoeff;
            const float gain = voice.env * voice.velocity * voice.appliedGain * master;

            left[i] += sampleL * gain * voice.panL;

            if (right != nullptr)
                right[i] += sampleR * gain * voice.panR;

            voice.pos += voice.increment;

            if (looping)
                wrapLoopPosition (voice.pos, (double) loopStart, (double) loopEnd);
        }
    }
}

void SampleEngine::refreshVoice (Voice& voice)
{
    if (voice.slotIndex >= 0 && voice.slotIndex < maxSlots)
    {
        auto& slot = slots[(size_t) voice.slotIndex];

        if (slot.epoch.load() == voice.epoch)
        {
            voice.sampleTune = slot.tune.load() + slot.fine.load() / 100.0f;
            voice.attackSeconds = slot.attackSeconds.load();
            voice.releaseSeconds = slot.releaseSeconds.load();
            voice.gain = slot.gain.load();
            voice.pan = slot.pan.load();
            voice.panL = equalPowerLeft (voice.pan);
            voice.panR = equalPowerRight (voice.pan);
            designBand (voice.low, slot.lowDb.load(), hostSampleRate, 180.0, 0);
            designBand (voice.mid, slot.midDb.load(), hostSampleRate, 1000.0, 1);
            designBand (voice.high, slot.highDb.load(), hostSampleRate, 5000.0, 2);
            voice.synced = slot.synced.load();
            voice.reversed = slot.reversed.load();
            voice.speed = slot.speed.load();

            const int slotLength = juce::jmax (2, slot.length.load());
            const int gap = juce::jmax (2, juce::jmin (64, slotLength / 8));
            const int start = juce::jlimit (0, slotLength - gap, slot.loopStart.load());
            voice.loopStart = start;
            voice.loopEnd = juce::jlimit (start + gap, slotLength, slot.loopEnd.load());
            voice.length = slotLength;
        }
        else if (! voice.releasing)
        {
            voice.releasing = true;
        }
    }

    voice.looping = loop;

    if (voice.looping && voice.loopEnd > voice.loopStart)
    {
        wrapLoopPosition (voice.pos, (double) voice.loopStart, (double) voice.loopEnd);
        wrapLoopPosition (voice.headA, (double) voice.loopStart, (double) voice.loopEnd);
        wrapLoopPosition (voice.headB, (double) voice.loopStart, (double) voice.loopEnd);
    }

    if (sync && voice.synced && hostBpm > 1.0)
    {
        const int span = juce::jmax (1, voice.loopEnd - voice.loopStart);
        const int region = (loop && span > 1) ? span : voice.length;
        const double seconds = (double) juce::jmax (1, region) / juce::jmax (1.0, voice.sourceRate);
        const double naturalBeats = juce::jmax (0.25, seconds * hostBpm / 60.0);
        const double targetBeats = juce::jlimit (0.25, 64.0, std::round (naturalBeats * 4.0) / 4.0);
        const float timeSemitones = juce::jlimit (-48.0f, 48.0f, voice.sampleTune + globalTune);
        const double timeRatio = std::pow (2.0, (double) timeSemitones / 12.0);
        const double beats = voice.speed
                                 ? juce::jlimit (0.0625, 256.0, targetBeats / juce::jmax (1.0e-3, timeRatio))
                                 : targetBeats;

        const double targetSeconds = beats * 60.0 / hostBpm;
        voice.increment = (double) juce::jmax (1, region)
                          / (targetSeconds * juce::jmax (1.0, hostSampleRate));

        if (loop && hostPlaying && hostPpq >= 0.0)
        {
            double phase = std::fmod (hostPpq, beats);

            if (phase < 0.0)
                phase += beats;

            phase /= beats;
            const double spanSamples = (double) juce::jmax (1, voice.loopEnd - voice.loopStart);
            const double desired = voice.reversed
                                        ? (double) voice.loopEnd - phase * spanSamples
                                        : (double) voice.loopStart + phase * spanSamples;

            if (std::abs (desired - voice.pos) > spanSamples * 0.25)
            {
                voice.pos = desired;
                voice.headA = desired;
                voice.headB = desired;
            }
        }

        const float semitones = juce::jlimit (-72.0f, 72.0f,
                                               voice.speed ? (float) (voice.note - rootNote)
                                                           : (float) (voice.note - rootNote) + voice.sampleTune + globalTune);
        const double pitchRatio = std::pow (2.0, (double) semitones / 12.0);
        voice.pitchIncrement = juce::jmax (0.001, voice.increment * pitchRatio);
        voice.grainStep = 1.0 / juce::jmax (64.0, 0.05 * hostSampleRate);
        const bool wantsStretch = std::fabs (semitones) > 0.004f;

        if (wantsStretch && ! voice.stretch)
        {
            voice.grainPhase = 0.0;
            voice.headA = voice.pos;
            voice.headB = voice.pos;
        }

        voice.stretch = wantsStretch;

        if (voice.reversed)
        {
            voice.increment = -std::abs (voice.increment);
            voice.pitchIncrement = -std::abs (voice.pitchIncrement);
        }

        return;
    }

    voice.stretch = false;

    const float semitones = juce::jlimit (-72.0f, 72.0f,
                                           (float) (voice.note - rootNote) + voice.sampleTune + globalTune);
    const double rate = std::pow (2.0, (double) semitones / 12.0);
    voice.increment = juce::jmax (0.001, rate * voice.sourceRate / juce::jmax (1.0, hostSampleRate));

    if (voice.reversed)
        voice.increment = -voice.increment;
}

void SampleEngine::readPosition (const Voice& voice, const float* channel0, const float* channel1, int length,
                                 double position, float& left, float& right) const
{
    const int loopStart = juce::jlimit (0, juce::jmax (0, length - 1), voice.loopStart);
    const int loopEnd = juce::jlimit (loopStart + 1, length, voice.loopEnd);
    const bool looping = voice.looping && (loopEnd - loopStart) > 1;
    double p = position;

    if (looping)
        wrapLoopPosition (p, (double) loopStart, (double) loopEnd);
    else
        p = juce::jlimit (0.0, (double) (length - 1), p);

    int index0 = (int) p;
    const float fraction = (float) (p - (double) index0);
    int index1 = index0 + 1;

    if (looping)
    {
        if (index0 >= loopEnd || index0 < loopStart)
            index0 = loopStart;

        if (index1 >= loopEnd || index1 < loopStart)
            index1 = loopStart;
    }
    else if (index1 >= length)
    {
        index1 = index0;
    }

    index0 = juce::jlimit (0, length - 1, index0);
    index1 = juce::jlimit (0, length - 1, index1);
    left = channel0[index0] + fraction * (channel0[index1] - channel0[index0]);
    right = channel1[index0] + fraction * (channel1[index1] - channel1[index0]);

    if (! looping)
        return;

    const double span = (double) loopEnd - (double) loopStart;
    const double xfade = juce::jmin (span * 0.25, 0.004 * voice.sourceRate);

    if (xfade <= 1.0)
        return;

    const double into = p - (double) loopStart;
    const double remain = (double) loopEnd - p;
    float edge = 1.0f;

    if (into < xfade)
        edge = juce::jmin (edge, (float) (into / xfade));

    if (remain < xfade)
        edge = juce::jmin (edge, (float) (remain / xfade));

    const float shaped = std::sin (edge * juce::MathConstants<float>::halfPi);
    left *= shaped;
    right *= shaped;
}

void SampleEngine::retire (std::shared_ptr<juce::AudioBuffer<float>> audio)
{
    if (audio == nullptr)
        return;

    const juce::SpinLock::ScopedLockType guard (retiredLock);
    retired.push_back (std::move (audio));
}

void SampleEngine::designBand (Voice::Band& band, float db, double sampleRate, double frequency, int kind)
{
    if (std::fabs (db - band.db) < 0.04f)
        return;

    band.db = db;
    const double rate = juce::jmax (1000.0, sampleRate);
    const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (20.0, rate * 0.45, frequency) / rate;
    const double cosw = std::cos (w0);
    const double sinw = std::sin (w0);
    const double A = std::pow (10.0, (double) db / 40.0);
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    if (kind == 1)
    {
        const double alpha = sinw / 1.6;
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha / A;
    }
    else
    {
        const double alpha = sinw * 0.5 * std::sqrt (2.0);
        const double twoSqrt = 2.0 * std::sqrt (A) * alpha;
        const double ap = A + 1.0;
        const double am = A - 1.0;

        if (kind == 0)
        {
            b0 = A * (ap - am * cosw + twoSqrt);
            b1 = 2.0 * A * (am - ap * cosw);
            b2 = A * (ap - am * cosw - twoSqrt);
            a0 = ap + am * cosw + twoSqrt;
            a1 = -2.0 * (am + ap * cosw);
            a2 = ap + am * cosw - twoSqrt;
        }
        else
        {
            b0 = A * (ap + am * cosw + twoSqrt);
            b1 = -2.0 * A * (am + ap * cosw);
            b2 = A * (ap + am * cosw - twoSqrt);
            a0 = ap - am * cosw + twoSqrt;
            a1 = 2.0 * (am - ap * cosw);
            a2 = ap - am * cosw - twoSqrt;
        }
    }

    const double inv = 1.0 / a0;
    band.b0 = (float) (b0 * inv);
    band.b1 = (float) (b1 * inv);
    band.b2 = (float) (b2 * inv);
    band.a1 = (float) (a1 * inv);
    band.a2 = (float) (a2 * inv);
}

float SampleEngine::runBand (Voice::Band& band, float input, float& z1, float& z2)
{
    const float output = band.b0 * input + z1;
    z1 = band.b1 * input - band.a1 * output + z2;
    z2 = band.b2 * input - band.a2 * output;
    return output;
}

int SampleEngine::findBestLoopEnd (const juce::AudioBuffer<float>& buffer, double sourceRate)
{
    const int length = buffer.getNumSamples();

    if (length < 4)
        return length;

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;
    const int search = juce::jlimit (8, length / 2, (int) std::round (0.04 * sourceRate));
    const int start = length - search;
    const float targetL = left[0];
    const float targetR = right[0];
    const float slopeL = left[1] - left[0];
    const float slopeR = right[1] - right[0];
    int best = length - 1;
    float bestScore = std::numeric_limits<float>::max();

    for (int i = length - 1; i >= start; --i)
    {
        const int previous = juce::jmax (0, i - 1);
        const float level = std::fabs (left[i] - targetL) + std::fabs (right[i] - targetR);
        const float slope = std::fabs ((left[i] - left[previous]) - slopeL)
                            + std::fabs ((right[i] - right[previous]) - slopeR);
        const float score = level + slope * 0.5f;

        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }

    return juce::jlimit (1, length, best + 1);
}

void SampleEngine::toggleSlot (int index)
{
    if (index < 0 || index >= maxSlots)
        return;

    EngineCommand command;
    command.type = slotPlaying[(size_t) index].load() ? EngineCommand::Type::stopSlot
                                                       : EngineCommand::Type::previewSlot;
    command.slot = index;
    pushCommands (&command, 1);
}

void SampleEngine::playAll (int quantize)
{
    EngineCommand command;
    command.type = EngineCommand::Type::playAll;
    command.quantize = juce::jlimit (0, 5, quantize);
    pushCommands (&command, 1);
}

void SampleEngine::stopAll()
{
    EngineCommand command;
    command.type = EngineCommand::Type::stopAll;
    pushCommands (&command, 1);
}

bool SampleEngine::isSlotPlaying (int index) const
{
    if (index < 0 || index >= maxSlots)
        return false;

    return slotPlaying[(size_t) index].load();
}

int SampleEngine::getSlotPosition (int index) const
{
    if (index < 0 || index >= maxSlots)
        return -1;

    return slotPosition[(size_t) index].load();
}

int SampleEngine::requestStartCapture (double sampleRate, bool resample, int quantize, juce::String& error)
{
    if (sampleRate < 1.0)
        sampleRate = 48000.0;

    const int generation = nextCaptureGeneration++;
    auto take = std::make_shared<Take>();
    take->sampleRate = sampleRate;
    take->generation = generation;
    take->resample = resample;
    take->audio.setSize (2, (int) std::ceil (sampleRate * 180.0));
    take->audio.clear();

    EngineCommand commands[2];
    commands[0].type = EngineCommand::Type::stopCapture;
    commands[1].type = EngineCommand::Type::startCapture;
    commands[1].resample = resample;
    commands[1].quantize = juce::jlimit (0, 5, quantize);
    commands[1].take = std::move (take);

    if (! pushCommands (commands, 2))
    {
        error = "Recorder is busy.";
        return -1;
    }

    return generation;
}

void SampleEngine::requestStopCapture()
{
    EngineCommand command;
    command.type = EngineCommand::Type::stopCapture;
    pushCommands (&command, 1);
}

std::unique_ptr<SampleEngine::FinishedCapture> SampleEngine::takeFinishedCapture()
{
    std::shared_ptr<Take> take;

    {
        const juce::SpinLock::ScopedLockType guard (finishedLock);

        if (finishedTakes.empty())
            return nullptr;

        take = std::move (finishedTakes.front());
        finishedTakes.erase (finishedTakes.begin());
    }

    if (take == nullptr || take->writePos < 2)
        return nullptr;

    int length = take->writePos;

    if (take->resample)
    {
        const int trim = (int) std::round (0.02 * take->sampleRate);
        length = juce::jmax (1, length - trim);
    }

    auto finished = std::make_unique<FinishedCapture>();
    finished->sampleRate = take->sampleRate > 0.0 ? take->sampleRate : 44100.0;
    finished->resample = take->resample;
    finished->generation = take->generation;
    finished->audio.setSize (2, length);

    for (int channel = 0; channel < 2; ++channel)
        finished->audio.copyFrom (channel, 0, take->audio, channel, 0, length);

    return finished;
}

int SampleEngine::addBuffer (juce::AudioBuffer<float> buffer, double sampleRate,
                             const juce::String& name, const juce::String& path, juce::String& error)
{
    if (buffer.getNumSamples() < 2 || buffer.getNumChannels() < 1)
    {
        error = "Recording was empty.";
        return -1;
    }

    if (sampleRate < 1.0)
        sampleRate = 44100.0;

    if (buffer.getNumChannels() == 1)
    {
        juce::AudioBuffer<float> stereo (2, buffer.getNumSamples());
        stereo.copyFrom (0, 0, buffer, 0, 0, buffer.getNumSamples());
        stereo.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
        buffer = std::move (stereo);
    }
    else if (buffer.getNumChannels() > 2)
    {
        buffer.setSize (2, buffer.getNumSamples(), true, true, true);
    }

    auto shared = std::make_shared<juce::AudioBuffer<float>> (std::move (buffer));
    const int loopEnd = findBestLoopEnd (*shared, sampleRate);
    const int length = shared->getNumSamples();
    int slotIndex = -1;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);
        slotIndex = appendSlot();

        if (slotIndex < 0)
        {
            error = "Sixteen samples are already loaded.";
            return -1;
        }

        auto& slot = slots[(size_t) slotIndex];
        slot.name = name;
        slot.path = path;
        slot.gain.store (1.0f);
        slot.pan.store (0.0f);
        slot.tune.store (0.0f);
        slot.fine.store (0.0f);
        slot.lowDb.store (0.0f);
        slot.midDb.store (0.0f);
        slot.highDb.store (0.0f);
        slot.attackSeconds.store (0.01f);
        slot.releaseSeconds.store (0.05f);
        slot.armed.store (false);
        slot.synced.store (false);
        slot.reversed.store (false);
        slot.speed.store (false);
        slot.playQuantize.store (0);
        slot.loopStart.store (0);
        slot.loopEnd.store (loopEnd);
        slot.length.store (length);
        slot.sourceSampleRate.store (sampleRate);
        slot.audio = std::move (shared);
        slot.epoch.fetch_add (1);
    }

    return slotIndex;
}

bool SampleEngine::pushCommands (EngineCommand* commands, int count)
{
    if (commands == nullptr || count <= 0)
        return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    commandFifo.prepareToWrite (count, start1, size1, start2, size2);

    if (size1 + size2 < count)
    {
        commandFifo.finishedWrite (0);
        return false;
    }

    int written = 0;

    for (int i = 0; i < size1; ++i)
        commandSlots[(size_t) (start1 + i)] = std::move (commands[written++]);

    for (int i = 0; i < size2; ++i)
        commandSlots[(size_t) (start2 + i)] = std::move (commands[written++]);

    commandFifo.finishedWrite (count);
    return true;
}

void SampleEngine::drainCommands()
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    commandFifo.prepareToRead (32, start1, size1, start2, size2);

    for (int i = 0; i < size1; ++i)
    {
        auto command = std::move (commandSlots[(size_t) (start1 + i)]);
        commandSlots[(size_t) (start1 + i)] = {};
        applyCommand (std::move (command));
    }

    for (int i = 0; i < size2; ++i)
    {
        auto command = std::move (commandSlots[(size_t) (start2 + i)]);
        commandSlots[(size_t) (start2 + i)] = {};
        applyCommand (std::move (command));
    }

    commandFifo.finishedRead (size1 + size2);
}

void SampleEngine::applyCommand (EngineCommand command)
{
    switch (command.type)
    {
        case EngineCommand::Type::previewSlot:
            previewSlot (command.slot);
            break;

        case EngineCommand::Type::stopSlot:
            stopSlot (command.slot);
            break;

        case EngineCommand::Type::playAll:
            for (int i = 0; i < maxSlots; ++i)
                previewSlot (i, command.quantize);
            break;

        case EngineCommand::Type::stopAll:
            releaseAll();
            break;

        case EngineCommand::Type::startCapture:
            if (capturingInput || capturingOutput || liveTake != nullptr)
                finalizeCapture();

            pendingTake.reset();
            stopOnGrid = false;
            stopWaiting.store (false);

            if (command.take == nullptr)
                break;

            {
                const double bars = barsToRecord (command.quantize);
                const bool clock = bars > 0.0 && sync && hostPlaying && hostPpq >= 0.0;

                if (clock)
                {
                    pendingTake = std::move (command.take);
                    pendingResample = command.resample;
                    captureQuantizeBeats = bars * 4.0;
                    pendingStartPpq = nextQuantizePoint (hostPpq, 4.0);
                    captureWaiting.store (true);
                    captureActive.store (false);
                }
                else
                {
                    captureOffset = 0;
                    beginTake (std::move (command.take), command.resample, 0.0);
                }
            }
            break;

        case EngineCommand::Type::stopCapture:
            if (pendingTake != nullptr)
            {
                pendingTake.reset();
                captureWaiting.store (false);
                stopOnGrid = false;
                stopWaiting.store (false);
                break;
            }

            stopOnGrid = false;
            stopWaiting.store (false);
            finalizeCapture();
            break;
    }
}

void SampleEngine::previewSlot (int slot, int quantizeOverride)
{
    if (slot < 0 || slot >= maxSlots)
        return;

    Trigger trigger;
    int quantize = 0;

    {
        const juce::SpinLock::ScopedLockType guard (slotLock);
        auto& source = slots[(size_t) slot];

        if (source.audio == nullptr)
            return;

        trigger.audio = source.audio;
        trigger.slot = slot;
        trigger.epoch = source.epoch.load();
        trigger.loopStart = source.loopStart.load();
        trigger.loopEnd = source.loopEnd.load();
        trigger.length = source.length.load();
        trigger.sourceRate = source.sourceSampleRate.load();
        trigger.gain = source.gain.load();
        trigger.pan = source.pan.load();
        trigger.tune = source.tune.load();
        trigger.synced = source.synced.load();
        trigger.reversed = source.reversed.load();
        quantize = quantizeOverride >= 0 ? quantizeOverride : source.playQuantize.load();
    }

    const double beats = sync ? beatsForDivision (quantize) : 0.0;

    for (int i = (int) pendingVoices.size(); --i >= 0;)
        if (pendingVoices[(size_t) i].trigger.slot == slot)
            pendingVoices.erase (pendingVoices.begin() + i);

    if (beats > 0.0 && hostPlaying && hostPpq >= 0.0)
    {
        PendingVoice item;
        item.trigger = std::move (trigger);
        item.startPpq = nextQuantizePoint (hostPpq, beats);
        item.note = rootNote;
        item.velocity = 1.0f;
        item.preview = true;
        pendingVoices.push_back (std::move (item));
        return;
    }

    startVoice (trigger, rootNote, 1.0f, true);
}

void SampleEngine::stopSlot (int slot)
{
    for (int i = (int) pendingVoices.size(); --i >= 0;)
        if (pendingVoices[(size_t) i].trigger.slot == slot)
            pendingVoices.erase (pendingVoices.begin() + i);

    for (auto& voice : voices)
        if (voice.active && voice.slotIndex == slot)
        {
            voice.releasing = true;
            voice.shortRelease = voice.preview;
        }
}

void SampleEngine::launchQuantized (int numSamples)
{
    const double perSample = (hostBpm / 60.0) / juce::jmax (1.0, hostSampleRate);
    const bool clock = sync && hostPlaying && hostPpq >= 0.0 && hostBpm > 1.0;
    const double blockEnd = hostPpq + perSample * (double) numSamples;

    auto due = [&] (double boundary)
    {
        return ! clock || boundary <= blockEnd + 1.0e-4;
    };

    auto offset = [&] (double boundary)
    {
        if (! clock || perSample <= 0.0)
            return 0;

        const double delta = (boundary - hostPpq) / perSample;

        if (delta <= 0.0)
            return 0;

        return juce::jmin (numSamples, (int) std::ceil (delta));
    };

    if (pendingTake != nullptr && due (pendingStartPpq))
    {
        captureOffset = offset (pendingStartPpq);
        auto take = std::move (pendingTake);
        const bool resample = pendingResample;
        const double lengthBeats = captureQuantizeBeats;
        const double startPpq = pendingStartPpq;
        beginTake (std::move (take), resample, lengthBeats);

        if (lengthBeats > 0.0)
        {
            stopOnGrid = true;
            stopAtPpq = startPpq + lengthBeats;
            stopWaiting.store (false);
        }
    }

    for (int i = (int) pendingVoices.size(); --i >= 0;)
    {
        if (! due (pendingVoices[(size_t) i].startPpq))
            continue;

        const int delay = offset (pendingVoices[(size_t) i].startPpq);
        auto item = std::move (pendingVoices[(size_t) i]);
        pendingVoices.erase (pendingVoices.begin() + i);
        startVoice (item.trigger, item.note, item.velocity, item.preview, delay);

        if (item.released && ! item.preview)
        {
            for (auto& voice : voices)
            {
                if (! voice.active || voice.preview || voice.note != item.note
                    || voice.slotIndex != item.trigger.slot)
                    continue;

                if (sustain)
                    voice.sustained = true;
                else
                {
                    voice.releasing = true;
                    voice.shortRelease = false;
                }
            }
        }
    }
}

void SampleEngine::beginTake (std::shared_ptr<Take> take, bool resample, double quantizeBeats)
{
    if (take == nullptr)
        return;

    liveTake = std::move (take);
    liveTake->writePos = 0;
    liveTake->sampleRate = hostSampleRate > 0.0 ? hostSampleRate : liveTake->sampleRate;
    capturingInput = ! resample;
    capturingOutput = resample;
    captureQuantizeBeats = quantizeBeats;
    captureWaiting.store (false);
    captureActive.store (true);
    stopWaiting.store (false);
    stopOnGrid = false;
}

void SampleEngine::appendCapture (const juce::AudioBuffer<float>& buffer, int numChannels, int firstSample, int numSamples)
{
    if (liveTake == nullptr || numSamples <= 0)
        return;

    firstSample = juce::jlimit (0, buffer.getNumSamples(), firstSample);
    numSamples = juce::jmin (numSamples, buffer.getNumSamples() - firstSample);

    if (numSamples <= 0)
        return;

    const int capacity = liveTake->audio.getNumSamples();
    const int position = liveTake->writePos;

    if (position >= capacity)
    {
        finalizeCapture();
        return;
    }

    const int toCopy = juce::jmin (numSamples, capacity - position);

    if (numChannels <= 0)
    {
        liveTake->audio.clear (position, toCopy);
    }
    else
    {
        for (int channel = 0; channel < 2; ++channel)
        {
            const int sourceChannel = juce::jmin (channel, numChannels - 1);
            liveTake->audio.copyFrom (channel, position, buffer, sourceChannel, firstSample, toCopy);
        }
    }

    liveTake->writePos = position + toCopy;

    if (liveTake->writePos >= capacity)
        finalizeCapture();
}

void SampleEngine::finalizeCapture()
{
    capturingInput = false;
    capturingOutput = false;
    captureActive.store (false);
    captureWaiting.store (false);
    stopWaiting.store (false);
    stopOnGrid = false;
    captureQuantizeBeats = 0.0;

    if (liveTake == nullptr)
        return;

    if (liveTake->writePos >= 2)
    {
        const juce::SpinLock::ScopedLockType guard (finishedLock);
        finishedTakes.push_back (std::move (liveTake));
    }
    else
    {
        liveTake.reset();
    }
}

void SampleEngine::applyMasterEq (juce::AudioBuffer<float>& buffer)
{
    const bool active = std::fabs (masterLowDb) > 0.05f
                        || std::fabs (masterMidDb) > 0.05f
                        || std::fabs (masterHighDb) > 0.05f;

    if (! active)
    {
        masterLow = {};
        masterMid = {};
        masterHigh = {};
        return;
    }

    if (buffer.getNumChannels() < 1 || buffer.getNumSamples() < 1)
        return;

    designBand (masterLow, masterLowDb, hostSampleRate, 180.0, 0);
    designBand (masterMid, masterMidDb, hostSampleRate, 1000.0, 1);
    designBand (masterHigh, masterHighDb, hostSampleRate, 5000.0, 2);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float sampleL = left[i];
        sampleL = runBand (masterLow, sampleL, masterLow.z1L, masterLow.z2L);
        sampleL = runBand (masterMid, sampleL, masterMid.z1L, masterMid.z2L);
        sampleL = runBand (masterHigh, sampleL, masterHigh.z1L, masterHigh.z2L);
        left[i] = sampleL;

        if (right == nullptr)
            continue;

        float sampleR = right[i];
        sampleR = runBand (masterLow, sampleR, masterLow.z1R, masterLow.z2R);
        sampleR = runBand (masterMid, sampleR, masterMid.z1R, masterMid.z2R);
        sampleR = runBand (masterHigh, sampleR, masterHigh.z1R, masterHigh.z2R);
        right[i] = sampleR;
    }
}

void SampleEngine::publishSlotActivity()
{
    std::array<bool, maxSlots> active {};
    std::array<int, maxSlots> position;
    std::array<uint32_t, maxSlots> newest {};
    position.fill (-1);

    for (const auto& voice : voices)
    {
        if (! voice.active || voice.slotIndex < 0 || voice.slotIndex >= maxSlots)
            continue;

        const auto slot = (size_t) voice.slotIndex;
        active[slot] = true;

        if (position[slot] < 0 || voice.age >= newest[slot])
        {
            newest[slot] = voice.age;
            position[slot] = (int) voice.pos;
        }
    }

    for (const auto& item : pendingVoices)
    {
        const int slot = item.trigger.slot;

        if (slot >= 0 && slot < maxSlots)
            active[(size_t) slot] = true;
    }

    for (int i = 0; i < maxSlots; ++i)
    {
        slotPlaying[(size_t) i].store (active[(size_t) i]);
        slotPosition[(size_t) i].store (position[(size_t) i]);
    }
}
