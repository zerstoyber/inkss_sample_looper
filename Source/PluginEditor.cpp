#include "PluginEditor.h"
#include "BinaryData.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace
{
class InkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    InkLookAndFeel()
    {
        setColour (juce::Slider::thumbColourId, juce::Colour (0xff5a5a5a));
        setColour (juce::Slider::trackColourId, juce::Colour (0xff8a8a8a));
        setColour (juce::Slider::backgroundColourId, juce::Colour (0xffc4c4c4));
        setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, juce::Colours::black);
        setColour (juce::TextButton::buttonColourId, juce::Colour (0xffd4d4d4));
        setColour (juce::TextButton::textColourOffId, juce::Colours::black);
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ScrollBar::thumbColourId, juce::Colours::black);
        setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colours::black);
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff2a2a2a));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool, bool) override
    {
        const bool on = button.isEnabled() && button.getToggleState();
        auto full = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);
        const int inset = (int) button.getProperties().getWithDefault ("topInset", 0);
        g.setColour (on ? juce::Colour (0xff0000ff)
                        : (button.isEnabled() ? juce::Colour (0xffd4d4d4) : juce::Colour (0xffececec)));
        g.fillRoundedRectangle (full, 5.0f);

        const float level = (float) button.getProperties().getWithDefault ("level", 0.0f);

        if (level > 0.001f)
        {
            auto bar = full;

            if (inset > 0)
                bar.removeFromTop ((float) inset);

            bar = bar.reduced (8.0f, 7.0f).removeFromBottom (2.0f);
            bar.setWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level));
            g.setColour (on ? juce::Colours::white : juce::Colours::black);
            g.fillRect (bar);
        }
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
    {
        const float radius = (float) juce::jmin (width, height) * 0.5f - 2.0f;
        const float centreX = (float) x + (float) width * 0.5f;
        const float centreY = (float) y + (float) height * 0.5f;
        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        auto tip = juce::Point<float> (0.0f, -radius).rotatedAboutOrigin (angle);

        g.setColour (slider.isEnabled() ? juce::Colours::black : juce::Colour (0xffb5b5b5));
        g.drawEllipse (centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.4f);
        g.drawLine (centreX, centreY, centreX + tip.x, centreY + tip.y, 1.5f);
    }

    juce::Font getLabelFont (juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
            return juce::Font (juce::FontOptions (9.0f));

        return juce::LookAndFeel_V4::getLabelFont (label);
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                        bool vertical, int thumbStart, int thumbSize, bool, bool) override
    {
        g.setColour (juce::Colours::black);

        if (vertical)
            g.fillRoundedRectangle ((float) x, (float) thumbStart, (float) width, (float) thumbSize, 2.0f);
        else
            g.fillRoundedRectangle ((float) thumbStart, (float) y, (float) thumbSize, (float) height, 2.0f);
    }

    int getDefaultScrollbarWidth() override
    {
        return 8;
    }

    void drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int, int, const juce::PopupMenu::Options&) override
    {
        g.fillAll (juce::Colours::black);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool) override
    {
        if (! button.isEnabled())
            g.setColour (juce::Colour (0xffb0b0b0));
        else
            g.setColour (button.getToggleState() ? juce::Colours::white : juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (button.getButtonText().containsChar ('\n') ? 9.0f : 10.0f)));
        auto area = button.getLocalBounds().reduced (3, 1);
        const int inset = (int) button.getProperties().getWithDefault ("topInset", 0);
        const int rightInset = (int) button.getProperties().getWithDefault ("rightInset", 0);

        if (inset > 0)
            area.removeFromTop (inset);

        if (rightInset > 0)
            area.removeFromRight (rightInset);

        const int lines = button.getButtonText().containsChar ('\n') ? 2 : 1;
        g.drawFittedText (button.getButtonText(), area, juce::Justification::centred, lines);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return juce::Font (juce::FontOptions (10.0f));
    }
};

const juce::Colour pageColour { 0xffe8e8e8 };
const juce::Colour plateColour { 0xffd4d4d4 };
const juce::Colour plateMutedColour { 0xffececec };
const juce::Colour mutedInk { 0xffb0b0b0 };

struct KnobPlate
{
    juce::Rectangle<int> bounds;
    int sections = 0;
    bool envelope = false;
};

constexpr int knobColumnWidth = 56;
constexpr int knobPlateHeight = 58;

void configureKnob (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobColumnWidth - 2, 11);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::black);
    slider.setMouseDragSensitivity (120);
    slider.setOpaque (false);
}

KnobPlate layoutKnobGroup (int x, int y, int columnWidth,
                           const std::vector<std::pair<juce::Slider*, juce::Label*>>& knobs)
{
    const int count = juce::jmax (1, (int) knobs.size());
    const int column = juce::jmax (40, columnWidth);
    KnobPlate plate;
    plate.sections = count;
    plate.bounds = { x, y, count * column + 6, knobPlateHeight };
    auto inner = plate.bounds.reduced (3, 2);

    for (int i = 0; i < count; ++i)
    {
        auto cell = inner.removeFromLeft (column);
        knobs[(size_t) i].second->setBounds (cell.removeFromBottom (12));
        knobs[(size_t) i].first->setBounds (cell.reduced (1, 0));
    }

    return plate;
}

void paintKnobPlate (juce::Graphics& g, juce::Rectangle<int> bounds, int sections, bool dimmed = false)
{
    if (bounds.isEmpty())
        return;

    g.setColour (dimmed ? plateMutedColour : plateColour);
    g.fillRoundedRectangle (bounds.toFloat(), 8.0f);

    if (sections < 2)
        return;

    g.setColour (dimmed ? mutedInk.withAlpha (0.45f) : juce::Colours::black.withAlpha (0.28f));
    const float top = (float) bounds.getY() + 8.0f;
    const float bottom = (float) bounds.getBottom() - 16.0f;

    for (int i = 1; i < sections; ++i)
    {
        const float x = (float) bounds.getX() + (float) bounds.getWidth() * (float) i / (float) sections;
        g.drawLine (x, top, x, bottom, 1.0f);
    }
}

void setTransportButton (juce::Button& button, bool on, const juce::String& text)
{
    if (button.getToggleState() != on)
        button.setToggleState (on, juce::dontSendNotification);

    if (button.getButtonText() != text)
        button.setButtonText (text);
}
}

class InkssSamplerEditor::SourceLights : public juce::Component
{
public:
    void setSources (bool keyboardDownIn, bool midiDownIn)
    {
        if (keyboardDownIn == keyboardDown && midiDownIn == midiDown)
            return;

        keyboardDown = keyboardDownIn;
        midiDown = midiDownIn;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        auto left = area.removeFromLeft ((area.getWidth() - 6.0f) * 0.5f);
        area.removeFromLeft (6.0f);
        drawChip (g, left, "KEY IN", keyboardDown);
        drawChip (g, area, "MIDI IN", midiDown);
    }

private:
    static void drawChip (juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& text, bool on)
    {
        g.setColour (on ? juce::Colour (0xff0000ff) : plateColour);
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (on ? juce::Colours::white : juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText (text, bounds.toNearestInt(), juce::Justification::centred, 1);
    }

    bool keyboardDown = false;
    bool midiDown = false;
};

class InkssSamplerEditor::TempoBox : public juce::Component
{
public:
    std::function<void (int)> onChange;

