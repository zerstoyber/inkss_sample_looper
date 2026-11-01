#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

// Polyphonic sampler. Without sync, pitch is playback speed:
// rate = 2 ^ ((midiNote - root + sampleTune + globalTune) / 12).
// With sync, that same pitch offsets a grain stretch, so the loop length stays locked.
class SampleEngine
{
public:
    static constexpr int maxSlots = 32;
    static constexpr int maxVoices = 32;

    struct SlotInfo
    {
        bool occupied = false;
        bool armed = false;
        bool synced = false;
        bool reversed = false;
        bool speed = false;
        int playQuantize = 0;
        float gain = 1.0f;
        float pan = 0.0f;
        float tune = 0.0f;
        float fine = 0.0f;
        float attackSeconds = 0.01f;
        float releaseSeconds = 0.05f;
        float lowDb = 0.0f;
        float midDb = 0.0f;
        float highDb = 0.0f;
        double durationSeconds = 0.0;
        int length = 0;
        int loopStart = 0;
        int loopEnd = 0;
        juce::String name;
        juce::String path;
    };

    SampleEngine();

    void prepare (double sampleRate, int maxBlock);
    void reset();

    int loadFile (const juce::File& file, juce::String& error);
    void clearSlot (int index);
    void clearAll();
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

    SlotInfo getSlotInfo (int index) const;
    int getOccupiedCount() const;
    std::unique_ptr<juce::XmlElement> createStateXml() const;
    juce::String restoreFromXml (const juce::XmlElement& xml);

    void toggleSlot (int index);
    void playAll (int quantize);
    void stopAll();
    bool isSlotPlaying (int index) const;
    int getSlotPosition (int index) const;

    // Returns the capture generation, or -1 when the recorder queue is full.
    // A capture lasts at most three minutes. resample == false records the
    // plugin input; resample == true records the sampler output.
    int requestStartCapture (double sampleRate, bool resample, int quantize, juce::String& error);
    void requestStopCapture();
    bool isCaptureWaiting() const { return captureWaiting.load(); }
    bool isCapturing() const { return captureActive.load(); }
    bool isStopWaiting() const { return stopWaiting.load(); }

