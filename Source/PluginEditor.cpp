#include "PluginEditor.h"

#include <BinaryData.h>

#include <cmath>

namespace
{
    // The 1176 / analog-recreation language: flat, dark, no borders. Pale discs are the things
    // you turn; pale type is what is selected; the quieter grey names things. The four band
    // colours are the SSL console's own cap colours, the one exception to the pale face.
    namespace Theme
    {
        const juce::Colour background { 0xff101011 }, panel { 0xff1e1d1e }, text { 0xffffffff },
                           label { 0xffa8a6a6 }, accent { 0xffe8893a }, pale { 0xffd7e3ec },
                           clipping { 0xffff5a5a }, field { 0xff3b3a3b };
        const juce::Colour bandHigh { 0xffffc7ce }, bandHighMid { 0xffc9efd5 }, bandLowMid { 0xffc9dcff }, bandLow { 0xffd8d8dc };

        // The VU face: the panel grey, a shade warmer; its hood is the plain panel grey.
        const juce::Colour face { 0xff24221e }, hood = panel;
        const juce::Colour redZone = clipping.withMultipliedSaturation (0.7f).withAlpha (0.7f);

        juce::Font make (const juce::Typeface::Ptr& typeface, float height, float tracking)
        {
            return juce::Font (juce::FontOptions (typeface).withHeight (height)).withExtraKerningFactor (tracking);
        }

        /** Labels are upper-case and tracked out; pass upper-case text. */
        juce::Font labelFont (float height = 11.0f)  { return make (juce::SharedResourcePointer<SslFonts>()->inter, height, 0.09f); }
        juce::Font titleFont (float height)          { return make (juce::SharedResourcePointer<SslFonts>()->inter, height, 0.18f); }
        juce::Font bodyFont (float height = 12.0f)   { return make (juce::SharedResourcePointer<SslFonts>()->inter, height, 0.01f); }
        juce::Font valueFont (float height = 11.0f)  { return make (juce::SharedResourcePointer<SslFonts>()->mono, height, 0.0f); }

        int widthOf (const juce::Font& font, const juce::String& content)
        {
            return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (font, content)));
        }
    }

    // Spacing grammar of an option row: sibling options closest, the label further from its
    // first option.
    constexpr int kOptionGap = 10, kLabelGap = 18, kRowHeight = 18;

    /** Every position of the layout, in logical pixels at the reference size. The window scales
        the whole view, so nothing else needs to know the scale. Groups are set apart by space. */
    namespace Layout
    {
        constexpr int margin = 24, headerHeight = 50, laneY = 50;
        constexpr int knobCell = 84, knobDisc = 48, trimDisc = 36;
        constexpr int bandLabelColumn = 32, bandPitch = 98;
        constexpr int rightX = 344, rightWidth = 252, rowPitch = 20;
        constexpr int helpHeight = 54, helpBottom = 10;
    }

    const juce::Identifier helpId ("help"), helpTitleId ("helpTitle");

    constexpr auto kTitle = "CLASSIC CHANNEL STRIP";
    constexpr auto kDefaultHelpTitle = "CLASSIC CHANNEL STRIP";
    constexpr auto kDefaultHelp = "A schematic-based SSL 4000 E channel strip: the 82E242 filters and four-band EQ with the 82E10 "
                                  "compressor/limiter, 0 dBFS = +18 dBu. Hover over a control to see what it does; "
                                  "double-click a knob for its default, or its value to type one.";

    /** Help text as shown: "+-18" as "±18", and a number kept on the line of its unit
        ("0 dBFS", "+18 dBu", "-3 dB", "6 dB/oct"). */
    juce::String helpForDisplay (const juce::String& text)
    {
        auto t = text.replace ("+-", juce::String::charToString (0x00b1));
        const auto nbsp = juce::String::charToString (0x00a0);
        for (const char* unit : { " dBFS", " dBu", " dB", " Hz", " kHz", " V" })
            for (int i = t.indexOf (unit); i > 0; i = t.indexOf (i + 1, unit))
                if (juce::CharacterFunctions::isDigit (t[i - 1]))
                    t = t.replaceSection (i, 1, nbsp);
        return t;
    }

    void setHelp (juce::Component& c, const juce::String& title, const juce::String& help)
    {
        c.getProperties().set (helpTitleId, title.toUpperCase());
        c.getProperties().set (helpId, helpForDisplay (help));
    }

    /** The card's controls: names, ranges and help (built once, not on the audio thread). */
    const ar::ParamSpec& spec (const char* id)
    {
        static const auto specs = ar::fx::sslE242Params();
        for (const auto& s : specs)
            if (s.id == id)
                return s;
        jassertfalse;
        return specs.front();
    }

    juce::RangedAudioParameter& parameter (Ssl4000EqProcessor& p, const char* id)
    {
        auto* param = p.apvts.getParameter (id);
        jassert (param != nullptr);
        return *param;
    }

    juce::String dbText (float v)
    {
        if (std::abs (v) < 0.05f)
            return "0.0 dB";
        return (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
    }

    juce::String hzText (float v)
    {
        if (v < 999.5f)
            return juce::String (juce::roundToInt (v)) + " Hz";
        return juce::String (v / 1000.0f, v < 9995.0f ? 2 : 1) + " kHz";
    }

    juce::String qText (float v) { return juce::String (v, 2); }
    juce::String ratioText (float v) { return v >= 19.5f ? "LIMIT" : juce::String (v, 1) + ":1"; }
    juce::String secondsText (float v) { return juce::String (v, 2) + " s"; }

    juce::StringArray choicesOf (const char* id)
    {
        juce::StringArray options;
        for (const auto& c : spec (id).choices)
            options.add (c);
        return options;
    }
}