    void setBpm (int value)
    {
        value = juce::jlimit (40, 240, value);

        if (value == bpm)
            return;

        bpm = value;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (plateColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (juce::Colours::black);
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawFittedText (juce::String (bpm) + " BPM", getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        dragStart = bpm;
        event.source.enableUnboundedMouseMovement (true, false);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        apply (dragStart - event.getDistanceFromDragStartY() / 3);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        apply (bpm + (wheel.deltaY > 0.0f ? 1 : -1));
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        apply (120);
    }

private:
    void apply (int value)
    {
        value = juce::jlimit (40, 240, value);

        if (value == bpm)
            return;

        bpm = value;

        if (onChange != nullptr)
            onChange (bpm);

        repaint();
    }

    int bpm = 120;
    int dragStart = 120;
};

static void styleCaption (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (10.0f)));
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, juce::Colours::black);
    label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label.setOpaque (false);
    label.setInterceptsMouseClicks (false, false);
}

class LoopView : public juce::Component
{
public:
    std::function<void (int, int)> onChange;

    void setRegion (std::vector<float> peaksIn, int lengthIn, int start, int end)
    {
        peaks = std::move (peaksIn);
        length = juce::jmax (2, lengthIn);
        const int gap = minimumGap();
        loopStart = juce::jlimit (0, length - gap, start);
        loopEnd = juce::jlimit (loopStart + gap, length, end > loopStart ? end : length);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (plateColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

        auto inner = getLocalBounds().reduced (6, 7).toFloat();
        const float startX = xFor (loopStart);
        const float endX = xFor (loopEnd);
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.fillRect (startX, inner.getY(), juce::jmax (1.0f, endX - startX), inner.getHeight());

        float peakMax = 0.001f;

        for (float peak : peaks)
            peakMax = juce::jmax (peakMax, peak);

        const int count = (int) peaks.size();

        for (int i = 0; i < count; ++i)
        {
            const float left = inner.getX() + inner.getWidth() * (float) i / (float) count;
            const float right = inner.getX() + inner.getWidth() * (float) (i + 1) / (float) count;
            const int sample = (int) (((float) i + 0.5f) / (float) count * (float) length);
            const bool inside = sample >= loopStart && sample < loopEnd;
            g.setColour (inside ? juce::Colours::black : juce::Colours::black.withAlpha (0.28f));
            const float bar = juce::jmax (1.0f, peaks[(size_t) i] / peakMax * (inner.getHeight() - 2.0f));
            g.fillRect (left, inner.getCentreY() - bar * 0.5f, juce::jmax (1.0f, right - left - 0.4f), bar);
        }

        g.setColour (juce::Colours::black);
        g.fillRect (startX - 1.0f, inner.getY(), 2.0f, inner.getHeight());
        g.fillRect (endX - 1.0f, inner.getY(), 2.0f, inner.getHeight());

        if (playhead >= 0)
        {
            const float x = xFor (juce::jlimit (0, length, playhead));
            g.setColour (juce::Colour (0xff0000ff));
            g.fillRect (x - 1.0f, inner.getY(), 2.0f, inner.getHeight());
        }
    }

    void setPlayhead (int sample)
    {
        const int next = sample < 0 ? -1 : juce::jlimit (0, length, sample);

        if (next == playhead)
            return;

        playhead = next;
        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        setMouseCursor (handleAt (event.x) == 0 ? juce::MouseCursor::NormalCursor
                                                 : juce::MouseCursor::LeftRightResizeCursor);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        drag = handleAt (event.x);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (drag == 0)
            return;

        const int sample = sampleAt (event.x);
        const int gap = minimumGap();

        if (drag == 1)
            loopStart = juce::jlimit (0, loopEnd - gap, sample);
        else
            loopEnd = juce::jlimit (loopStart + gap, length, sample);

        if (onChange != nullptr)
            onChange (loopStart, loopEnd);

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        drag = 0;
    }

private:
    int minimumGap() const
    {
        return juce::jmax (2, juce::jmin (64, length / 8));
    }

    float xFor (int sample) const
    {
        auto inner = getLocalBounds().reduced (6, 7).toFloat();
        return inner.getX() + inner.getWidth() * (float) sample / (float) juce::jmax (1, length);
    }

    int sampleAt (int x) const
    {
        auto inner = getLocalBounds().reduced (6, 7);

        if (inner.getWidth() <= 1)
            return 0;

        const float proportion = juce::jlimit (0.0f, 1.0f, (float) (x - inner.getX()) / (float) inner.getWidth());
        return (int) std::lround ((double) proportion * (double) length);
    }

    int handleAt (int x) const
    {
        const int startDistance = std::abs (x - (int) std::lround ((double) xFor (loopStart)));
        const int endDistance = std::abs (x - (int) std::lround ((double) xFor (loopEnd)));

        if (startDistance <= 7 && startDistance <= endDistance)
            return 1;

        if (endDistance <= 7)
            return 2;

        return 0;
    }

    std::vector<float> peaks;
    int length = 2;
    int loopStart = 0;
    int loopEnd = 2;
    int playhead = -1;
    int drag = 0;
};

class InkssSamplerEditor::QuantizePicker : public juce::Component
{
public:
    std::function<void (int)> onChange;

    void setSelected (int index)
    {
        index = juce::jlimit (0, 5, index);

        if (index == selected)
            return;

        selected = index;
        repaint();
    }

    void setActive (bool shouldBeActive)
    {
        if (shouldBeActive == active)
            return;

        active = shouldBeActive;
        repaint();
    }

    void setBarLengths (bool shouldUseBars)
    {
        if (shouldUseBars == barLengths)
            return;

        barLengths = shouldUseBars;
        repaint();
    }

    void setOnDark (bool shouldBeDark)
    {
        if (shouldBeDark == onDark)
            return;

        onDark = shouldBeDark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        static const char* divisions[] = { "none", "1/1", "1/2", "1/4", "1/8", "1/16" };
        static const char* bars[] = { "none", "1bar", "2bar", "4bar", "8bar", "16bar" };
        const char* const* labels = barLengths ? bars : divisions;
        const int width = getWidth();
        const int height = getHeight();
        g.setFont (juce::Font (juce::FontOptions (7.0f)));

        for (int i = 0; i < 6; ++i)
        {
            const int x0 = width * i / 6;
            const int x1 = width * (i + 1) / 6;
            auto cell = juce::Rectangle<int> (x0, 0, juce::jmax (1, x1 - x0), height).reduced (1, 0);
            const bool chosen = active && i == selected;

            if (chosen)
            {
                g.setColour (onDark ? juce::Colours::white : juce::Colour (0xff0000ff));
                g.fillRoundedRectangle (cell.toFloat(), 2.0f);
                g.setColour (onDark ? juce::Colour (0xff0000ff) : juce::Colours::white);
            }
            else if (! active)
            {
                g.setColour (juce::Colour (0xffb0b0b0));
            }
            else
            {
                g.setColour (onDark ? juce::Colours::white : juce::Colours::black);
            }

            g.drawFittedText (labels[i], cell, juce::Justification::centred, 1);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! active || getWidth() <= 0)
            return;

        const int index = juce::jlimit (0, 5, event.x * 6 / getWidth());

        if (index == selected)
            return;

        selected = index;

        if (onChange != nullptr)
            onChange (selected);

        repaint();
    }

private:
    int selected = 0;
    bool active = false;
    bool onDark = false;
    bool barLengths = false;
};

class PlayDirection : public juce::Component
{
public:
    std::function<void (bool)> onChange;

