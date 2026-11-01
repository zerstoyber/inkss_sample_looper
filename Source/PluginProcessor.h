#pragma once

#include <JuceHeader.h>
#include "SampleEngine.h"

class InkssSamplerProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,
                              private juce::Timer
{
public:
    InkssSamplerProcessor();
    ~InkssSamplerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getApvts() { return apvts; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    int loadSamples (const juce::Array<juce::File>& files);
    void clearSlot (int index);
    void clearAllSamples();
    void moveSlot (int index, int direction);
    bool canMoveSlot (int index, int direction) const;
    void setSlotGain (int index, float gain);
    void setSlotPan (int index, float pan);
    void setSlotTune (int index, float semitones);
    void setSlotFine (int index, float cents);
    void setSlotEq (int index, int band, float db);
    void setSlotAttack (int index, float seconds);
    void setSlotRelease (int index, float seconds);
    void setSlotName (int index, const juce::String& name);
    void setSlotArmed (int index, bool armed);
    void setSlotSync (int index, bool synced);
    void setSlotReverse (int index, bool reversed);
    void setSlotSpeed (int index, bool speed);
    void setSlotPlayQuantize (int index, int division);
    void setSlotLoop (int index, int start, int end);
    std::vector<float> copyPeaks (int index, int buckets) const;
    void revealSlot (int index);
    int removeUnreferencedCaptures();
    struct DiskCounts
    {
        int unusedFiles = 0;
        juce::int64 unusedBytes = 0;
        int sessionFiles = 0;
    };
    DiskCounts getDiskCounts() const;
    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }
    bool consumeMidiActivity() { return midiHeard.exchange (false); }
    void toggleSlot (int index);
    void playAllSamples();
    void stopAllSamples();
    bool isAnyPlaying() const;
    bool isSlotPlaying (int index) const;
    int getSlotPosition (int index) const;
    void startRecording();
    void startResampling();
    void stopCapture();
    int getRecordQuantize() const { return recordQuantize.load(); }
    int getResampleQuantize() const { return resampleQuantize.load(); }
    int getPlayQuantize() const { return playQuantize.load(); }
    void setRecordQuantize (int division);
    void setResampleQuantize (int division);
    void setPlayQuantize (int division);
    void openStandaloneInput();
    bool isRecording() const { return recording.load(); }
    bool isResampling() const { return resampling.load(); }
    bool isSyncEnabled() const;
    void setAppBpm (double bpm);
    double getAppBpm() const { return appBpm.load(); }
    SampleEngine::SlotInfo getSlotInfo (int index) const;
    juce::String getStatus() const;

private:
    void timerCallback() override;
    void applyMidiController (const juce::MidiMessage& message);
    void setStatus (juce::String text);
    void commitFinishedCapture();
    static juce::File captureDirectory();
    static bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate);
    bool discardCaptureFile (const juce::String& path);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    SampleEngine engine;
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;
    mutable juce::SpinLock statusLock;
    juce::String status { "DROP AUDIO" };
    std::atomic<bool> recording { false };
    std::atomic<bool> resampling { false };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> outputLevel { 0.0f };
    std::atomic<bool> midiHeard { false };
    std::atomic<double> appBpm { 120.0 };
    std::atomic<int> recordQuantize { 0 };
    std::atomic<int> resampleQuantize { 0 };
    std::atomic<int> playQuantize { 0 };
    bool playParameterDown = false;
    double standalonePpq = 0.0;
    int activeCaptureGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InkssSamplerProcessor)
};
