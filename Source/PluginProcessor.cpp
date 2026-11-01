#include "PluginProcessor.h"
#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

InkssSamplerProcessor::InkssSamplerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
    startTimerHz (4);
}

InkssSamplerProcessor::~InkssSamplerProcessor()
{
    stopTimer();
}

void InkssSamplerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
}

void InkssSamplerProcessor::releaseResources()
{
    engine.reset();
}

bool InkssSamplerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    const auto& input = layouts.getMainInputChannelSet();
    const bool outputOk = output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
    const bool inputOk = input.isDisabled()
                         || input == juce::AudioChannelSet::mono()
                         || input == juce::AudioChannelSet::stereo();

    if (! outputOk || ! inputOk)
        return false;

    return ! (input == juce::AudioChannelSet::stereo() && output == juce::AudioChannelSet::mono());
}

void InkssSamplerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    inputLevel.store (buffer.getMagnitude (0, buffer.getNumSamples()));
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isController())
            applyMidiController (message);

        if (message.isNoteOn() && message.getVelocity() > 0)
            midiHeard.store (true);
    }

    const bool wantPlay = apvts.getRawParameterValue ("play")->load() >= 0.5f;

    if (wantPlay != playParameterDown)
    {
        playParameterDown = wantPlay;

        if (wantPlay)
            playAllSamples();
        else
            stopAllSamples();
    }

    const float gain = apvts.getRawParameterValue ("gain")->load() / 100.0f;
    const float coarse = apvts.getRawParameterValue ("tune")->load();
    const float fine = apvts.getRawParameterValue ("tuneFine")->load() / 100.0f;
    const bool loop = apvts.getRawParameterValue ("loop")->load() >= 0.5f;
    const bool sync = apvts.getRawParameterValue ("sync")->load() >= 0.5f;
    const float eqLow = apvts.getRawParameterValue ("eqLow")->load();
    const float eqMid = apvts.getRawParameterValue ("eqMid")->load();
    const float eqHigh = apvts.getRawParameterValue ("eqHigh")->load();
    const int root = juce::roundToInt (apvts.getRawParameterValue ("root")->load());
    double bpm = appBpm.load();
    double ppq = -1.0;
    bool playing = false;
    bool hostTempo = false;

    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
        {
            if (auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                {
                    bpm = *hostBpm;
                    hostTempo = true;
                }

            if (auto hostPpq = position->getPpqPosition())
                ppq = *hostPpq;

            playing = position->getIsPlaying();
        }

    if (! hostTempo)
    {
        ppq = standalonePpq;
        playing = true;
        const double rate = getSampleRate();

        if (rate > 1.0)
            standalonePpq += bpm / 60.0 * (double) buffer.getNumSamples() / rate;
    }

    engine.process (buffer, midi, gain, coarse + fine, loop, sync, bpm, ppq, playing, root,
                    getMainBusNumInputChannels(), eqLow, eqMid, eqHigh);
    outputLevel.store (buffer.getMagnitude (0, buffer.getNumSamples()));
}

juce::AudioProcessorEditor* InkssSamplerProcessor::createEditor()
{
    return new InkssSamplerEditor (*this);
}

bool InkssSamplerProcessor::hasEditor() const
{
    return true;
}

const juce::String InkssSamplerProcessor::getName() const
{
    return JucePlugin_Name;
}

bool InkssSamplerProcessor::acceptsMidi() const
{
    return true;
}

bool InkssSamplerProcessor::producesMidi() const
{
    return false;
}

bool InkssSamplerProcessor::isMidiEffect() const
{
    return false;
}

double InkssSamplerProcessor::getTailLengthSeconds() const
{
    return 0.05;
}

int InkssSamplerProcessor::getNumPrograms()
{
    return 1;
}

int InkssSamplerProcessor::getCurrentProgram()
{
    return 0;
}

void InkssSamplerProcessor::setCurrentProgram (int)
{
}

const juce::String InkssSamplerProcessor::getProgramName (int)
{
    return {};
}

void InkssSamplerProcessor::changeProgramName (int, const juce::String&)
{
}

void InkssSamplerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml == nullptr)
        return;

    xml->setAttribute ("appBpm", getAppBpm());
    xml->setAttribute ("recordQuantize", getRecordQuantize());
    xml->setAttribute ("resampleQuantize", getResampleQuantize());
    xml->setAttribute ("playQuantize", getPlayQuantize());

    if (wrapperType != wrapperType_Standalone)
        if (auto samples = engine.createStateXml())
            xml->addChildElement (samples.release());

    copyXmlToBinary (*xml, destData);
}

void InkssSamplerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr)
        return;

    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (xml->hasAttribute ("appBpm"))
        setAppBpm (xml->getDoubleAttribute ("appBpm", 120.0));

    if (xml->hasAttribute ("recordQuantize"))
        setRecordQuantize (xml->getIntAttribute ("recordQuantize", 0));

    if (xml->hasAttribute ("resampleQuantize"))
        setResampleQuantize (xml->getIntAttribute ("resampleQuantize", 0));

    if (xml->hasAttribute ("playQuantize"))
        setPlayQuantize (xml->getIntAttribute ("playQuantize", 0));

    if (wrapperType == wrapperType_Standalone)
    {
        setStatus ("DROP AUDIO");
        sendChangeMessage();
        return;
    }

    juce::String missing;

    if (auto* samples = xml->getChildByName ("SAMPLES"))
        missing = engine.restoreFromXml (*samples);

    setStatus (missing.isNotEmpty() ? "MISSING " + missing : "RESTORED");
    sendChangeMessage();
}

int InkssSamplerProcessor::loadSamples (const juce::Array<juce::File>& files)
{
    int loaded = 0;
    juce::String error;

    for (const auto& file : files)
    {
        juce::String oneError;

        if (engine.loadFile (file, oneError) >= 0)
            ++loaded;
        else
            error = oneError;
    }

    if (loaded > 0 && error.isEmpty())
        setStatus ("LOADED " + juce::String (loaded));
    else if (loaded > 0)
        setStatus ("LOADED " + juce::String (loaded) + "  " + error.toUpperCase());
    else
        setStatus (error.isNotEmpty() ? error.toUpperCase() : "NOTHING LOADED");

    sendChangeMessage();
    return loaded;
}

void InkssSamplerProcessor::moveSlot (int index, int direction)
{
    engine.moveSlot (index, direction);
    sendChangeMessage();
}

bool InkssSamplerProcessor::canMoveSlot (int index, int direction) const
{
    return engine.canMoveSlot (index, direction);
}

void InkssSamplerProcessor::clearSlot (int index)
{
    const auto path = engine.getSlotInfo (index).path;
    engine.clearSlot (index);
    const bool removedFile = discardCaptureFile (path);
    setStatus (removedFile ? "DELETED" : "REMOVED");
    sendChangeMessage();
}

void InkssSamplerProcessor::clearAllSamples()
{
    juce::StringArray paths;

    for (int i = 0; i < SampleEngine::maxSlots; ++i)
    {
        const auto path = engine.getSlotInfo (i).path;

        if (path.isNotEmpty())
            paths.add (path);
    }

    engine.clearAll();

    for (const auto& path : paths)
        discardCaptureFile (path);

    setStatus ("CLEARED");
    sendChangeMessage();
}

void InkssSamplerProcessor::setSlotGain (int index, float gain)
{
    engine.setSlotGain (index, gain);
}

void InkssSamplerProcessor::setSlotPan (int index, float pan)
{
    engine.setSlotPan (index, pan);
}

void InkssSamplerProcessor::setSlotTune (int index, float semitones)
{
    engine.setSlotTune (index, semitones);
}

void InkssSamplerProcessor::setSlotFine (int index, float cents)
{
    engine.setSlotFine (index, cents);
}

void InkssSamplerProcessor::setSlotEq (int index, int band, float db)
{
    engine.setSlotEq (index, band, db);
}

void InkssSamplerProcessor::setSlotAttack (int index, float seconds)
{
    engine.setSlotAttack (index, seconds);
}

void InkssSamplerProcessor::setSlotRelease (int index, float seconds)
{
    engine.setSlotRelease (index, seconds);
}

void InkssSamplerProcessor::setSlotName (int index, const juce::String& name)
{
    engine.setSlotName (index, name);
}