    void setReversed (bool shouldReverse)
    {
        if (shouldReverse == reversed)
            return;

        reversed = shouldReverse;
        repaint();
    }

    void setOnDark (bool shouldBeDark)
    {
        if (shouldBeDark == onDark)
            return;

        onDark = shouldBeDark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        constexpr int arrow = 16;
        constexpr int pad = 4;
        paintArrow (g, { pad, 0, arrow, getHeight() }, true, reversed);
        paintArrow (g, { getWidth() - pad - arrow, 0, arrow, getHeight() }, false, ! reversed);
    }

    bool hitTest (int x, int y) override
    {
        juce::ignoreUnused (y);
        constexpr int zone = 24;
        return x < zone || x >= getWidth() - zone;
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const bool next = event.x < getWidth() / 2;

        if (next == reversed)
            return;

        reversed = next;

        if (onChange != nullptr)
            onChange (reversed);

        repaint();
    }

private:
    void paintArrow (juce::Graphics& g, juce::Rectangle<int> area, bool pointsLeft, bool selected)
    {
        g.setColour (selected ? (onDark ? juce::Colours::white : juce::Colours::black)
                              : (onDark ? juce::Colours::white.withAlpha (0.45f) : mutedInk));

        auto bounds = area.withSizeKeepingCentre (7, 7).toFloat();
        juce::Path path;

        if (pointsLeft)
        {
            path.startNewSubPath (bounds.getRight(), bounds.getY());
            path.lineTo (bounds.getX(), bounds.getCentreY());
            path.lineTo (bounds.getRight(), bounds.getBottom());
        }
        else
        {
            path.startNewSubPath (bounds.getX(), bounds.getY());
            path.lineTo (bounds.getRight(), bounds.getCentreY());
            path.lineTo (bounds.getX(), bounds.getBottom());
        }

        path.closeSubPath();
        g.fillPath (path);
    }

    bool reversed = false;
    bool onDark = false;
};

class SpeedMark : public juce::Component
{
public:
    std::function<void (bool)> onChange;

    bool isOn() const { return on; }

    void setOn (bool shouldBeOn)
    {
        if (shouldBeOn == on)
            return;

        on = shouldBeOn;
        repaint();
    }

    void setOnDark (bool shouldBeDark)
    {
        if (shouldBeDark == onDark)
            return;

        onDark = shouldBeDark;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setFont (juce::Font (juce::FontOptions (8.0f)));
        const auto colour = ! isEnabled() ? mutedInk
                                           : on ? (onDark ? juce::Colours::white : juce::Colour (0xff0000ff))
                                                : (onDark ? juce::Colours::white.withAlpha (0.45f) : mutedInk);
        g.setColour (colour);
        g.drawFittedText ("s", getLocalBounds(), juce::Justification::centred, 1);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isEnabled())
            return;

        on = ! on;

        if (onChange != nullptr)
            onChange (on);

        repaint();
    }

private:
    bool on = false;
    bool onDark = false;
};

class OrderArrows : public juce::Component
{
public:
    std::function<void (int)> onMove;

    void setDirections (bool up, bool down)
    {
        canUp = up;
        canDown = down;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const int mid = getHeight() / 2;
        paintArrow (g, { 0, 0, getWidth(), mid }, true, canUp);
        paintArrow (g, { 0, mid, getWidth(), getHeight() - mid }, false, canDown);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const int direction = event.y < getHeight() / 2 ? -1 : 1;

        if ((direction < 0 && ! canUp) || (direction > 0 && ! canDown) || onMove == nullptr)
            return;

        onMove (direction);
    }

private:
    void paintArrow (juce::Graphics& g, juce::Rectangle<int> area, bool pointsUp, bool enabled)
    {
        g.setColour (enabled ? juce::Colours::black : mutedInk);
        auto bounds = area.withSizeKeepingCentre (7, 5).toFloat();
        juce::Path path;

        if (pointsUp)
        {
            path.startNewSubPath (bounds.getX(), bounds.getBottom());
            path.lineTo (bounds.getCentreX(), bounds.getY());
            path.lineTo (bounds.getRight(), bounds.getBottom());
        }
        else
        {
            path.startNewSubPath (bounds.getX(), bounds.getY());
            path.lineTo (bounds.getCentreX(), bounds.getBottom());
            path.lineTo (bounds.getRight(), bounds.getY());
        }

        path.closeSubPath();
        g.fillPath (path);
    }

    bool canUp = false;
    bool canDown = false;
};

class InkssSamplerEditor::SampleRow : public juce::Component
{
public:
    SampleRow (InkssSamplerProcessor& processorIn, int slotIn, const SampleEngine::SlotInfo& info)
        : sampler (processorIn), slot (slotIn)
    {
        name.setColour (juce::TextEditor::textColourId, juce::Colours::black);
        name.setColour (juce::TextEditor::highlightedTextColourId, juce::Colours::black);
        name.setColour (juce::CaretComponent::caretColourId, juce::Colours::black);
        name.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        name.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::black);
        name.setFont (juce::Font (juce::FontOptions (13.0f)));
        name.setText (info.name, juce::dontSendNotification);
        name.applyColourToAllText (juce::Colours::black, true);
        name.setJustification (juce::Justification::centredLeft);
        name.setIndents (4, 2);
        name.setOpaque (false);
        name.onReturnKey = [this] { commitName(); };
        name.onFocusLost = [this] { commitName(); };
        addAndMakeVisible (name);
        order.setDirections (sampler.canMoveSlot (slot, -1), sampler.canMoveSlot (slot, 1));
        order.onMove = [this] (int direction) { sampler.moveSlot (slot, direction); };
        addAndMakeVisible (order);

        duration.setText (juce::String (info.durationSeconds, 2) + " S", juce::dontSendNotification);
        duration.setJustificationType (juce::Justification::centredRight);
        duration.setFont (juce::Font (juce::FontOptions (10.0f)));
        duration.setColour (juce::Label::textColourId, juce::Colours::black);
        duration.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        duration.setOpaque (false);
        duration.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (duration);

