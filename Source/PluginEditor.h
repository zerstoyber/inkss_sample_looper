#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

class InkssSamplerEditor : public juce::AudioProcessorEditor,
                           private juce::ChangeListener,
                           private juce::FileDragAndDropTarget,
                           private juce::Timer
{
public:
    explicit InkssSamplerEditor (InkssSamplerProcessor&);
    ~InkssSamplerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void parentHierarchyChanged() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct HeldNote
    {
        juce::juce_wchar character = 0;
        int note = -1;
    };

    class SampleRow;
    class SourceLights;
    class TempoBox;
    class QuantizePicker;

    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void rebuildRows();
    void layoutRows();
    void browseForSamples();
    void refreshDiskLabels();
    void releaseHeldNotes();
    void showOptionsMenu();
    static int noteForCharacter (juce::juce_wchar character);

    InkssSamplerProcessor& sampler;
    std::unique_ptr<juce::LookAndFeel> lookAndFeel;
    juce::Label status;
    juce::Label empty;
    juce::Label gainLabel;
    juce::Label coarseLabel;
    juce::Label fineLabel;
    juce::Label lowLabel;
    juce::Label midLabel;
    juce::Label highLabel;
    juce::Slider gainSlider;
    juce::Slider tuneSlider;
    juce::Slider fineSlider;
    juce::Slider lowSlider;
    juce::Slider midSlider;
    juce::Slider highSlider;
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton syncButton { "SYNC" };
    juce::TextButton playAllButton { "PLAY" };
    juce::TextButton optionsButton { "OPTIONS" };
    juce::TextButton loadButton { "IMPORT" };
    juce::TextButton recordButton { "RECORD" };
    juce::TextButton resampleButton { "RESAMPLE" };
    juce::TextButton clearButton { "CLEAR SESSION\n0" };
    juce::TextButton clearFilesButton { "DELETE FILES\n0  0 MB" };
    juce::Viewport viewport;
    struct ListBackground : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xffe8e8e8));
        }
    };
    ListBackground list;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<SourceLights> sourceLights;
    std::unique_ptr<TempoBox> tempoBox;
    std::unique_ptr<QuantizePicker> playQuantize;
    std::unique_ptr<QuantizePicker> recordQuantize;
    std::unique_ptr<QuantizePicker> resampleQuantize;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;
    std::unique_ptr<SliderAttachment> tuneAttachment;
    std::unique_ptr<SliderAttachment> fineAttachment;
    std::unique_ptr<SliderAttachment> lowAttachment;
    std::unique_ptr<SliderAttachment> midAttachment;
    std::unique_ptr<SliderAttachment> highAttachment;
    std::unique_ptr<ButtonAttachment> loopAttachment;
    std::unique_ptr<ButtonAttachment> syncAttachment;

    std::vector<std::unique_ptr<SampleRow>> rows;
    std::vector<HeldNote> heldNotes;
    float shownInput = 0.0f;
    float shownOutput = 0.0f;
    int midiHold = 0;
    int diskTick = 0;
    bool dragging = false;
    bool windowChromeReady = false;
    juce::Image wordmark;
    juce::Rectangle<int> wordmarkBounds;
    juce::Rectangle<int> headerCard;
    juce::Rectangle<int> headerKnobBounds;
    int headerKnobSections = 0;
    juce::Rectangle<int> headerEqBounds;
    int headerEqSections = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InkssSamplerEditor)
};
