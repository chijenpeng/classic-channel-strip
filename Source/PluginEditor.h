#pragma once

#include "PluginProcessor.h"

#include <functional>
#include <optional>

/** The embedded typefaces (Inter, JetBrains Mono). Shared by all open editors and released with
    the last one, so nothing outlives the host's JUCE shutdown. */
struct SslFonts
{
    SslFonts();
    juce::Typeface::Ptr inter, mono;
};

/** Flat drawing for the knobs (a disc with a dot; colour from Slider::thumbColourId) and the
    window's resize corner; everything else paints itself. */
class SslLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
};

/** A flat disc with its name above and value below. Drag (Shift: fine), wheel,
    double-click the disc for the default, double-click the value to type one. */
class Knob final : public juce::Component
{
public:
    using Formatter = std::function<juce::String (float)>;
    static constexpr int kLabelHeight = 16, kGap = 8;

    Knob (juce::RangedAudioParameter&, const juce::String& name, Formatter, int diameter = 48);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void setFaceColour (juce::Colour colour) { slider.setColour (juce::Slider::thumbColourId, colour); }
    int preferredHeight() const noexcept { return 2 * (kLabelHeight + kGap) + diameter; }

    /** "4.9k" -> 4900, "-3 dB" -> -3, "2.50 kHz" -> 2500; nothing without a number. */
    static std::optional<double> parseNumber (const juce::String&);

private:
    void finishEditing (bool apply);
    std::optional<double> parse (const juce::String&) const;

    juce::RangedAudioParameter& param;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::String name;
    Formatter format;
    juce::TextEditor editor;
    juce::SliderParameterAttachment attachment;
    int diameter;
};

/** "LABEL  OPTION  OPTION ..." as plain text; the selected option is lit. An empty label starts
    the options at the left edge. */
class OptionRow final : public juce::Component
{
public:
    OptionRow (juce::RangedAudioParameter&, const juce::String& label, juce::StringArray options);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** The label's own width; rows stacked together share the widest plus the label gap. */
    int labelTextWidth() const;
    void setLabelColumn (int width) { labelColumn = width; repaint(); }
    int preferredWidth() const;

private:
    std::vector<juce::Range<int>> optionRanges() const;
    int optionAt (int x) const;

    juce::String label;
    juce::StringArray options;
    int labelColumn = 0, selected = 0, hovered = -1;
    juce::ParameterAttachment attachment;
};

/** A thin flat peak bar, -40 to +6 dBFS with 0 dBFS marked: instant rise, about 20 dB/s
    fall, and the peak held for a second before it falls too. */
class LevelMeter final : public juce::Component
{
public:
    LevelMeter();
    /** The largest sample since the last call (1.0 = 0 dBFS); call at kRateHz. */
    void push (float peak);
    void paint (juce::Graphics&) override;

    static constexpr int kRateHz = 30;

private:
    float levelDb = -100.0f, holdDb = -100.0f;
    int holdTicks = 0;
};

/** A flat lamp that lights when the card's op-amps go past their linear limit, and holds for about
    a second. While lit it reads the largest level reached at the card, in dBFS. */
class OverloadLamp final : public juce::Component
{
public:
    OverloadLamp();
    /** What Ssl4000EqProcessor::takeCardOverload() returned (-120 when the card stayed linear);
        call at LevelMeter::kRateHz. */
    void push (float cardDbfs);
    void paint (juce::Graphics&) override;

private:
    float heldDb = -120.0f;
    int holdTicks = 0;
};

/** Compressor gain reduction on the sibling 1176 VU face, with a numeric GR readout. */
class ReductionMeter final : public juce::Component
{
public:
    ReductionMeter();
    void push (float compressorDb);
    void paint (juce::Graphics&) override;

private:
    float angleFor (float reductionDb) const noexcept;

    float compressor = 0.0f;
};

/** The whole editor at its reference size; the window scales it. */
class EditorView final : public juce::Component
{
public:
    static constexpr int kWidth = 632, kHeight = 672;

    explicit EditorView (Ssl4000EqProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called by the editor's timer. */
    void tick();
    void showHelpFor (juce::Component*);
    /** Every hover help with its title (for the snapshot tool's text checks). */
    std::vector<std::pair<juce::String, juce::String>> allHelp() const;
    /** How many lines `text` takes in the help area at full width (it has room for three). */
    int helpLinesNeeded (const juce::String& text) const;

private:
    Ssl4000EqProcessor& processor;
    juce::SharedResourcePointer<SslFonts> fonts;

    OptionRow oversample;
    OptionRow hpfOn, lpfOn, eqOn, hfShape, lfShape;
    Knob hpfFreq, lpfFreq;
    Knob hfGain, hfFreq, hmfGain, hmfFreq, hmfQ;
    Knob lfGain, lfFreq, lmfGain, lmfFreq, lmfQ;
    Knob input, output;
    LevelMeter inputMeter, outputMeter;
    OverloadLamp overloadLamp;
    OptionRow dynOn, compFast, compMakeup, dynPosition, sidechainSource;
    Knob compThreshold, compRatio, compRelease;
    ReductionMeter reductionMeter;

    // Left to right: filters and EQ, then routing, compressor and level. Each band is a row;
    // its name sits at the left, aligned with the option rows.
    std::array<int, 4> bandCentreY {};
    juce::Rectangle<int> levelBars;
    juce::Rectangle<int> helpArea;
    juce::String helpTitle, helpText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};

/** The plug-in window: the view scaled to the window's width (0.75x to 2x, fixed aspect ratio). */
class Ssl4000EqEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit Ssl4000EqEditor (Ssl4000EqProcessor&);
    ~Ssl4000EqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    EditorView& getView() noexcept { return view; }

private:
    void timerCallback() override { view.tick(); }

    Ssl4000EqProcessor& pluginProcessor;
    SslLookAndFeel lookAndFeel;
    EditorView view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Ssl4000EqEditor)
};