        styleCaption (tuneLabel, "PITCH");
        styleCaption (fineLabel, "FINE");
        styleCaption (volumeLabel, "VOL");
        styleCaption (panLabel, "PAN");
        styleCaption (attackLabel, "ATTACK");
        styleCaption (releaseLabel, "RELEASE");
        styleCaption (lowLabel, "LOW");
        styleCaption (midLabel, "MID");
        styleCaption (highLabel, "HIGH");
        addAndMakeVisible (tuneLabel);
        addAndMakeVisible (fineLabel);
        addAndMakeVisible (volumeLabel);
        addAndMakeVisible (panLabel);
        addAndMakeVisible (attackLabel);
        addAndMakeVisible (releaseLabel);
        addAndMakeVisible (lowLabel);
        addAndMakeVisible (midLabel);
        addAndMakeVisible (highLabel);

        auto addKnob = [] (juce::Slider& slider, double start, double end, double interval, double value, double reset)
        {
            configureKnob (slider);
            slider.setRange (start, end, interval);
            slider.setValue (value, juce::dontSendNotification);
            slider.setDoubleClickReturnValue (true, reset);
        };

        addKnob (tune, -24.0, 24.0, 1.0, info.tune, 0.0);
        tune.setTextValueSuffix (" ST");
        tune.setNumDecimalPlacesToDisplay (0);
        tune.onValueChange = [this] { sampler.setSlotTune (slot, (float) tune.getValue()); };
        addAndMakeVisible (tune);

        addKnob (fine, -100.0, 100.0, 1.0, info.fine, 0.0);
        fine.setTextValueSuffix (" CT");
        fine.setNumDecimalPlacesToDisplay (0);
        fine.onValueChange = [this] { sampler.setSlotFine (slot, (float) fine.getValue()); };
        addAndMakeVisible (fine);

        addKnob (volume, 0.0, 100.0, 1.0, info.gain * 100.0, 100.0);
        volume.setTextValueSuffix ("%");
        volume.setNumDecimalPlacesToDisplay (0);
        volume.onValueChange = [this]
        {
            sampler.setSlotGain (slot, (float) volume.getValue() / 100.0f);
        };
        addAndMakeVisible (volume);

        addKnob (pan, -100.0, 100.0, 1.0, info.pan * 100.0, 0.0);
        pan.setNumDecimalPlacesToDisplay (0);
        pan.setTextValueSuffix (" CT");
        pan.onValueChange = [this]
        {
            sampler.setSlotPan (slot, (float) pan.getValue() / 100.0f);
        };
        addAndMakeVisible (pan);

        addKnob (attack, 0.0, 2.0, 0.01, info.attackSeconds, 0.01);
        attack.setSkewFactorFromMidPoint (0.1);
        attack.setTextValueSuffix (" S");
        attack.setNumDecimalPlacesToDisplay (2);
        attack.onValueChange = [this] { sampler.setSlotAttack (slot, (float) attack.getValue()); };
        addAndMakeVisible (attack);

        addKnob (release, 0.0, 4.0, 0.01, info.releaseSeconds, 0.05);
        release.setSkewFactorFromMidPoint (0.2);
        release.setTextValueSuffix (" S");
        release.setNumDecimalPlacesToDisplay (2);
        release.onValueChange = [this] { sampler.setSlotRelease (slot, (float) release.getValue()); };
        addAndMakeVisible (release);

        auto addEqSlider = [this] (juce::Slider& slider, int band, float value)
        {
            configureKnob (slider);
            slider.setRange (-24.0, 24.0, 0.1);
            slider.setValue (value, juce::dontSendNotification);
            slider.setDoubleClickReturnValue (true, 0.0);
            slider.setTextValueSuffix (" DP");
            slider.setNumDecimalPlacesToDisplay (1);
            slider.onValueChange = [this, &slider, band]
            {
                sampler.setSlotEq (slot, band, (float) slider.getValue());
            };
            addAndMakeVisible (slider);
        };

        addEqSlider (low, 0, info.lowDb);
        addEqSlider (mid, 1, info.midDb);
        addEqSlider (high, 2, info.highDb);

        key.setClickingTogglesState (true);
        key.setToggleState (info.armed, juce::dontSendNotification);
        key.onClick = [this]
        {
            sampler.setSlotArmed (slot, key.getToggleState());
            showEnvelope (key.getToggleState());
        };
        addAndMakeVisible (key);

        sync.setClickingTogglesState (true);
        sync.setToggleState (info.synced, juce::dontSendNotification);
        sync.getProperties().set ("rightInset", 12);
        sync.onClick = [this]
        {
            sampler.setSlotSync (slot, sync.getToggleState());
            refreshSpeedMark();
        };
        sync.setEnabled (sampler.isSyncEnabled());
        addAndMakeVisible (sync);

        speed.setOn (info.speed);
        speed.onChange = [this] (bool on)
        {
            sampler.setSlotSpeed (slot, on);
            refreshSpeedMark();
        };
        sync.addAndMakeVisible (speed);
        refreshSpeedMark();

        finder.onClick = [this] { sampler.revealSlot (slot); };
        addAndMakeVisible (finder);

        remove.onClick = [this] { sampler.clearSlot (slot); };
        addAndMakeVisible (remove);

        play.onClick = [this] { sampler.toggleSlot (slot); };
        play.getProperties().set ("topInset", 15);
        playQuantize.setSelected (info.playQuantize);
        playQuantize.setActive (sampler.isSyncEnabled());
        playQuantize.onChange = [this] (int division) { sampler.setSlotPlayQuantize (slot, division); };
        play.addAndMakeVisible (playQuantize);
        playDirection.setReversed (info.reversed);
        playDirection.onChange = [this] (bool reversed) { sampler.setSlotReverse (slot, reversed); };
        play.addAndMakeVisible (playDirection);
        addAndMakeVisible (play);