void InkssSamplerProcessor::setSlotArmed (int index, bool armed)
{
    engine.setSlotArmed (index, armed);
}

void InkssSamplerProcessor::setSlotSync (int index, bool synced)
{
    engine.setSlotSync (index, synced);
}

void InkssSamplerProcessor::setSlotPlayQuantize (int index, int division)
{
    engine.setSlotPlayQuantize (index, division);
}

void InkssSamplerProcessor::setSlotReverse (int index, bool reversed)
{
    engine.setSlotReverse (index, reversed);
}

void InkssSamplerProcessor::setSlotSpeed (int index, bool speed)
{
    engine.setSlotSpeed (index, speed);
}

void InkssSamplerProcessor::setSlotLoop (int index, int start, int end)
{
    engine.setSlotLoop (index, start, end);
}

std::vector<float> InkssSamplerProcessor::copyPeaks (int index, int buckets) const
{
    return engine.copyPeaks (index, buckets);
}

void InkssSamplerProcessor::revealSlot (int index)
{
    const auto file = juce::File (engine.getSlotInfo (index).path);

    if (file.existsAsFile())
        file.revealToUser();
    else
        setStatus ("NO FILE");
}

int InkssSamplerProcessor::removeUnreferencedCaptures()
{
    juce::StringArray used;

    for (int i = 0; i < SampleEngine::maxSlots; ++i)
    {
        const auto path = engine.getSlotInfo (i).path;

        if (path.isNotEmpty())
            used.add (path);
    }

    int removed = 0;

    for (const auto& file : captureDirectory().findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        if (used.contains (file.getFullPathName()))
            continue;

        if (file.deleteFile())
            ++removed;
    }

    setStatus (removed == 0 ? "NOTHING TO DELETE"
                            : "DELETED " + juce::String (removed));
    return removed;
}

InkssSamplerProcessor::DiskCounts InkssSamplerProcessor::getDiskCounts() const
{
    const auto directory = captureDirectory();
    juce::StringArray used;
    DiskCounts counts;

    for (int i = 0; i < SampleEngine::maxSlots; ++i)
    {
        const auto file = juce::File (engine.getSlotInfo (i).path);

        if (! file.existsAsFile() || ! file.isAChildOf (directory))
            continue;

        const auto path = file.getFullPathName();

        if (! used.contains (path))
        {
            used.add (path);
            ++counts.sessionFiles;
        }
    }

    for (const auto& file : directory.findChildFiles (juce::File::findFiles, false, "*.wav"))
    {
        if (used.contains (file.getFullPathName()))
            continue;

        ++counts.unusedFiles;
        counts.unusedBytes += file.getSize();
    }

    return counts;
}

void InkssSamplerProcessor::toggleSlot (int index)
{
    engine.toggleSlot (index);
}

void InkssSamplerProcessor::playAllSamples()
{
    const int division = isSyncEnabled() ? playQuantize.load() : 0;
    engine.playAll (division);
}

void InkssSamplerProcessor::stopAllSamples()
{
    engine.stopAll();
}

bool InkssSamplerProcessor::isAnyPlaying() const
{
    for (int i = 0; i < SampleEngine::maxSlots; ++i)
        if (engine.isSlotPlaying (i))
            return true;

    return false;
}

bool InkssSamplerProcessor::isSlotPlaying (int index) const
{
    return engine.isSlotPlaying (index);
}

int InkssSamplerProcessor::getSlotPosition (int index) const
{
    return engine.getSlotPosition (index);
}

bool InkssSamplerProcessor::isSyncEnabled() const
{
    return apvts.getRawParameterValue ("sync")->load() >= 0.5f;
}

void InkssSamplerProcessor::setAppBpm (double bpm)
{
    appBpm.store (juce::jlimit (40.0, 240.0, bpm));
}

void InkssSamplerProcessor::startRecording()
{
    if (recording.load())
    {
        stopCapture();
        return;
    }

    openStandaloneInput();

    juce::String error;
    const int division = isSyncEnabled() ? recordQuantize.load() : 0;
    const int generation = engine.requestStartCapture (getSampleRate(), false, division, error);

    if (generation < 0)
    {
        setStatus (error);
        return;
    }

    activeCaptureGeneration = generation;
    recording.store (true);
    resampling.store (false);
    setStatus ("RECORD");
}