//==============================================================================
SslFonts::SslFonts()
    : inter (juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize)),
      mono (juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, BinaryData::JetBrainsMonoMedium_ttfSize))
{
}

//==============================================================================
void SslLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                       float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const float angle = startAngle + position * (endAngle - startAngle);
    const float dot = juce::jmax (2.5f, radius * 0.11f);
    const auto face = slider.isColourSpecified (juce::Slider::thumbColourId) ? slider.findColour (juce::Slider::thumbColourId) : Theme::pale;

    g.setColour (slider.isMouseOverOrDragging() ? face.brighter (0.1f) : face);
    g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

    g.setColour (Theme::background);
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (centre.getPointOnCircumference (radius * 0.72f, angle)));
}

void SslLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (Theme::label.withAlpha (isMouseOver || isMouseDragging ? 0.8f : 0.3f));
    const float size = (float) juce::jmin (w, h);
    for (float i = 0.3f; i < 1.0f; i += 0.3f)
        g.drawLine ((float) w - size * i, (float) h - 2.0f, (float) w - 2.0f, (float) h - size * i, 1.0f);
}

//==============================================================================
Knob::Knob (juce::RangedAudioParameter& p, const juce::String& nameToUse, Formatter formatter, int diameterToUse)
    : param (p), name (nameToUse.toUpperCase()), format (std::move (formatter)), attachment (p, slider), diameter (diameterToUse)
{
    const float pi = juce::MathConstants<float>::pi;
    slider.setRotaryParameters (1.25f * pi, 2.75f * pi, true);
    // Shift while dragging: fine adjustment.
    slider.setVelocityModeParameters (0.15, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.setDoubleClickReturnValue (true, param.convertFrom0to1 (param.getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);

    editor.setJustification (juce::Justification::centred);
    editor.setFont (Theme::valueFont());
    editor.setIndents (0, 0);
    editor.setColour (juce::TextEditor::backgroundColourId, Theme::field);
    editor.setColour (juce::TextEditor::textColourId, Theme::pale);
    editor.setColour (juce::TextEditor::highlightColourId, Theme::accent.withAlpha (0.5f));
    editor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor.onReturnKey = [this] { finishEditing (true); };
    editor.onEscapeKey = [this] { finishEditing (false); };
    editor.onFocusLost = [this] { finishEditing (true); };
    addChildComponent (editor);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (kLabelHeight + kGap - 1);
    slider.setBounds (area.removeFromTop (diameter + 2).withSizeKeepingCentre (diameter + 2, diameter + 2));
    editor.setBounds (getLocalBounds().removeFromBottom (kLabelHeight).withSizeKeepingCentre (72, kLabelHeight));
}

void Knob::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour (Theme::label);
    g.setFont (Theme::labelFont());
    g.drawText (name, area.removeFromTop (kLabelHeight), juce::Justification::centred, false);
    if (! editor.isVisible())
    {
        g.setFont (Theme::valueFont());
        g.drawText (format ((float) slider.getValue()), area.removeFromBottom (kLabelHeight), juce::Justification::centred, false);
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y < getHeight() - kLabelHeight - 2)
        return;
    editor.setText (format ((float) slider.getValue()), false);
    editor.setVisible (true);
    editor.selectAll();
    editor.grabKeyboardFocus();
    repaint();
}

void Knob::finishEditing (bool apply)
{
    if (! editor.isVisible())
        return;
    editor.setVisible (false);
    if (apply)
        if (const auto v = parse (editor.getText()))
            slider.setValue (*v, juce::sendNotificationSync);   // the attachment makes it one gesture
    repaint();
}

std::optional<double> Knob::parseNumber (const juce::String& text)
{
    const auto t = text.trim().toLowerCase();
    int i = 0;
    while (i < t.length() && ! (juce::CharacterFunctions::isDigit (t[i]) || t[i] == '.' || ((t[i] == '-' || t[i] == '+') && i + 1 < t.length())))
        ++i;
    const auto rest = t.substring (i);
    if (rest.isEmpty() || ! rest.containsAnyOf ("0123456789"))
        return std::nullopt;
    const auto number = rest.initialSectionContainingOnly ("+-0123456789.");
    double v = number.getDoubleValue();
    if (rest.substring (number.length()).trimStart().startsWithChar ('k'))
        v *= 1000.0;   // "4.9k", "4.9 kHz"
    return v;
}

std::optional<double> Knob::parse (const juce::String& text) const
{
    auto v = parseNumber (text);
    if (! v)
        return std::nullopt;
    const auto range = param.getNormalisableRange();
    // A frequency typed without its "k" where only kHz makes sense: "8" on HF is 8 kHz.
    if (param.getLabel() == "Hz" && *v < range.start && *v * 1000.0 >= range.start && *v * 1000.0 <= range.end
        && ! text.containsIgnoreCase ("hz"))
        *v *= 1000.0;
    return juce::jlimit ((double) range.start, (double) range.end, *v);
}

//==============================================================================
OptionRow::OptionRow (juce::RangedAudioParameter& param, const juce::String& labelToUse, juce::StringArray optionsToUse)
    : label (labelToUse.toUpperCase()), options (std::move (optionsToUse)),
      attachment (param, [this] (float v) { selected = juce::roundToInt (v); repaint(); })
{
    labelColumn = label.isEmpty() ? 0 : labelTextWidth() + kLabelGap;
    attachment.sendInitialUpdate();
}

int OptionRow::labelTextWidth() const
{
    return label.isEmpty() ? 0 : Theme::widthOf (Theme::labelFont(), label);
}

std::vector<juce::Range<int>> OptionRow::optionRanges() const
{
    std::vector<juce::Range<int>> ranges;
    const auto font = Theme::labelFont();
    int x = labelColumn;

    for (const auto& option : options)
    {
        const int width = Theme::widthOf (font, option.toUpperCase());
        ranges.push_back ({ x, x + width });
        x += width + kOptionGap;
    }
    return ranges;
}

int OptionRow::preferredWidth() const
{
    const auto ranges = optionRanges();
    return ranges.empty() ? labelColumn : ranges.back().getEnd();
}

int OptionRow::optionAt (int x) const
{
    const auto ranges = optionRanges();
    for (int i = 0; i < (int) ranges.size(); ++i)
        if (ranges[(size_t) i].expanded (kOptionGap / 2).contains (x))
            return i;
    return -1;
}

void OptionRow::paint (juce::Graphics& g)
{
    g.setFont (Theme::labelFont());
    g.setColour (Theme::label);
    if (labelColumn > 0)
        g.drawText (label, 0, 0, labelColumn, getHeight(), juce::Justification::centredLeft, false);

    const auto ranges = optionRanges();
    for (int i = 0; i < options.size(); ++i)
    {
        const auto range = ranges[(size_t) i];
        g.setColour (i == selected ? Theme::pale : (i == hovered ? Theme::text : Theme::label.withAlpha (0.7f)));
        g.drawText (options[i].toUpperCase(), range.getStart(), 0, range.getLength() + 4, getHeight(),
                    juce::Justification::centredLeft, false);
    }
}

void OptionRow::mouseDown (const juce::MouseEvent& e)
{
    if (const int option = optionAt (e.x); option >= 0)
        attachment.setValueAsCompleteGesture ((float) option);
}

void OptionRow::mouseMove (const juce::MouseEvent& e)
{
    const int option = optionAt (e.x);
    if (option != hovered)
    {
        hovered = option;
        repaint();
    }
}

void OptionRow::mouseExit (const juce::MouseEvent&)     { hovered = -1; repaint(); }

//==============================================================================
LevelMeter::LevelMeter() = default;

void LevelMeter::push (float peak)
{
    constexpr float fallPerTick = 20.0f / kRateHz;   // 20 dB/s
    constexpr int holdFor = kRateHz;                  // 1 s
    const float db = peak > 0.0f ? juce::Decibels::gainToDecibels (peak, -100.0f) : -100.0f;
    const float level = juce::jmax (db, levelDb - fallPerTick);
    float hold = holdDb;
    if (db >= holdDb)
    {
        hold = db;
        holdTicks = holdFor;
    }
    else if (holdTicks > 0)
        --holdTicks;
    else
        hold = juce::jmax (level, holdDb - fallPerTick);

    if (std::abs (level - levelDb) > 0.05f || std::abs (hold - holdDb) > 0.05f)
    {
        levelDb = level;
        holdDb = hold;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    // A thin flat bar in the middle of the component; the component itself is the hover target.
    constexpr float lo = -40.0f, hi = 6.0f, thickness = 6.0f;
    const auto r = getLocalBounds().toFloat();
    const auto bar = r.withSizeKeepingCentre (r.getWidth(), thickness);
    const auto xFor = [&bar, low = lo, high = hi] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db - low) / (high - low)); };

    g.setColour (Theme::label.withAlpha (0.25f));
    g.fillRect (bar);
    if (levelDb > lo)
    {
        g.setColour (Theme::accent);
        g.fillRect (bar.withRight (xFor (levelDb)));
    }
    // 0 dBFS, and the held peak (red at or above it).
    g.setColour (Theme::label);
    g.fillRect (xFor (0.0f) - 0.5f, bar.getY() - 2.0f, 1.0f, thickness + 4.0f);
    if (holdDb > lo)
    {
        g.setColour (holdDb >= 0.0f ? Theme::clipping : Theme::accent);
        g.fillRect (xFor (holdDb) - 1.0f, bar.getY(), 2.0f, thickness);
    }
}