        loopView.setRegion (sampler.copyPeaks (slot, 140), info.length, info.loopStart, info.loopEnd);
        loopView.onChange = [this] (int start, int end) { sampler.setSlotLoop (slot, start, end); };
        addAndMakeVisible (loopView);
        showEnvelope (info.armed);
        setPlaying (info.occupied && sampler.isSlotPlaying (slot));
    }

    int preferredHeight() const
    {
        return 80;
    }

    void setPlaying (bool playing)
    {
        if (playing == isPlaying)
            return;

        isPlaying = playing;
        play.setButtonText (playing ? "STOP" : "PLAY");
        play.setToggleState (playing, juce::dontSendNotification);
        playQuantize.setOnDark (playing);
        playDirection.setOnDark (playing);
    }

    void setPlayback (bool playing, int position)
    {
        setPlaying (playing);
        loopView.setPlayhead (playing ? position : -1);
    }

    void setSyncEnabled (bool enabled)
    {
        if (sync.isEnabled() == enabled)
            return;

        sync.setEnabled (enabled);
        sync.repaint();
        playQuantize.setActive (enabled);
        refreshSpeedMark();
    }

    void refreshSpeedMark()
    {
        const bool synced = sync.isEnabled() && sync.getToggleState();
        speed.setEnabled (synced);
        speed.setOnDark (synced);
        sync.setButtonText (synced && speed.isOn() ? "SPEED" : "SYNC");
    }

    int getSlot() const { return slot; }

    void resized() override
    {
        plates.clear();
        auto row = getLocalBounds().reduced (8, 8);
        constexpr int sideWidth = 84;
        constexpr int playWidth = 138;
        constexpr int playHeight = 40;
        constexpr int keyHeight = 20;
        constexpr int sideHeight = 28;
        auto left = row.removeFromLeft (playWidth);
        play.setBounds (left.removeFromTop (playHeight));
        playQuantize.setBounds (play.getLocalBounds().removeFromTop (15).reduced (3, 2));
        playDirection.setBounds (0, 15, play.getWidth(), play.getHeight() - 15);
        left.removeFromTop (4);
        auto keys = left.removeFromTop (keyHeight);
        const int half = (playWidth - 4) / 2;
        key.setBounds (keys.removeFromLeft (half));
        keys.removeFromLeft (4);
        sync.setBounds (keys);
        speed.setBounds (sync.getWidth() - 14, 0, 14, sync.getHeight());

        row.removeFromLeft (8);
        auto side = row.removeFromRight (sideWidth);
        finder.setBounds (side.removeFromTop (sideHeight));
        side.removeFromTop (8);
        remove.setBounds (side.removeFromTop (sideHeight));
        row.removeFromRight (8);

        const int groupCount = 4;
        const int cells = 9;
        constexpr int namePref = 140;
        constexpr int loopMin = 120;
        int column = knobColumnWidth;
        auto knobsWidthFor = [&] (int col)
        {
            return cells * col + groupCount * 6 + (groupCount - 1) * 8;
        };

        while (column > 40 && 96 + 8 + knobsWidthFor (column) + 8 + loopMin > row.getWidth())
            column -= 2;

        const int knobsWidth = knobsWidthFor (column);
        const int room = row.getWidth() - knobsWidth - 16;
        const int nameWidth = juce::jlimit (96, namePref, juce::jmax (96, room - loopMin));
        const int knobY = row.getY() + (row.getHeight() - knobPlateHeight) / 2;
        auto nameArea = juce::Rectangle<int> (row.getX(), knobY, nameWidth, knobPlateHeight);
        row.removeFromLeft (nameWidth);
        namePlate = nameArea;
        order.setBounds (nameArea.getX() + 2, nameArea.getY() + 4, 14, nameArea.getHeight() - 8);
        duration.setBounds (nameArea.getRight() - 48, nameArea.getY() + 3, 44, 12);
        auto nameText = nameArea.reduced (6, 0);
        nameText.removeFromLeft (14);
        name.setBounds (nameText.withSizeKeepingCentre (juce::jmax (20, nameText.getWidth()), 18));
        row.removeFromLeft (8);

        auto place = [&] (const std::vector<std::pair<juce::Slider*, juce::Label*>>& knobs, bool envelope = false)
        {
            auto plate = layoutKnobGroup (row.getX(), knobY, column, knobs);
            plate.envelope = envelope;
            plates.push_back (plate);
            row.removeFromLeft (plate.bounds.getWidth() + 8);
        };

        place ({ { &volume, &volumeLabel }, { &pan, &panLabel } });
        place ({ { &tune, &tuneLabel }, { &fine, &fineLabel } });
        place ({ { &low, &lowLabel }, { &mid, &midLabel }, { &high, &highLabel } });
        place ({ { &attack, &attackLabel }, { &release, &releaseLabel } }, true);
        loopView.setBounds (row.getX(), knobY, juce::jmax (0, row.getWidth()), knobPlateHeight);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (getLocalBounds().reduced (4, 0).toFloat(), 8.0f);
        g.setColour (plateColour);
        g.fillRoundedRectangle (namePlate.toFloat(), 8.0f);

        for (const auto& plate : plates)
            paintKnobPlate (g, plate.bounds, plate.sections, plate.envelope && ! key.getToggleState());
    }

private:

    void commitName()
    {
        const auto text = name.getText().trim();

        if (text.isEmpty())
        {
            name.setText (sampler.getSlotInfo (slot).name, juce::dontSendNotification);
            return;
        }

        sampler.setSlotName (slot, text);
    }

    void showEnvelope (bool active)
    {
        const auto text = active ? juce::Colours::black : mutedInk;

        for (auto* slider : { &attack, &release })
        {
            slider->setEnabled (active);
            slider->setColour (juce::Slider::textBoxTextColourId, text);
        }

        attackLabel.setColour (juce::Label::textColourId, text);
        releaseLabel.setColour (juce::Label::textColourId, text);
        repaint();
    }

    InkssSamplerProcessor& sampler;
    int slot = 0;
    juce::Rectangle<int> namePlate;
    std::vector<KnobPlate> plates;
    juce::TextEditor name;
    OrderArrows order;
    juce::Label duration;
    juce::Label tuneLabel;
    juce::Label fineLabel;
    juce::Label volumeLabel;
    juce::Label panLabel;
    juce::Label attackLabel;
    juce::Label releaseLabel;
    juce::Label lowLabel;
    juce::Label midLabel;
    juce::Label highLabel;
    juce::Slider tune;
    juce::Slider fine;
    juce::Slider volume;
    juce::Slider pan;
    juce::Slider attack;
    juce::Slider release;
    juce::Slider low;
    juce::Slider mid;
    juce::Slider high;
    juce::TextButton key { "KEY" };
    juce::TextButton sync { "SYNC" };
    SpeedMark speed;
    juce::TextButton finder { "SHOW IN\nFINDER" };
    juce::TextButton remove { "DELETE\nFILE" };
    juce::TextButton play { "PLAY" };
    QuantizePicker playQuantize;
    PlayDirection playDirection;
    LoopView loopView;
    bool isPlaying = false;
};