void InkssSamplerProcessor::startResampling()
{
    if (resampling.load())
    {
        stopCapture();
        return;
    }

    juce::String error;
    const int division = isSyncEnabled() ? resampleQuantize.load() : 0;
    const int generation = engine.requestStartCapture (getSampleRate(), true, division, error);

    if (generation < 0)
    {
        setStatus (error);
        return;
    }

    activeCaptureGeneration = generation;
    recording.store (false);
    resampling.store (true);
    setStatus ("RESAMPLE");
}

void InkssSamplerProcessor::openStandaloneInput()
{
   #if JucePlugin_Build_Standalone
    auto* holder = juce::StandalonePluginHolder::getInstance();

    if (holder == nullptr)
        return;

    holder->getMuteInputValue().setValue (false);

    auto& manager = holder->deviceManager;
    auto* device = manager.getCurrentAudioDevice();

    if (device == nullptr || device->getActiveInputChannels().countNumberOfSetBits() > 0)
        return;

    const int available = device->getInputChannelNames().size();

    if (available <= 0)
        return;

    auto setup = manager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();

    const int count = juce::jmin (2, available);

    for (int channel = 0; channel < count; ++channel)
        setup.inputChannels.setBit (channel);

    const auto error = manager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        setStatus (error);
   #endif
}

void InkssSamplerProcessor::stopCapture()
{
    const bool waiting = engine.isCaptureWaiting() && ! engine.isCapturing();
    engine.requestStopCapture();
    recording.store (false);
    resampling.store (false);

    if (waiting)
        setStatus ("STOPPED");
}

void InkssSamplerProcessor::setRecordQuantize (int division)
{
    recordQuantize.store (juce::jlimit (0, 5, division));
}

void InkssSamplerProcessor::setResampleQuantize (int division)
{
    resampleQuantize.store (juce::jlimit (0, 5, division));
}

void InkssSamplerProcessor::setPlayQuantize (int division)
{
    playQuantize.store (juce::jlimit (0, 5, division));
}

void InkssSamplerProcessor::applyMidiController (const juce::MidiMessage& message)
{
    const int controller = message.getControllerNumber();
    const float unit = (float) message.getControllerValue() / 127.0f;

    auto assign = [this] (const char* parameterId, float plain)
    {
        if (auto* parameter = apvts.getParameter (parameterId))
        {
            const float normalised = parameter->convertTo0to1 (plain);

            if (std::abs (parameter->getValue() - normalised) > 0.0005f)
                parameter->setValueNotifyingHost (normalised);
        }
    };

    switch (controller)
    {
        case 7:  assign ("gain", unit * 100.0f); break;
        case 16: assign ("tune", (float) juce::roundToInt (unit * 48.0f - 24.0f)); break;
        case 17: assign ("tuneFine", (float) juce::roundToInt (unit * 200.0f - 100.0f)); break;
        case 18: assign ("loop", message.getControllerValue() >= 64 ? 1.0f : 0.0f); break;
        case 19: assign ("sync", message.getControllerValue() >= 64 ? 1.0f : 0.0f); break;
        case 20: assign ("play", message.getControllerValue() >= 64 ? 1.0f : 0.0f); break;
        case 21: assign ("eqLow", (float) juce::roundToInt (unit * 48.0f - 24.0f)); break;
        case 22: assign ("eqMid", (float) juce::roundToInt (unit * 48.0f - 24.0f)); break;
        case 23: assign ("eqHigh", (float) juce::roundToInt (unit * 48.0f - 24.0f)); break;
        default: break;
    }
}

SampleEngine::SlotInfo InkssSamplerProcessor::getSlotInfo (int index) const
{
    return engine.getSlotInfo (index);
}

juce::String InkssSamplerProcessor::getStatus() const
{
    const juce::SpinLock::ScopedLockType guard (statusLock);
    return status;
}