    struct FinishedCapture
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        bool resample = false;
        int generation = 0;
    };

    std::unique_ptr<FinishedCapture> takeFinishedCapture();
    int addBuffer (juce::AudioBuffer<float> buffer, double sampleRate,
                   const juce::String& name, const juce::String& path, juce::String& error);

    void process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                  float globalGain, float globalTuneSemitones, bool loop, bool sync,
                  double hostBpm, double hostPpq, bool hostPlaying, int rootNote,
                  int numInputChannels, float eqLowDb, float eqMidDb, float eqHighDb);

    void collectRetired();

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        juce::String name;
        juce::String path;
        std::atomic<float> gain { 1.0f };
        std::atomic<float> pan { 0.0f };
        std::atomic<float> tune { 0.0f };
        std::atomic<float> fine { 0.0f };
        std::atomic<float> attackSeconds { 0.01f };
        std::atomic<float> releaseSeconds { 0.05f };
        std::atomic<float> lowDb { 0.0f };
        std::atomic<float> midDb { 0.0f };
        std::atomic<float> highDb { 0.0f };
        std::atomic<bool> armed { false };
        std::atomic<bool> synced { false };
        std::atomic<bool> reversed { false };
        std::atomic<bool> speed { false };
        std::atomic<int> playQuantize { 0 };
        std::atomic<int> loopStart { 0 };
        std::atomic<int> loopEnd { 0 };
        std::atomic<int> length { 0 };
        std::atomic<double> sourceSampleRate { 44100.0 };
        std::atomic<int> epoch { 0 };
    };

    struct Voice
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        int note = -1;
        int slotIndex = -1;
        int epoch = 0;
        int loopStart = 0;
        int loopEnd = 0;
        int length = 0;
        uint32_t age = 0;
        double pos = 0.0;
        int delaySamples = 0;
        double increment = 1.0;
        double pitchIncrement = 1.0;
        double grainPhase = 0.0;
        double grainStep = 0.02;
        double headA = 0.0;
        double headB = 0.0;
        double sourceRate = 44100.0;
        float velocity = 0.0f;
        float gain = 1.0f;
        float appliedGain = 0.0f;
        float pan = 0.0f;
        float panL = 0.707f;
        float panR = 0.707f;
        float sampleTune = 0.0f;
        float attackSeconds = 0.01f;
        float releaseSeconds = 0.05f;
        float env = 0.0f;
        struct Band
        {
            float b0 = 1.0f;
            float b1 = 0.0f;
            float b2 = 0.0f;
            float a1 = 0.0f;
            float a2 = 0.0f;
            float z1L = 0.0f;
            float z2L = 0.0f;
            float z1R = 0.0f;
            float z2R = 0.0f;
            float db = 1000.0f;
        };
        Band low;
        Band mid;
        Band high;
        bool active = false;
        bool releasing = false;
        bool shortRelease = false;
        bool sustained = false;
        bool looping = false;
        bool preview = false;
        bool stretch = false;
        bool synced = false;
        bool reversed = false;
        bool speed = false;
    };

    struct Take
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        int writePos = 0;
        int generation = 0;
        bool resample = false;
    };

    struct EngineCommand
    {
        enum class Type { previewSlot, stopSlot, playAll, stopAll, startCapture, stopCapture };

        Type type = Type::stopCapture;
        int slot = -1;
        bool resample = false;
        int quantize = 0;
        std::shared_ptr<Take> take;
    };

    struct Trigger
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        int slot = -1;
        int epoch = 0;
        int loopStart = 0;
        int loopEnd = 0;
        int length = 0;
        double sourceRate = 44100.0;
        float gain = 1.0f;
        float pan = 0.0f;
        float tune = 0.0f;
        bool synced = false;
        bool reversed = false;
    };

    void exchangeSlots (int first, int second);
    void packSlots();
    int appendSlot();
    void handleMidi (const juce::MidiMessage& message, int samplePosition);
    void noteOn (int note, float velocity, int samplePosition);
    void noteOff (int note);
    void releaseAll();
    void killAll();
    void startVoice (const Trigger& trigger, int note, float velocity, bool preview, int delaySamples = 0);
    Voice* allocateVoice();
    void render (juce::AudioBuffer<float>& buffer, int start, int end);
    void refreshVoice (Voice& voice);
    void readPosition (const Voice& voice, const float* channel0, const float* channel1, int length,
                       double position, float& left, float& right) const;
    void retire (std::shared_ptr<juce::AudioBuffer<float>> audio);
    static int findBestLoopEnd (const juce::AudioBuffer<float>& buffer, double sourceRate);
    static void designBand (Voice::Band& band, float db, double sampleRate, double frequency, int kind);
    static float runBand (Voice::Band& band, float input, float& z1, float& z2);

    bool pushCommands (EngineCommand* commands, int count);
    void drainCommands();
    void applyCommand (EngineCommand command);
    void previewSlot (int slot, int quantizeOverride = -1);
    void stopSlot (int slot);
    void launchQuantized (int numSamples);
    void beginTake (std::shared_ptr<Take> take, bool resample, double quantizeBeats);
    void appendCapture (const juce::AudioBuffer<float>& buffer, int numChannels, int firstSample, int numSamples);
    void finalizeCapture();
    void applyMasterEq (juce::AudioBuffer<float>& buffer);
    void publishSlotActivity();

    juce::AudioFormatManager formatManager;
    juce::SpinLock slotLock;
    juce::SpinLock retiredLock;
    juce::SpinLock finishedLock;
    std::array<Slot, maxSlots> slots;
    std::array<Voice, maxVoices> voices;
    std::array<std::atomic<bool>, maxSlots> slotPlaying;
    std::array<std::atomic<int>, maxSlots> slotPosition;
    std::vector<std::shared_ptr<juce::AudioBuffer<float>>> retired;
    std::shared_ptr<Take> liveTake;
    std::vector<std::shared_ptr<Take>> finishedTakes;

    juce::AbstractFifo commandFifo { 32 };
    std::array<EngineCommand, 32> commandSlots;

    juce::SmoothedValue<float> smoothedGain;
    double hostSampleRate = 44100.0;
    float fadeStep = 1.0f / 662.0f;
    float gainCoeff = 0.1f;
    float globalTune = 0.0f;
    int rootNote = 60;
    bool loop = false;
    bool sync = false;
    double hostBpm = 120.0;
    double hostPpq = -1.0;
    bool hostPlaying = false;
    int renderBlockSamples = 0;
    bool sustain = false;
    bool capturingInput = false;
    bool capturingOutput = false;
    bool stopOnGrid = false;
    std::atomic<bool> captureWaiting { false };
    std::atomic<bool> captureActive { false };
    std::atomic<bool> stopWaiting { false };
    int captureOffset = 0;
    double captureQuantizeBeats = 0.0;
    double pendingStartPpq = 0.0;
    double stopAtPpq = 0.0;
    bool pendingResample = false;
    Voice::Band masterLow;
    Voice::Band masterMid;
    Voice::Band masterHigh;
    float masterLowDb = 0.0f;
    float masterMidDb = 0.0f;
    float masterHighDb = 0.0f;
    std::shared_ptr<Take> pendingTake;
    struct PendingVoice
    {
        Trigger trigger;
        double startPpq = 0.0;
        int note = 60;
        float velocity = 1.0f;
        bool preview = true;
        bool released = false;
    };
    std::vector<PendingVoice> pendingVoices;
    uint32_t ageCounter = 1;
    int nextCaptureGeneration = 1;
};