InkssSamplerEditor::InkssSamplerEditor (InkssSamplerProcessor& processorIn)
    : AudioProcessorEditor (processorIn),
      sampler (processorIn)
{
    setOpaque (true);
    lookAndFeel = std::make_unique<InkLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());

    if (sampler.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        optionsButton.onClick = [this] { showOptionsMenu(); };
        addAndMakeVisible (optionsButton);
        sampler.openStandaloneInput();
    }

    styleCaption (gainLabel, "VOL");
    styleCaption (coarseLabel, "PITCH");
    styleCaption (fineLabel, "FINE");
    styleCaption (lowLabel, "LOW");
    styleCaption (midLabel, "MID");
    styleCaption (highLabel, "HIGH");
    addAndMakeVisible (gainLabel);
    addAndMakeVisible (coarseLabel);
    addAndMakeVisible (fineLabel);
    addAndMakeVisible (lowLabel);
    addAndMakeVisible (midLabel);
    addAndMakeVisible (highLabel);

    configureKnob (gainSlider);
    gainSlider.setTextValueSuffix ("%");
    gainSlider.setNumDecimalPlacesToDisplay (0);
    gainSlider.setDoubleClickReturnValue (true, 70.0);
    addAndMakeVisible (gainSlider);

    configureKnob (tuneSlider);
    tuneSlider.setTextValueSuffix (" ST");
    tuneSlider.setNumDecimalPlacesToDisplay (0);
    tuneSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (tuneSlider);

    configureKnob (fineSlider);
    fineSlider.setTextValueSuffix (" CT");
    fineSlider.setNumDecimalPlacesToDisplay (0);
    fineSlider.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (fineSlider);

    auto configureEq = [this] (juce::Slider& slider)
    {
        configureKnob (slider);
        slider.setRange (-24.0, 24.0, 0.1);
        slider.setDoubleClickReturnValue (true, 0.0);
        slider.setTextValueSuffix (" DP");
        slider.setNumDecimalPlacesToDisplay (1);
        addAndMakeVisible (slider);
    };

    configureEq (lowSlider);
    configureEq (midSlider);
    configureEq (highSlider);

    loopButton.setClickingTogglesState (true);
    addAndMakeVisible (loopButton);

    syncButton.setClickingTogglesState (true);
    addAndMakeVisible (syncButton);

    playAllButton.onClick = [this]
    {
        const bool playing = sampler.isAnyPlaying();
        auto* parameter = sampler.getApvts().getParameter ("play");
        const bool held = parameter != nullptr && parameter->getValue() >= 0.5f;

        if (playing)
        {
            if (held)
                parameter->setValueNotifyingHost (0.0f);
            else
                sampler.stopAllSamples();
        }
        else if (held)
        {
            sampler.playAllSamples();
        }
        else if (parameter != nullptr)
        {
            parameter->setValueNotifyingHost (1.0f);
        }
    };
    playAllButton.getProperties().set ("topInset", 15);
    playQuantize = std::make_unique<QuantizePicker>();
    playQuantize->setSelected (sampler.getPlayQuantize());
    playQuantize->setActive (sampler.isSyncEnabled());
    playQuantize->onChange = [this] (int division) { sampler.setPlayQuantize (division); };
    playAllButton.addAndMakeVisible (playQuantize.get());
    addAndMakeVisible (playAllButton);

    loadButton.onClick = [this] { browseForSamples(); };
    addAndMakeVisible (loadButton);

    recordButton.onClick = [this]
    {
        if (sampler.isRecording())
            sampler.stopCapture();
        else
            sampler.startRecording();
    };
    addAndMakeVisible (recordButton);

    resampleButton.onClick = [this]
    {
        if (sampler.isResampling())
            sampler.stopCapture();
        else
            sampler.startResampling();
    };
    addAndMakeVisible (resampleButton);

    auto attachQuantize = [] (juce::TextButton& button, std::unique_ptr<QuantizePicker>& picker, int division, auto changed)
    {
        button.getProperties().set ("topInset", 16);
        picker = std::make_unique<QuantizePicker>();
        picker->setSelected (division);
        picker->onChange = std::move (changed);
        button.addAndMakeVisible (picker.get());
    };

    attachQuantize (recordButton, recordQuantize, sampler.getRecordQuantize(),
                    [this] (int division) { sampler.setRecordQuantize (division); });
    attachQuantize (resampleButton, resampleQuantize, sampler.getResampleQuantize(),
                    [this] (int division) { sampler.setResampleQuantize (division); });
    recordQuantize->setActive (sampler.isSyncEnabled());
    resampleQuantize->setActive (sampler.isSyncEnabled());
    recordQuantize->setBarLengths (true);
    resampleQuantize->setBarLengths (true);

    clearButton.onClick = [this]
    {
        sampler.clearAllSamples();
        refreshDiskLabels();
    };
    addAndMakeVisible (clearButton);

    clearFilesButton.onClick = [this]
    {
        sampler.removeUnreferencedCaptures();
        refreshDiskLabels();
    };
    addAndMakeVisible (clearFilesButton);

    sourceLights = std::make_unique<SourceLights>();
    addAndMakeVisible (*sourceLights);

    if (sampler.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        tempoBox = std::make_unique<TempoBox>();
        tempoBox->setBpm (juce::roundToInt (sampler.getAppBpm()));
        tempoBox->onChange = [this] (int bpm) { sampler.setAppBpm ((double) bpm); };
        addAndMakeVisible (*tempoBox);
    }

    status.setJustificationType (juce::Justification::centredRight);
    status.setFont (juce::Font (juce::FontOptions (10.0f)));
    status.setColour (juce::Label::textColourId, juce::Colours::black);
    addAndMakeVisible (status);

    empty.setText ("DROP AUDIO", juce::dontSendNotification);
    empty.setJustificationType (juce::Justification::centred);
    empty.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (empty);

    wordmark = juce::ImageFileFormat::loadFrom (BinaryData::wordmark_png, BinaryData::wordmark_pngSize);

    if (wordmark.isValid())
    {
        wordmark = wordmark.convertedToFormat (juce::Image::ARGB);
        juce::Image::BitmapData pixels (wordmark, juce::Image::BitmapData::readWrite);

        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                pixels.setPixelColour (x, y, pixels.getPixelColour (x, y).darker (0.1f));
    }

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setOpaque (false);
    list.setOpaque (true);
    addAndMakeVisible (viewport);

    auto& apvts = sampler.getApvts();
    gainAttachment = std::make_unique<SliderAttachment> (apvts, "gain", gainSlider);
    tuneAttachment = std::make_unique<SliderAttachment> (apvts, "tune", tuneSlider);
    fineAttachment = std::make_unique<SliderAttachment> (apvts, "tuneFine", fineSlider);
    lowAttachment = std::make_unique<SliderAttachment> (apvts, "eqLow", lowSlider);
    midAttachment = std::make_unique<SliderAttachment> (apvts, "eqMid", midSlider);
    highAttachment = std::make_unique<SliderAttachment> (apvts, "eqHigh", highSlider);
    loopAttachment = std::make_unique<ButtonAttachment> (apvts, "loop", loopButton);
    syncAttachment = std::make_unique<ButtonAttachment> (apvts, "sync", syncButton);

    auto showRounded = [] (juce::Slider& slider, const juce::String& suffix)
    {
        slider.textFromValueFunction = [suffix] (double value)
        {
            return juce::String (juce::roundToInt (value)) + suffix;
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            return text.getDoubleValue();
        };
        slider.updateText();
    };

    showRounded (gainSlider, "%");
    showRounded (tuneSlider, " ST");
    showRounded (fineSlider, " CT");

    auto showEq = [] (juce::Slider& slider)
    {
        slider.textFromValueFunction = [] (double value)
        {
            return juce::String (value, 1) + " DP";
        };
        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            return text.getDoubleValue();
        };
        slider.updateText();
    };

    showEq (lowSlider);
    showEq (midSlider);
    showEq (highSlider);

    status.setText (sampler.getStatus(), juce::dontSendNotification);

    sampler.addChangeListener (this);
    setResizable (true, true);
    setResizeLimits (1140, 420, 2200, 1100);
    setSize (1180, 560);
    setWantsKeyboardFocus (true);
    rebuildRows();
    refreshDiskLabels();
    startTimerHz (30);
}

InkssSamplerEditor::~InkssSamplerEditor()
{
    stopTimer();
    sampler.removeChangeListener (this);
    releaseHeldNotes();
    setLookAndFeel (nullptr);
}

void InkssSamplerEditor::mouseDown (const juce::MouseEvent&)
{
    grabKeyboardFocus();
}