void InkssSamplerProcessor::timerCallback()
{
    engine.collectRetired();
    commitFinishedCapture();

    if (! recording.load() && ! resampling.load())
        return;

    juce::String text;

    if (engine.isCaptureWaiting() || engine.isStopWaiting())
        text = "WAIT";
    else if (engine.isCapturing())
        text = recording.load() ? "RECORD" : "RESAMPLE";

    if (text.isNotEmpty() && getStatus() != text)
        setStatus (text);
}

void InkssSamplerProcessor::commitFinishedCapture()
{
    auto capture = engine.takeFinishedCapture();

    if (capture == nullptr)
        return;

    const bool stillThisTake = capture->generation == activeCaptureGeneration;
    const float peak = capture->audio.getMagnitude (0, capture->audio.getNumSamples());

    if (peak < 0.0008f)
    {
        if (stillThisTake)
        {
            recording.store (false);
            resampling.store (false);
        }

        setStatus (capture->resample ? "RESAMPLE SILENT" : "INPUT SILENT");
        sendChangeMessage();
        return;
    }
    const int number = engine.getOccupiedCount() + 1;
    const juce::String name = capture->resample
                                  ? "Resample " + juce::Time::getCurrentTime().formatted ("%H.%M.%S")
                                  : "Sample " + juce::String (number);

    auto file = captureDirectory().getChildFile (juce::File::createLegalFileName (name)
                                                 + "_" + juce::Uuid().toString() + ".wav");
    juce::String error;
    juce::String path;

    if (writeWav (file, capture->audio, capture->sampleRate))
        path = file.getFullPathName();

    const int slot = engine.addBuffer (std::move (capture->audio), capture->sampleRate, name, path, error);

    if (stillThisTake)
    {
        recording.store (false);
        resampling.store (false);
    }

    if (slot < 0)
    {
        discardCaptureFile (path);
        setStatus (error);
    }
    else if (path.isEmpty())
    {
        setStatus ("NOT SAVED");
    }
    else
    {
        setStatus (capture->resample ? "RESAMPLED" : "RECORDED");
    }

    sendChangeMessage();
}

juce::File InkssSamplerProcessor::captureDirectory()
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("Inkasso")
                         .getChildFile ("INKSS Sampler")
                         .getChildFile ("Samples");
    directory.createDirectory();
    return directory;
}

bool InkssSamplerProcessor::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate)
{
    auto stream = std::unique_ptr<juce::OutputStream> (new juce::FileOutputStream (file));

    if (auto* fileStream = dynamic_cast<juce::FileOutputStream*> (stream.get()))
        if (fileStream->failedToOpen())
            return false;

    juce::WavAudioFormat format;
    auto writer = format.createWriterFor (stream,
                                          juce::AudioFormatWriterOptions{}
                                              .withSampleRate (sampleRate)
                                              .withNumChannels (audio.getNumChannels())
                                              .withBitsPerSample (16));

    if (writer == nullptr)
    {
        file.deleteFile();
        return false;
    }

    const bool wrote = writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());

    if (! wrote)
        file.deleteFile();

    return wrote;
}

bool InkssSamplerProcessor::discardCaptureFile (const juce::String& path)
{
    if (path.isEmpty())
        return false;

    const juce::File file (path);

    if (! file.isAChildOf (captureDirectory()))
        return false;

    return file.deleteFile();
}

void InkssSamplerProcessor::setStatus (juce::String text)
{
    const juce::SpinLock::ScopedLockType guard (statusLock);
    status = std::move (text);
}

juce::AudioProcessorValueTreeState::ParameterLayout InkssSamplerProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "gain", 1 },
        "VOL",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        70.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tune", 1 },
        "PITCH",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tuneFine", 1 },
        "FINE",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f),
        0.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "loop", 1 },
        "LOOP",
        false));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "sync", 1 },
        "SYNC",
        false));

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "root", 1 },
        "Root",
        0,
        127,
        60));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "play", 1 },
        "PLAY",
        false));

    auto addEq = [&layout] (const char* parameterId, const char* name)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { parameterId, 1 },
            name,
            juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
            0.0f));
    };

    addEq ("eqLow", "LOW");
    addEq ("eqMid", "MID");
    addEq ("eqHigh", "HIGH");

    return layout;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new InkssSamplerProcessor();
}