//==============================================================================
OverloadLamp::OverloadLamp() = default;

void OverloadLamp::push (float cardDbfs)
{
    constexpr int holdFor = LevelMeter::kRateHz;   // 1 s
    const bool wasLit = holdTicks > 0;
    const float before = heldDb;
    if (cardDbfs > -119.0f)
    {
        heldDb = wasLit ? juce::jmax (heldDb, cardDbfs) : cardDbfs;
        holdTicks = holdFor;
    }
    else if (holdTicks > 0)
        --holdTicks;

    if (wasLit != (holdTicks > 0) || std::abs (heldDb - before) > 0.05f)
        repaint();
}

void OverloadLamp::paint (juce::Graphics& g)
{
    // A flat segment on the bars' row; lit in the clipping red, with the peak it reached at the card.
    constexpr float width = 14.0f, thickness = 6.0f;
    const bool lit = holdTicks > 0;
    const auto r = getLocalBounds().toFloat();
    const auto lamp = r.withSizeKeepingCentre (width, thickness).withX (r.getX());
    g.setColour (lit ? Theme::clipping : Theme::label.withAlpha (0.25f));
    g.fillRect (lamp);
    if (lit)
    {
        g.setColour (Theme::clipping);
        g.setFont (Theme::valueFont());
        g.drawText (dbText (heldDb).replace (" dB", " dBFS"), r.withTrimmedLeft (width + 8.0f).toNearestInt(), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
ReductionMeter::ReductionMeter() = default;

void ReductionMeter::push (float compressorDb)
{
    const auto follow = [] (float next, float shown)
    {
        return next > shown ? next : juce::jmax (0.0f, shown - 24.0f / LevelMeter::kRateHz);
    };
    const float nextComp = follow (compressorDb, compressor);
    if (std::abs (nextComp - compressor) > 0.05f)
    {
        compressor = nextComp;
        repaint();
    }
}

void ReductionMeter::paint (juce::Graphics& g)
{
    // The same face geometry as the sibling 1176 VU: arc 25 px below the top, pivot hidden
    // beneath the 19 px hood, and a nonlinear VU scale. This meter always reads GR.
    constexpr float halfSweep = 0.85f, arcTop = 25.0f, hoodHeight = 19.0f;
    const auto face = getLocalBounds().toFloat();
    const auto hood = face.withTop (face.getBottom() - hoodHeight);
    const float radius = face.getWidth() * 0.42f / std::sin (halfSweep);
    const juce::Point<float> pivot { face.getCentreX(), face.getY() + arcTop + radius };

    g.setColour (Theme::face);
    g.fillRoundedRectangle (face, 4.0f);
    juce::Path hoodShape;
    hoodShape.addRoundedRectangle (hood.getX(), hood.getY(), hood.getWidth(), hood.getHeight(),
                                   4.0f, 4.0f, false, false, true, true);
    g.setColour (Theme::hood);
    g.fillPath (hoodShape);

    juce::Path line, red;
    line.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (-20.0f), angleFor (0.0f), true);
    red.addCentredArc (pivot.x, pivot.y, radius + 1.0f, radius + 1.0f, 0.0f, angleFor (0.0f), angleFor (3.0f), true);
    g.setColour (Theme::label.withAlpha (0.7f));
    g.strokePath (line, juce::PathStrokeType (1.0f));
    g.setColour (Theme::redZone);
    g.strokePath (red, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    const float marks[] { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
    g.setFont (Theme::valueFont (8.0f));
    for (const float mark : marks)
    {
        const float angle = angleFor (mark);
        g.setColour (mark > 0.0f ? Theme::redZone : Theme::label);
        g.drawLine ({ pivot.getPointOnCircumference (radius, angle),
                      pivot.getPointOnCircumference (radius + 5.0f, angle) }, mark == 0.0f ? 1.5f : 1.0f);
        const auto at = pivot.getPointOnCircumference (radius + 11.0f, angle);
        const auto label = mark < 0.0f ? juce::String::fromUTF8 ("\xE2\x88\x92") + juce::String ((int) -mark)
                                       : (mark > 0.0f ? "+" : "") + juce::String ((int) mark);
        g.drawText (label, juce::Rectangle<float> (22.0f, 10.0f).withCentre (at),
                    juce::Justification::centred, false);
    }

    g.setColour (Theme::label.withAlpha (0.5f));
    g.setFont (Theme::titleFont (10.0f));
    g.drawText ("VU", juce::Rectangle<float> (face.getX(), pivot.y - radius + 35.0f, face.getWidth(), 12.0f),
                juce::Justification::centred, false);

    auto readout = hood.reduced (8.0f, 0.0f);
    g.setColour (Theme::label);
    g.setFont (Theme::labelFont (8.0f));
    g.drawText ("GR", readout.removeFromLeft (18.0f), juce::Justification::centredLeft, false);
    g.setFont (Theme::valueFont (9.0f));
    g.drawText ((compressor >= 0.05f ? juce::String::charToString (0x2212) : juce::String())
                    + juce::String (compressor, 1) + " dB", readout,
                juce::Justification::centredLeft, false);

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (face.withBottom (hood.getY()).toNearestInt());
    g.setColour (Theme::pale);
    g.drawLine ({ pivot, pivot.getPointOnCircumference (radius + 2.0f, angleFor (-compressor)) }, 1.25f);
}

float ReductionMeter::angleFor (float meterDb) const noexcept
{
    constexpr float halfSweep = 0.85f;
    const auto deflection = [] (float db) { return std::pow (10.0f, db / 20.0f); };
    const float lo = deflection (-20.0f), hi = deflection (3.0f);
    const float clamped = juce::jlimit (-20.0f, 3.0f, meterDb);
    return halfSweep * (2.0f * (deflection (clamped) - lo) / (hi - lo) - 1.0f);
}

//==============================================================================
EditorView::EditorView (Ssl4000EqProcessor& p)
    : processor (p),
      oversample (parameter (p, ParamID::oversample), "Oversample", { "1x", "2x" }),
      hpfOn (parameter (p, ParamID::hpfOn), "HPF", choicesOf (ParamID::hpfOn)),
      lpfOn (parameter (p, ParamID::lpfOn), "LPF", choicesOf (ParamID::lpfOn)),
      eqOn (parameter (p, ParamID::eqOn), "EQ", choicesOf (ParamID::eqOn)),
      hfShape (parameter (p, ParamID::hfBell), "", choicesOf (ParamID::hfBell)),
      lfShape (parameter (p, ParamID::lfBell), "", choicesOf (ParamID::lfBell)),
      hpfFreq (parameter (p, ParamID::hpfFreq), "Freq", hzText),
      lpfFreq (parameter (p, ParamID::lpfFreq), "Freq", hzText),
      hfGain (parameter (p, ParamID::hfGain), "Gain", dbText),
      hfFreq (parameter (p, ParamID::hfFreq), "Frequency", hzText),
      hmfGain (parameter (p, ParamID::hmfGain), "Gain", dbText),
      hmfFreq (parameter (p, ParamID::hmfFreq), "Frequency", hzText),
      hmfQ (parameter (p, ParamID::hmfQ), "Q", qText),
      lfGain (parameter (p, ParamID::lfGain), "Gain", dbText),
      lfFreq (parameter (p, ParamID::lfFreq), "Frequency", hzText),
      lmfGain (parameter (p, ParamID::lmfGain), "Gain", dbText),
      lmfFreq (parameter (p, ParamID::lmfFreq), "Frequency", hzText),
      lmfQ (parameter (p, ParamID::lmfQ), "Q", qText),
      input (parameter (p, ParamID::input), "Input", dbText, Layout::trimDisc),
      output (parameter (p, ParamID::output), "Output", dbText, Layout::trimDisc),
      dynOn (parameter (p, ParamID::dynOn), "Compressor", { "Out", "In" }),
      compFast (parameter (p, ParamID::compFast), "Attack", { "Auto", "Fast" }),
      compMakeup (parameter (p, ParamID::compMakeup), "Make-up", { "Off", "Auto" }),
      dynPosition (parameter (p, ParamID::dynPosition), "Order", { "Pre EQ", "Post EQ" }),
      sidechainSource (parameter (p, ParamID::sidechainSource), "SC", { "Input", "Filters", "EQ", "External" }),
      compThreshold (parameter (p, ParamID::compThreshold), "Threshold", dbText),
      compRatio (parameter (p, ParamID::compRatio), "Ratio", ratioText),
      compRelease (parameter (p, ParamID::compRelease), "Release", secondsText)
{
    for (auto* c : std::initializer_list<juce::Component*> { &oversample, &hpfOn, &lpfOn, &eqOn, &hfShape, &lfShape,
                                                             &hpfFreq, &lpfFreq, &hfGain, &hfFreq, &hmfGain, &hmfFreq, &hmfQ,
                                                             &lfGain, &lfFreq, &lmfGain, &lmfFreq, &lmfQ, &input, &output,
                                                             &inputMeter, &outputMeter, &overloadLamp, &dynOn, &compFast, &compMakeup,
                                                             &dynPosition, &sidechainSource, &compThreshold, &compRatio, &compRelease,
                                                             &reductionMeter })
        addAndMakeVisible (c);

    // The card's own help for its controls, titled with their names.
    const std::pair<juce::Component*, const char*> cardControls[] {
        { &hpfOn, ParamID::hpfOn }, { &hpfFreq, ParamID::hpfFreq }, { &lpfOn, ParamID::lpfOn }, { &lpfFreq, ParamID::lpfFreq },
        { &hfGain, ParamID::hfGain }, { &hfFreq, ParamID::hfFreq }, { &hfShape, ParamID::hfBell },
        { &hmfGain, ParamID::hmfGain }, { &hmfFreq, ParamID::hmfFreq }, { &hmfQ, ParamID::hmfQ },
        { &lmfGain, ParamID::lmfGain }, { &lmfFreq, ParamID::lmfFreq }, { &lmfQ, ParamID::lmfQ },
        { &lfGain, ParamID::lfGain }, { &lfFreq, ParamID::lfFreq }, { &lfShape, ParamID::lfBell }, { &eqOn, ParamID::eqOn } };
    for (const auto& [component, id] : cardControls)
        setHelp (*component, spec (id).name, spec (id).help);
    setHelp (input, "Input", "Gain before the card. 0 dBFS = +18 dBu at the card; its op-amps stay clean to about +4.6 dBFS and "
                             "saturate softly above, so turning this up drives the card.");
    setHelp (output, "Output", "Gain after the card, in dB. The two bars bracket the pair: the input is metered before the "
                               "Input trim, the output after the Output trim.");
    setHelp (inputMeter, "Input level", "The channel input in dBFS, before the Input trim: a flat bar from -40 to +6 dBFS with 0 dBFS "
                                        "marked. It rises at once and falls at about 20 dB/s; the held peak turns red at or above 0 dBFS.");
    setHelp (outputMeter, "Output level", "The channel output in dBFS, after the Output trim: a flat bar from -40 to +6 dBFS with "
                                          "0 dBFS marked. It rises at once and falls at about 20 dB/s; the held peak turns red at or above 0 dBFS.");
    setHelp (overloadLamp, "Card overload", "Lights when an op-amp in the card goes past its linear limit (+-14.85 V, which is +4.6 dBFS at the card "
                                            "with the EQ flat; boosts lower it) and holds for about a second, reading the peak it reached. "
                                            "Turn Input down to stay clean.");
    setHelp (reductionMeter, "Gain reduction", "The compressor's gain reduction on a VU face: the needle rests at 0 and falls to the left, "
                                               "and the hood reads the peak reduction in dB. It stays at 0 while the compressor is out.");
    setHelp (oversample, "Oversample", "The card at the host rate (1X) or twice it (2X). 2X adds 4 samples of latency and brings the bells and "
                                       "the LPF near 20 kHz within about a dB of the hardware.");
    setHelp (dynOn, "Compressor", "Switches the stereo-linked 82E10 compressor/limiter in or out. OUT passes the audio through "
                                  "bit-for-bit; the gate is permanently bypassed.");
    setHelp (compThreshold, "Comp threshold", "The console's +10 to -20 dB threshold scale. Lower settings compress more and bring "
                                              "more automatic make-up gain.");
    setHelp (compRatio, "Comp ratio", "Continuous from 1:1 to hard limiting (LIMIT); the D14/D1 diode network produces the soft knee.");
    setHelp (compRelease, "Comp release", "0.1 to 4 seconds. The VCA recovers on the 82E10-style logarithmic release curve: short "
                                          "for rhythmic material, long for smooth levelling.");
    setHelp (compFast, "Comp attack", "AUTO is the program-dependent path (R21 10 k with C5 3.3 uF); FAST switches to R19 300 ohm, "
                                      "about 3 ms to settle.");
    setHelp (compMakeup, "Make-up", "AUTO applies the threshold- and ratio-linked make-up gain of the console control; OFF leaves the "
                                    "level as compressed, for level-matched comparison.");
    setHelp (dynPosition, "Dynamics order", "PRE EQ places the compressor before the filters and EQ (the default signal order); "
                                            "POST EQ places it after them.");
    setHelp (sidechainSource, "Sidechain", "What keys the compressor: the channel INPUT, a key shaped by the actual FILTERS or EQ "
                                           "section, or the host's EXTERNAL sidechain bus.");

    // SSL cap colours: HF pink, HMF mint, LMF blue, LF grey; everything else keeps the pale face.
    for (auto* k : { &hfGain, &hfFreq })            k->setFaceColour (Theme::bandHigh);
    for (auto* k : { &hmfGain, &hmfFreq, &hmfQ })   k->setFaceColour (Theme::bandHighMid);
    for (auto* k : { &lmfGain, &lmfFreq, &lmfQ })   k->setFaceColour (Theme::bandLowMid);
    for (auto* k : { &lfGain, &lfFreq })            k->setFaceColour (Theme::bandLow);

    // Stacked rows share one label column, so their options start together.
    const auto share = [] (std::initializer_list<OptionRow*> rows)
    {
        int column = 0;
        for (auto* row : rows)
            column = juce::jmax (column, row->labelTextWidth());
        for (auto* row : rows)
            row->setLabelColumn (column + kLabelGap);
    };
    share ({ &dynPosition, &sidechainSource });
    share ({ &dynOn, &compFast, &compMakeup });

    helpTitle = kDefaultHelpTitle;
    helpText = helpForDisplay (kDefaultHelp);
    setSize (kWidth, kHeight);
}

void EditorView::resized()
{
    using namespace Layout;

    // Lane 1, under the title: the two filter switches at the left, Oversample at the right end.
    const int cell0 = margin + bandLabelColumn;   // first knob cell of the EQ
    hpfOn.setBounds (margin, laneY, hpfOn.preferredWidth(), kRowHeight);
    lpfOn.setBounds (cell0 + knobCell, laneY, lpfOn.preferredWidth(), kRowHeight);
    oversample.setBounds (kWidth - margin - oversample.preferredWidth(), laneY, oversample.preferredWidth(), kRowHeight);

    // Filters: one knob each.
    const int filterY = laneY + kRowHeight + 4;
    hpfFreq.setBounds (cell0, filterY, knobCell, hpfFreq.preferredHeight());
    lpfFreq.setBounds (cell0 + knobCell, filterY, knobCell, lpfFreq.preferredHeight());

    // The EQ switch above its four bands, which are one row each: gain, frequency, then Q (or
    // the shelf/bell choice, in the cell Q would have).
    const int eqY = filterY + hpfFreq.preferredHeight() + 10;
    eqOn.setBounds (margin, eqY, eqOn.preferredWidth(), kRowHeight);

    const int bandsY = eqY + kRowHeight + 4;
    const auto placeBand = [&] (int i, Knob& gain, Knob& freq, Knob* q, OptionRow* shape)
    {
        const int y = bandsY + i * bandPitch;
        bandCentreY[(size_t) i] = y + Knob::kLabelHeight + Knob::kGap + knobDisc / 2;
        gain.setBounds (cell0, y, knobCell, gain.preferredHeight());
        freq.setBounds (cell0 + knobCell, y, knobCell, freq.preferredHeight());
        if (q != nullptr)
            q->setBounds (cell0 + 2 * knobCell, y, knobCell, q->preferredHeight());
        if (shape != nullptr)
            shape->setBounds (cell0 + 2 * knobCell + 4, bandCentreY[(size_t) i] - kRowHeight / 2,
                              shape->preferredWidth(), kRowHeight);
    };
    placeBand (0, hfGain, hfFreq, nullptr, &hfShape);
    placeBand (1, hmfGain, hmfFreq, &hmfQ, nullptr);
    placeBand (2, lmfGain, lmfFreq, &lmfQ, nullptr);
    placeBand (3, lfGain, lfFreq, nullptr, &lfShape);
    const int bandsBottom = bandsY + 3 * bandPitch + hfGain.preferredHeight();

    // Right: routing, then the compressor, its meter and the level group.
    int y = laneY + kRowHeight + 4;
    dynPosition.setBounds (rightX, y, dynPosition.preferredWidth(), kRowHeight);
    sidechainSource.setBounds (rightX, y + rowPitch, sidechainSource.preferredWidth(), kRowHeight);

    y += 2 * rowPitch + 24;
    dynOn.setBounds (rightX, y, dynOn.preferredWidth(), kRowHeight);
    compFast.setBounds (rightX, y + rowPitch, compFast.preferredWidth(), kRowHeight);
    compMakeup.setBounds (rightX, y + 2 * rowPitch, compMakeup.preferredWidth(), kRowHeight);

    y += 2 * rowPitch + kRowHeight + 12;
    for (const auto& [knob, column] : std::initializer_list<std::pair<Knob*, int>> { { &compThreshold, 0 }, { &compRatio, 1 }, { &compRelease, 2 } })
        knob->setBounds (rightX + column * knobCell, y, knobCell, knob->preferredHeight());

    y += compThreshold.preferredHeight() + 16;
    reductionMeter.setBounds (rightX, y, rightWidth, 112);

    // Level: input and output bars, and the two trims beneath, ending level with the last band.
    const int trimY = bandsBottom - input.preferredHeight();
    // The three rows (IN, CARD, OUT) start where the two bars started, so the gap under the VU panel is
    // unchanged; the extra row is absorbed by a 17 px pitch of 16 px rows.
    constexpr int meterPitch = 17, meterHeight = 16;
    const int barsY = trimY - 14 - 2 * rowPitch;
    levelBars = { rightX, barsY, rightWidth, 2 * meterPitch + meterHeight };
    const int barColumn = Theme::widthOf (Theme::labelFont(), "CARD") + kLabelGap;
    inputMeter.setBounds (rightX + barColumn, barsY, rightWidth - barColumn, meterHeight);
    overloadLamp.setBounds (rightX + barColumn, barsY + meterPitch, rightWidth - barColumn, meterHeight);
    outputMeter.setBounds (rightX + barColumn, barsY + 2 * meterPitch, rightWidth - barColumn, meterHeight);
    input.setBounds (rightX, trimY, knobCell, input.preferredHeight());
    output.setBounds (rightX + knobCell, trimY, knobCell, output.preferredHeight());

    helpArea = { margin, kHeight - helpBottom - helpHeight, kWidth - 2 * margin, helpHeight };
}

void EditorView::paint (juce::Graphics& g)
{
    using namespace Layout;
    g.fillAll (Theme::background);

    // Title: the plug-in, and quieter, the cards behind it.
    const auto title = Theme::titleFont (15.0f), subtitle = Theme::titleFont (11.0f);
    const int baseline = 33;
    g.setColour (Theme::pale);
    g.setFont (title);
    g.drawSingleLineText (kTitle, margin, baseline);
    g.setColour (Theme::label);
    g.setFont (subtitle);
    g.drawSingleLineText (juce::String::fromUTF8 ("82E242 EQ  \xC2\xB7  82E10 DYNAMICS"),
                          margin + Theme::widthOf (title, kTitle) + 12, baseline);

    // Band names, at the left of each band's row.
    g.setColour (Theme::label);
    g.setFont (Theme::labelFont());
    const char* bandNames[] { "HF", "HMF", "LMF", "LF" };
    for (size_t i = 0; i < bandCentreY.size(); ++i)
        g.drawText (bandNames[i], margin, bandCentreY[i] - 8, bandLabelColumn, 16, juce::Justification::centredLeft, false);

    // The level rows' names: the input bar, the card's overload lamp, the output bar.
    g.drawText ("IN", levelBars.getX(), inputMeter.getY(), levelBars.getWidth(), inputMeter.getHeight(), juce::Justification::centredLeft, false);
    g.drawText ("CARD", levelBars.getX(), overloadLamp.getY(), levelBars.getWidth(), overloadLamp.getHeight(), juce::Justification::centredLeft, false);
    g.drawText ("OUT", levelBars.getX(), outputMeter.getY(), levelBars.getWidth(), outputMeter.getHeight(), juce::Justification::centredLeft, false);

    // Help: what the control under the mouse does.
    auto help = helpArea;
    g.setColour (Theme::pale);
    g.setFont (Theme::labelFont());
    g.drawText (helpTitle, help.removeFromTop (16), juce::Justification::topLeft, false);
    g.setColour (Theme::label);
    g.setFont (Theme::bodyFont());
    g.drawFittedText (helpText, help, juce::Justification::topLeft, 3, 1.0f);
}

void EditorView::showHelpFor (juce::Component* component)
{
    juce::String title = kDefaultHelpTitle, text = helpForDisplay (kDefaultHelp);
    // The help belongs to the nearest ancestor that has any (a knob's slider has none itself).
    for (auto* c = component; c != nullptr && c != this; c = c->getParentComponent())
        if (c->getProperties().contains (helpId))
        {
            title = c->getProperties()[helpTitleId].toString();
            text = c->getProperties()[helpId].toString();
            break;
        }
    if (title != helpTitle || text != helpText)
    {
        helpTitle = title;
        helpText = text;
        repaint (helpArea);
    }
}

std::vector<std::pair<juce::String, juce::String>> EditorView::allHelp() const
{
    std::vector<std::pair<juce::String, juce::String>> all { { kDefaultHelpTitle, helpForDisplay (kDefaultHelp) } };
    for (auto* c : getChildren())
        if (c->getProperties().contains (helpId))
            all.emplace_back (c->getProperties()[helpTitleId].toString(), c->getProperties()[helpId].toString());
    return all;
}

int EditorView::helpLinesNeeded (const juce::String& text) const
{
    const auto font = Theme::bodyFont();
    juce::GlyphArrangement glyphs;
    glyphs.addJustifiedText (font, text, 0.0f, 0.0f, (float) helpArea.getWidth(), juce::Justification::left);
    int lines = 0;
    float lastY = -1.0e9f;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i)
        if (const float y = glyphs.getGlyph (i).getBaselineY(); y > lastY + 1.0f)
        {
            ++lines;
            lastY = y;
        }
    return lines;
}

void EditorView::tick()
{
    inputMeter.push (juce::jmax (processor.takeInputPeak (0), processor.takeInputPeak (1)));
    outputMeter.push (juce::jmax (processor.takeOutputPeak (0), processor.takeOutputPeak (1)));
    overloadLamp.push (processor.takeCardOverload());
    reductionMeter.push (processor.takeCompressorReduction());

    auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    showHelpFor (under != nullptr && isParentOf (under) ? under : nullptr);
}

//==============================================================================
Ssl4000EqEditor::Ssl4000EqEditor (Ssl4000EqProcessor& p)
    : AudioProcessorEditor (p), pluginProcessor (p), view (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (view);

    // Drag the corner (or the host's window edge) to scale the whole window; the aspect ratio is kept.
    constexpr int w = EditorView::kWidth, h = EditorView::kHeight;
    setResizable (true, true);
    setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
    getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
    const double scale = juce::jlimit (0.75, 2.0, (double) p.apvts.state.getProperty ("editorScale", 1.0));
    setSize (juce::roundToInt (w * scale), juce::roundToInt (h * scale));
    startTimerHz (LevelMeter::kRateHz);
}

Ssl4000EqEditor::~Ssl4000EqEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void Ssl4000EqEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
}

void Ssl4000EqEditor::resized()
{
    const float scale = (float) getWidth() / (float) EditorView::kWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, EditorView::kWidth, EditorView::kHeight);
    // Remember a size the user chose (a window on screen), not the ones a host passes through
    // while opening or closing it.
    if (isShowing() && getPeer() != nullptr)
        pluginProcessor.apvts.state.setProperty ("editorScale", (double) scale, nullptr);
}