void InkssSamplerEditor::paint (juce::Graphics& g)
{
    g.fillAll (pageColour);

    if (! headerCard.isEmpty())
    {
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (headerCard.toFloat(), 8.0f);
    }

    if (wordmark.isValid())
        g.drawImage (wordmark, wordmarkBounds.toFloat(), juce::RectanglePlacement::centred);

    paintKnobPlate (g, headerKnobBounds, headerKnobSections);
    paintKnobPlate (g, headerEqBounds, headerEqSections);

    if (dragging)
    {
        g.setColour (juce::Colour (0xff0000ff));
        g.drawRect (getLocalBounds(), 3);
    }
}

void InkssSamplerEditor::resized()
{
    auto bounds = getLocalBounds().reduced (8);
    auto top = bounds.removeFromTop (34);

    if (optionsButton.isVisible())
    {
        optionsButton.setBounds (top.removeFromRight (78).withSizeKeepingCentre (78, 20));
        top.removeFromRight (8);
    }

    sourceLights->setBounds (top.removeFromRight (150).withSizeKeepingCentre (150, 20));

    if (tempoBox != nullptr)
    {
        top.removeFromRight (8);
        tempoBox->setBounds (top.removeFromRight (72).withSizeKeepingCentre (72, 20));
    }

    if (wordmark.isValid())
    {
        const int markHeight = 26;
        const int markWidth = juce::roundToInt ((float) wordmark.getWidth() / (float) juce::jmax (1, wordmark.getHeight()) * (float) markHeight);
        wordmarkBounds = top.withSizeKeepingCentre (juce::jmin (markWidth, top.getWidth()), markHeight)
                             .withX (top.getX());
    }

    auto statusArea = top;

    if (wordmark.isValid())
        statusArea.removeFromLeft (juce::jmax (0, wordmarkBounds.getRight() - statusArea.getX() + 12));

    const int statusWidth = juce::jmin (140, statusArea.getWidth());
    status.setBounds (statusArea.removeFromRight (statusWidth).withSizeKeepingCentre (statusWidth, 12));

    bounds.removeFromTop (8);
    auto header = bounds.removeFromTop (80);
    headerCard = header;
    header = header.reduced (8);
    constexpr int gap = 8;
    constexpr int cluster = 180;

    auto left = header.removeFromLeft (cluster);
    playAllButton.setBounds (left.removeFromTop (40));

    if (playQuantize != nullptr)
        playQuantize->setBounds (playAllButton.getLocalBounds().removeFromTop (15).reduced (3, 2));

    left.removeFromTop (4);
    auto modeRow = left.removeFromTop (20);
    const int modeWidth = (cluster - 4) / 2;
    loopButton.setBounds (modeRow.removeFromLeft (modeWidth));
    modeRow.removeFromLeft (4);
    syncButton.setBounds (modeRow.removeFromLeft (modeWidth));

    header.removeFromLeft (gap);
    constexpr int fileWidth = 128;
    auto files = header.removeFromRight (fileWidth);
    clearFilesButton.setBounds (files.removeFromTop (28));
    files.removeFromTop (gap);
    clearButton.setBounds (files.removeFromTop (28));

    header.removeFromRight (gap);
    const int plateWidth = knobColumnWidth * 3 + 6;
    auto eqArea = header.removeFromRight (plateWidth);
    header.removeFromRight (gap);
    auto knobArea = header.removeFromRight (plateWidth);
    header.removeFromRight (gap);

    const int transportWidth = juce::jmax (1, (header.getWidth() - gap * 2) / 3);
    const int transportY = header.getY();
    const int transportHeight = header.getHeight();
    int transportX = header.getX();
    loadButton.setBounds (transportX, transportY, transportWidth, transportHeight);
    transportX += transportWidth + gap;
    recordButton.setBounds (transportX, transportY, transportWidth, transportHeight);
    transportX += transportWidth + gap;
    resampleButton.setBounds (transportX, transportY, header.getRight() - transportX, transportHeight);

    auto placeQuantize = [] (juce::Component& button, QuantizePicker& picker)
    {
        picker.setBounds (button.getLocalBounds().removeFromTop (16).reduced (4, 3));
    };

    if (recordQuantize != nullptr)
        placeQuantize (recordButton, *recordQuantize);

    if (resampleQuantize != nullptr)
        placeQuantize (resampleButton, *resampleQuantize);

    const int fieldY = knobArea.getY() + (knobArea.getHeight() - knobPlateHeight) / 2;
    auto plate = layoutKnobGroup (knobArea.getX(), fieldY, knobColumnWidth,
                                  { { &gainSlider, &gainLabel },
                                    { &tuneSlider, &coarseLabel },
                                    { &fineSlider, &fineLabel } });
    headerKnobBounds = plate.bounds;
    headerKnobSections = plate.sections;
    auto eqPlate = layoutKnobGroup (eqArea.getX(), fieldY, knobColumnWidth,
                                    { { &lowSlider, &lowLabel },
                                      { &midSlider, &midLabel },
                                      { &highSlider, &highLabel } });
    headerEqBounds = eqPlate.bounds;
    headerEqSections = eqPlate.sections;

    bounds.removeFromTop (8);
    viewport.setBounds (bounds);
    empty.setBounds (viewport.getBounds());
    layoutRows();
}

void InkssSamplerEditor::parentHierarchyChanged()
{
    if (windowChromeReady || sampler.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
        return;

    auto* window = findParentComponentOfClass<juce::DocumentWindow>();

    if (window == nullptr)
        return;

    windowChromeReady = true;
    window->setColour (juce::ResizableWindow::backgroundColourId, pageColour);
    window->setTitleBarButtonsRequired (juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton, true);
    window->setUsingNativeTitleBar (true);
    window->setName ("INKSS SAMPLE LOOPER");

    for (int i = 0; i < window->getNumChildComponents(); ++i)
        if (auto* button = dynamic_cast<juce::TextButton*> (window->getChildComponent (i)))
            if (button->getButtonText() == "Options")
                button->setVisible (false);
}

void InkssSamplerEditor::showOptionsMenu()
{
   #if JucePlugin_Build_Standalone
    juce::PopupMenu menu;
    menu.addItem (1, "AUDIO / MIDI");
    menu.addSeparator();
    menu.addItem (2, "SAVE");
    menu.addItem (3, "LOAD");
    menu.addSeparator();
    menu.addItem (4, "DELETE FILES");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&optionsButton),
                        [this] (int result)
                        {
                            if (result == 0)
                                return;

                            if (result == 4)
                            {
                                sampler.removeUnreferencedCaptures();
                                return;
                            }

                            auto* holder = juce::StandalonePluginHolder::getInstance();

                            if (holder == nullptr)
                                return;

                            if (result == 1)
                                holder->showAudioSettingsDialog();
                            else if (result == 2)
                                holder->askUserToSaveState();
                            else if (result == 3)
                                holder->askUserToLoadState();
                        });
   #endif
}

void InkssSamplerEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    status.setText (sampler.getStatus(), juce::dontSendNotification);
    refreshDiskLabels();
    rebuildRows();
}

void InkssSamplerEditor::timerCallback()
{
    for (int i = (int) heldNotes.size(); --i >= 0;)
    {
        if (! juce::KeyPress (heldNotes[(size_t) i].character).isCurrentlyDown())
        {
            sampler.getKeyboardState().noteOff (1, heldNotes[(size_t) i].note, 0.0f);
            heldNotes.erase (heldNotes.begin() + i);
        }
    }

    status.setText (sampler.getStatus(), juce::dontSendNotification);

    if (++diskTick >= 30)
    {
        diskTick = 0;
        refreshDiskLabels();
    }

    setTransportButton (recordButton, sampler.isRecording(), "RECORD");
    setTransportButton (resampleButton, sampler.isResampling(), "RESAMPLE");

    shownInput = juce::jmax (sampler.getInputLevel(), shownInput * 0.82f);
    shownOutput = juce::jmax (sampler.getOutputLevel(), shownOutput * 0.82f);
    recordButton.getProperties().set ("level", shownInput);
    resampleButton.getProperties().set ("level", shownOutput);
    recordButton.repaint();
    resampleButton.repaint();

    if (sampler.consumeMidiActivity())
        midiHold = 5;
    else if (midiHold > 0)
        --midiHold;

    sourceLights->setSources (! heldNotes.empty(), midiHold > 0);

    const bool syncOn = sampler.isSyncEnabled();

    if (recordQuantize != nullptr)
    {
        recordQuantize->setActive (syncOn);
        recordQuantize->setOnDark (sampler.isRecording());
    }

    if (resampleQuantize != nullptr)
    {
        resampleQuantize->setActive (syncOn);
        resampleQuantize->setOnDark (sampler.isResampling());
    }

    bool anyPlaying = false;

    for (auto& row : rows)
    {
        const bool playing = sampler.isSlotPlaying (row->getSlot());
        anyPlaying = anyPlaying || playing;
        row->setPlayback (playing, sampler.getSlotPosition (row->getSlot()));
        row->setSyncEnabled (syncOn);
    }

    setTransportButton (playAllButton, anyPlaying, anyPlaying ? "STOP" : "PLAY");

    if (playQuantize != nullptr)
    {
        playQuantize->setActive (syncOn);
        playQuantize->setOnDark (anyPlaying);
    }

    if (tempoBox != nullptr)
        tempoBox->setBpm (juce::roundToInt (sampler.getAppBpm()));
}

bool InkssSamplerEditor::keyPressed (const juce::KeyPress& key)
{
    const int note = noteForCharacter (key.getTextCharacter());

    if (note < 0)
        return false;

    const auto character = key.getTextCharacter();

    for (const auto& held : heldNotes)
        if (held.character == character)
            return true;

    heldNotes.push_back ({ character, note });
    sampler.getKeyboardState().noteOn (1, note, 0.9f);
    return true;
}

bool InkssSamplerEditor::keyStateChanged (bool)
{
    for (int i = (int) heldNotes.size(); --i >= 0;)
    {
        if (! juce::KeyPress (heldNotes[(size_t) i].character).isCurrentlyDown())
        {
            sampler.getKeyboardState().noteOff (1, heldNotes[(size_t) i].note, 0.0f);
            heldNotes.erase (heldNotes.begin() + i);
        }
    }

    return false;
}

bool InkssSamplerEditor::isInterestedInFileDrag (const juce::StringArray&)
{
    return true;
}

void InkssSamplerEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragging = true;
    repaint();
}

void InkssSamplerEditor::fileDragExit (const juce::StringArray&)
{
    dragging = false;
    repaint();
}

void InkssSamplerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragging = false;
    repaint();

    juce::Array<juce::File> audio;

    for (const auto& path : files)
        audio.add (juce::File (path));

    sampler.loadSamples (audio);
}

void InkssSamplerEditor::rebuildRows()
{
    rows.clear();

    for (int i = 0; i < SampleEngine::maxSlots; ++i)
    {
        const auto info = sampler.getSlotInfo (i);

        if (! info.occupied)
            continue;

        auto row = std::make_unique<SampleRow> (sampler, i, info);
        list.addAndMakeVisible (row.get());
        rows.push_back (std::move (row));
    }

    empty.setVisible (rows.empty());
    layoutRows();
}

void InkssSamplerEditor::layoutRows()
{
    const int width = juce::jmax (200, viewport.getMaximumVisibleWidth());
    int y = 0;

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const int rowHeight = rows[(size_t) i]->preferredHeight();
        rows[(size_t) i]->setBounds (0, y, width, rowHeight);
        y += rowHeight;

        if (i + 1 < (int) rows.size())
            y += 6;
    }

    list.setSize (width, juce::jmax (y, viewport.getHeight()));
}

void InkssSamplerEditor::refreshDiskLabels()
{
    const auto counts = sampler.getDiskCounts();
    const double megabytes = (double) counts.unusedBytes / (1024.0 * 1024.0);
    int decimals = 1;

    if (megabytes >= 10.0)
        decimals = 0;
    else if (counts.unusedBytes > 0 && megabytes < 0.1)
        decimals = 2;

    const auto size = counts.unusedBytes <= 0 ? juce::String ("0 MB")
                                               : juce::String (megabytes, decimals) + " MB";
    const auto unused = juce::String (counts.unusedFiles) + "  " + size;
    const auto session = juce::String (counts.sessionFiles);

    const auto filesText = "DELETE FILES\n" + unused;
    const auto sessionText = "CLEAR SESSION\n" + session;

    if (clearFilesButton.getButtonText() != filesText)
        clearFilesButton.setButtonText (filesText);

    if (clearButton.getButtonText() != sessionText)
        clearButton.setButtonText (sessionText);
}

void InkssSamplerEditor::browseForSamples()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load samples",
        juce::File{},
        "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");

    const auto flags = juce::FileBrowserComponent::openMode
                       | juce::FileBrowserComponent::canSelectFiles
                       | juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync (flags, [this] (const juce::FileChooser& dialog)
    {
        sampler.loadSamples (dialog.getResults());
    });
}

void InkssSamplerEditor::releaseHeldNotes()
{
    for (const auto& held : heldNotes)
        sampler.getKeyboardState().noteOff (1, held.note, 0.0f);

    heldNotes.clear();
}

int InkssSamplerEditor::noteForCharacter (juce::juce_wchar character)
{
    switch (juce::CharacterFunctions::toLowerCase (character))
    {
        case 'z': return 48;
        case 'x': return 50;
        case 'c': return 52;
        case 'v': return 53;
        case 'b': return 55;
        case 'n': return 57;
        case 'm': return 59;
        case 'a': return 60;
        case 'w': return 61;
        case 's': return 62;
        case 'e': return 63;
        case 'd': return 64;
        case 'f': return 65;
        case 't': return 66;
        case 'g': return 67;
        case 'y': return 68;
        case 'h': return 69;
        case 'u': return 70;
        case 'j': return 71;
        case 'k': return 72;
        case 'o': return 73;
        case 'l': return 74;
        case 'p': return 75;
        case 0x00f6: return 76;
        case 0x00e4: return 77;
        default: return -1;
    }
}
