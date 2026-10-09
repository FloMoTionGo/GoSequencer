#include "PluginEditor.h"
#include "ValueText.h"

#include <cmath>
#include <numeric>

//==============================================================================
namespace
{
    constexpr int captionHeight = 14, controlHeight = 24, valueHeight = 16;
    constexpr int knobWidth = 84, knobHeight = 76;          //  caption, dial, value
    constexpr int ledHeight = 22, segmentHeight = 26;
    constexpr int pillGap = 6, stepButtonWidth = 30;
    constexpr int sectionPadX = 14, sectionPadTop = 18, sectionPadBottom = 12, sectionGap = 14;

    //  the open face rides along in the state, so a session comes back on it
    const juce::Identifier panelFaceProperty { "panelFace" };

    //  so does the light/dark choice
    const juce::Identifier darkModeProperty { "darkMode" };

    //  how a button is drawn: an LED switch, rather than an outlined pill
    const juce::Identifier ledProperty { "goLed" };

    //  how a knob is drawn: from the middle (spread), or with a tick per choice
    const juce::Identifier bipolarProperty { "goBipolar" };
    const juce::Identifier detentProperty  { "goDetents" };

    juce::Font captionFont() { return theme::font (10.5f, juce::Font::plain, 0.08f); }
    juce::Font valueFont()   { return theme::font (12.5f); }
    juce::Font pillFont()    { return theme::font (11.0f, juce::Font::plain, 0.05f); }
    juce::Font ledFont()     { return theme::font (13.0f); }
    juce::Font titleFont()   { return theme::font (17.0f, juce::Font::bold, 0.16f); }
    juce::Font sectionFont() { return theme::font (10.5f, juce::Font::bold, 0.18f); }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    //  outlined buttons are as wide as what they say, not as wide as the space
    int pillWidth (const juce::Button& button)
    {
        return textWidth (pillFont(), button.getButtonText().toUpperCase()) + 24;
    }

    int ledWidth (const juce::Button& button)
    {
        return textWidth (ledFont(), button.getButtonText()) + 22;
    }

    /** Buttons side by side from the left, each at its own width. */
    void flow (juce::Rectangle<int> row, std::initializer_list<juce::Button*> buttons)
    {
        for (auto* button : buttons)
        {
            button->setBounds (row.removeFromLeft (pillWidth (*button)));
            row.removeFromLeft (pillGap);
        }
    }

    /** The same, packed against the right hand edge. */
    void flowRight (juce::Rectangle<int> row, std::initializer_list<juce::Button*> buttons)
    {
        std::vector<juce::Button*> list (buttons);

        for (auto it = list.rbegin(); it != list.rend(); ++it)
        {
            (*it)->setBounds (row.removeFromRight (pillWidth (**it)));
            row.removeFromRight (pillGap);
        }
    }

    /** A knob in a cell: its caption over it, its value under it. */
    void placeKnob (juce::Rectangle<int> cell, juce::Label& caption, juce::Slider& knob)
    {
        caption.setBounds (cell.removeFromTop (captionHeight));
        knob.setBounds (cell.removeFromTop (knobHeight - captionHeight));
    }

    juce::Rectangle<int> knobCell (juce::Rectangle<int>& row)
    {
        return row.removeFromLeft (knobWidth);
    }

    //  ---- what typed text means, per knob - see ValueText.h ----------------
    using Parse = std::function<std::optional<double> (const juce::String&)>;

    template <typename Fn>
    Parse parser (Fn fn)
    {
        return [fn] (const juce::String& text) -> std::optional<double>
        {
            if (auto v = fn (text.toStdString()))
                return (double) *v;

            return std::nullopt;
        };
    }

    Parse wholeParser (std::vector<std::string> units = {})
    {
        return parser ([units] (const std::string& t) { return valuetext::wholeNumber (t, units); });
    }

    Parse choiceParser (const juce::StringArray& names)
    {
        std::vector<std::string> list;

        for (const auto& name : names)
            list.push_back (name.toStdString());

        return parser ([list] (const std::string& t) { return valuetext::choice (t, list); });
    }

    //  ---- the four mode pictures: a square spiral, nested rings, and the four
    //  quadrants with a dot where their walk ends ----------------------------
    void drawModeIcon (juce::Graphics& g, juce::Rectangle<float> area, int mode)
    {
        const float side = juce::jmin (area.getWidth(), area.getHeight(), 22.0f);
        const auto box = juce::Rectangle<float> (side, side).withCentre (area.getCentre());
        const float u = side / 22.0f;
        const auto at = [&] (float x, float y) { return juce::Point<float> (box.getX() + x * u, box.getY() + y * u); };
        const juce::PathStrokeType stroke (1.4f);

        juce::Path path;

        if (mode == 0)
        {
            path.startNewSubPath (at (3, 3));

            for (auto p : { juce::Point<float> (19, 3), { 19, 19 }, { 7, 19 }, { 7, 7 }, { 15, 7 }, { 15, 15 }, { 11, 15 } })
                path.lineTo (at (p.x, p.y));

            g.strokePath (path, stroke);
            return;
        }

        if (mode == 1)
        {
            for (float inset : { 2.5f, 6.5f, 10.0f })
                path.addRectangle (juce::Rectangle<float> (at (inset, inset), at (22 - inset, 22 - inset)));

            g.strokePath (path, stroke);
            return;
        }

        for (auto origin : { juce::Point<float> (2.5f, 2.5f), { 12.0f, 2.5f }, { 2.5f, 12.0f }, { 12.0f, 12.0f } })
            path.addRectangle (juce::Rectangle<float> (at (origin.x, origin.y), at (origin.x + 7.5f, origin.y + 7.5f)));

        g.strokePath (path, stroke);

        //  quads out end on the outer corners, quads in on the inner ones
        const bool outward = (mode == 2);
        const float a = outward ? 4.6f : 7.9f, b = outward ? 17.4f : 14.1f;

        for (auto p : { juce::Point<float> (a, a), { b, a }, { a, b }, { b, b } })
            g.fillEllipse (juce::Rectangle<float> (2.6f * u, 2.6f * u).withCentre (at (p.x, p.y)));
    }
}

//==============================================================================
GoLookAndFeel::GoLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getLightColourScheme());
    applyColours();
}

void GoLookAndFeel::applyColours()
{
    setColour (juce::ResizableWindow::backgroundColourId,     theme::background);
    setColour (juce::Label::textColourId,                     theme::ink);
    setColour (juce::Label::backgroundWhenEditingColourId,    theme::background);
    setColour (juce::Label::textWhenEditingColourId,          theme::ink);
    setColour (juce::Label::outlineWhenEditingColourId,       theme::accent);

    setColour (juce::Slider::textBoxTextColourId,             theme::ink);
    setColour (juce::Slider::textBoxBackgroundColourId,       juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxOutlineColourId,          juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxHighlightColourId,        theme::hairline);

    setColour (juce::TextEditor::textColourId,                theme::ink);
    setColour (juce::TextEditor::backgroundColourId,          theme::background);
    setColour (juce::TextEditor::highlightColourId,           theme::hairline);
    setColour (juce::TextEditor::highlightedTextColourId,     theme::ink);
    setColour (juce::TextEditor::outlineColourId,             theme::accent);
    setColour (juce::TextEditor::focusedOutlineColourId,      theme::accent);
    setColour (juce::CaretComponent::caretColourId,           theme::ink);

    setColour (juce::ComboBox::textColourId,                  theme::ink);
    setColour (juce::ComboBox::backgroundColourId,            juce::Colours::transparentWhite);
    setColour (juce::ComboBox::outlineColourId,               theme::hairline);
    setColour (juce::ComboBox::arrowColourId,                 theme::faintText);
    setColour (juce::ComboBox::focusedOutlineColourId,        theme::ink);

    setColour (juce::PopupMenu::backgroundColourId,           theme::background);
    setColour (juce::PopupMenu::textColourId,                 theme::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::boardFill);
    setColour (juce::PopupMenu::highlightedTextColourId,      theme::ink);

    setColour (juce::TextButton::buttonColourId,              juce::Colours::transparentWhite);
    setColour (juce::TextButton::buttonOnColourId,            theme::accent);
    setColour (juce::TextButton::textColourOffId,             theme::dimText);
    setColour (juce::TextButton::textColourOnId,              theme::background);
}

void GoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto bounds = button.getLocalBounds().toFloat();
    const bool enabled = button.isEnabled();

    if (button.getProperties().contains (ledProperty))
    {
        //  an LED: a ring when off, lit in the accent when on - nothing else
        const auto lamp = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ bounds.getX() + 5.5f, bounds.getCentreY() });
        const float alpha = enabled ? 1.0f : 0.4f;

        if (button.getToggleState())
        {
            g.setColour (theme::accent.withAlpha (0.28f * alpha));
            g.fillEllipse (lamp.expanded (3.0f));
            g.setColour (theme::accent.withMultipliedAlpha (alpha));
            g.fillEllipse (lamp);
        }
        else
        {
            g.setColour ((shouldDrawButtonAsHighlighted ? theme::dimText : theme::faintText).withMultipliedAlpha (alpha));
            g.drawEllipse (lamp.reduced (0.65f), 1.3f);
        }

        return;
    }

    const auto face = bounds.reduced (0.5f);

    if (button.getToggleState())
    {
        g.setColour (theme::accent.withMultipliedAlpha (! enabled ? 0.4f : (shouldDrawButtonAsDown ? 0.85f : 1.0f)));
        g.fillRoundedRectangle (face, 4.0f);
    }
    else
    {
        if (shouldDrawButtonAsDown)
        {
            g.setColour (theme::boardFill);
            g.fillRoundedRectangle (face, 4.0f);
        }

        g.setColour (! enabled ? theme::hairline.withMultipliedAlpha (0.6f)
                               : (shouldDrawButtonAsHighlighted ? theme::faintText : theme::hairline));
        g.drawRoundedRectangle (face, 4.0f, 1.0f);
    }

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (theme::ink);
        g.drawRoundedRectangle (face, 4.0f, 1.0f);
    }
}

juce::Font GoLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return pillFont();
}

void GoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    bool shouldDrawButtonAsHighlighted, bool)
{
    const bool on = button.getToggleState();

    if (button.getProperties().contains (ledProperty))
    {
        g.setFont (ledFont());
        g.setColour (button.isEnabled() ? theme::ink : theme::faintText);
        g.drawText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (18),
                    juce::Justification::centredLeft, false);
        return;
    }

    const auto text = button.getButtonText().toUpperCase();
    auto colour = on ? theme::background : (shouldDrawButtonAsHighlighted ? theme::ink : theme::dimText);

    if (! button.isEnabled() && ! on)
        colour = theme::faintText.withMultipliedAlpha (0.7f);

    //  a single glyph - the step arrows - is a symbol, not a word: give it some size
    g.setFont (text.length() == 1 ? theme::font (17.0f) : pillFont());
    g.setColour (colour);
    g.drawFittedText (text, button.getLocalBounds().reduced (6, 0), juce::Justification::centred, 1, 0.8f);
}

void GoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    //  no box: the rule it sits on, and a chevron
    g.setColour (box.hasKeyboardFocus (true) ? theme::ink : theme::hairline);
    g.fillRect (0.0f, (float) height - 1.0f, (float) width, 1.0f);

    const float cx = (float) width - 5.0f;
    const float cy = (float) (height - 1) * 0.5f;

    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.75f);
    chevron.lineTo (cx, cy + 1.75f);
    chevron.lineTo (cx + 3.5f, cy - 1.75f);

    g.setColour (theme::faintText.withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.5f));
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font GoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return valueFont();
}

void GoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    //  flush left, so the value lines up with the caption above it
    label.setBounds (0, 0, box.getWidth() - 16, box.getHeight() - 1);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setFont (getComboBoxFont (box));
}

juce::Font GoLookAndFeel::getPopupMenuFont()
{
    return theme::font (14.0f);
}

void GoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float minSliderPos, float maxSliderPos,
                                      juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();

    if (style == juce::Slider::LinearBarVertical)
    {
        //  a patch-bay cell: a box round its number, the number being the text box
        const auto box = slider.getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (theme::well.withMultipliedAlpha (enabled ? 1.0f : 0.5f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (! enabled ? theme::hairline.withMultipliedAlpha (0.6f)
                               : (slider.isMouseOverOrDragging() ? theme::faintText : theme::hairline));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        return;
    }

    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    //  a hairline across the whole cell, filled up to a small dot
    const float centreY = (float) y + (float) height * 0.5f;

    g.setColour (theme::hairline);
    g.fillRect (0.0f, centreY - 0.75f, (float) slider.getWidth(), 1.5f);

    g.setColour (enabled ? theme::ink : theme::faintText);
    g.fillRect (0.0f, centreY - 0.75f, juce::jmax (0.0f, sliderPos), 1.5f);

    const float diameter = (enabled && slider.isMouseOverOrDragging()) ? 10.0f : 8.0f;
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre ({ sliderPos, centreY }));
}

void GoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float position, float startAngle, float endAngle, juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();
    const float alpha = enabled ? 1.0f : 0.38f;
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float radius = juce::jmin (17.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f);
    const auto centre = area.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);

    const auto pointAt = [&] (float a, float r)
    {
        return centre.getPointOnCircumference (r, a);
    };

    //  a tick per choice on a knob that steps through names
    if (slider.getProperties().contains (detentProperty))
    {
        const int steps = juce::roundToInt ((slider.getMaximum() - slider.getMinimum()) / juce::jmax (1.0, slider.getInterval()));

        g.setColour (theme::faintText.withMultipliedAlpha (alpha));

        for (int i = 0; i <= steps; ++i)
        {
            const float a = startAngle + (float) i / (float) juce::jmax (1, steps) * (endAngle - startAngle);
            g.drawLine (juce::Line<float> (pointAt (a, radius + 2.5f), pointAt (a, radius + 5.0f)), 1.0f);
        }
    }

    const juce::PathStrokeType track (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path rail;
    rail.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::hairline.withMultipliedAlpha (alpha));
    g.strokePath (rail, track);

    //  the value: from the start, or from the middle on a knob that goes both ways
    const float from = slider.getProperties().contains (bipolarProperty) ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (theme::ink.withMultipliedAlpha (alpha));
        g.strokePath (fill, track);
    }

    const float faceRadius = radius * 0.66f;
    g.setColour (theme::well.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre));
    g.setColour ((slider.isMouseOverOrDragging() && enabled ? theme::faintText : theme::hairline).withMultipliedAlpha (alpha));
    g.drawEllipse (juce::Rectangle<float> (faceRadius * 2.0f, faceRadius * 2.0f).withCentre (centre), 1.0f);

    g.setColour (theme::ink.withMultipliedAlpha (alpha));
    g.drawLine (juce::Line<float> (pointAt (angle, faceRadius * 0.25f), pointAt (angle, faceRadius * 0.85f)), 2.0f);
}

int GoLookAndFeel::getSliderThumbRadius (juce::Slider&)
{
    return 5;
}

juce::Slider::SliderLayout GoLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (slider.getTextBoxPosition() != juce::Slider::TextBoxAbove)
        return LookAndFeel_V4::getSliderLayout (slider);

    //  the value on the caption's line, right aligned; the track on the line under it
    auto bounds = slider.getLocalBounds();
    auto valueLine = bounds.removeFromTop (16);

    juce::Slider::SliderLayout layout;
    layout.textBoxBounds = valueLine.removeFromRight (juce::jmin (slider.getTextBoxWidth(), valueLine.getWidth()));
    layout.sliderBounds  = bounds.reduced (getSliderThumbRadius (slider), 0);

    return layout;
}

juce::Label* GoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    const bool centred = slider.isRotary() || slider.isBar();

    label->setFont (slider.isBar() ? theme::font (14.0f) : valueFont());
    label->setJustificationType (centred ? juce::Justification::centred : juce::Justification::centredRight);
    label->setBorderSize ({ 0, 0, 0, 0 });
    label->setMinimumHorizontalScale (0.8f);

    return label;
}

//==============================================================================
double Knob::getValueFromText (const juce::String& text)
{
    if (parse == nullptr)
        return Slider::getValueFromText (text);

    const auto typed = text.trim();
    const auto value = parse (typed);

    if (! value)
    {
        //  keep what it had: the default would read "abc" as the minimum
        if (onNote != nullptr)
            onNote ("\"" + typed + "\" is not a value");

        return getValue();
    }

    const double clamped = juce::jlimit (getMinimum(), getMaximum(), *value);

    if (! juce::approximatelyEqual (clamped, *value) && onNote != nullptr)
        onNote (typed + " is out of range - set to " + getTextFromValue (clamped));

    return clamped;
}

//==============================================================================
void SegmentedChoice::setItems (std::vector<Item> newItems)
{
    items = std::move (newItems);
    repaint();
}

void SegmentedChoice::setSelectedValue (int value)
{
    if (value != selected)
    {
        selected = value;
        repaint();
    }
}

int SegmentedChoice::itemAt (juce::Point<int> p) const
{
    if (items.empty() || ! getLocalBounds().contains (p))
        return -1;

    return juce::jlimit (0, (int) items.size() - 1, p.x * (int) items.size() / juce::jmax (1, getWidth()));
}

void SegmentedChoice::mouseDown (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());

    if (i < 0 || ! isEnabled())
        return;

    setSelectedValue (items[(size_t) i].value);

    if (onSelect != nullptr)
        onSelect (items[(size_t) i].value);
}

void SegmentedChoice::mouseMove (const juce::MouseEvent& e)
{
    const int i = itemAt (e.getPosition());

    if (i != hover)
    {
        hover = i;
        repaint();
    }
}

void SegmentedChoice::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void SegmentedChoice::paint (juce::Graphics& g)
{
    if (items.empty())
        return;

    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const int count = (int) items.size();
    const float w = bounds.getWidth() / (float) count;
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    juce::Path outline;
    outline.addRoundedRectangle (bounds, 4.0f);

    const auto segment = [&] (int i)
    {
        return juce::Rectangle<float> (bounds.getX() + (float) i * w, bounds.getY(), w, bounds.getHeight());
    };

    {
        //  the chosen segment is filled in ink; the accent stays for things that are on
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (outline);

        for (int i = 0; i < count; ++i)
        {
            if (items[(size_t) i].value == selected)
            {
                g.setColour (theme::ink.withMultipliedAlpha (alpha));
                g.fillRect (segment (i));
            }
            else if (i == hover && isEnabled())
            {
                g.setColour (theme::boardFill);
                g.fillRect (segment (i));
            }
        }
    }

    g.setColour (theme::hairline);
    g.strokePath (outline, juce::PathStrokeType (1.0f));

    for (int i = 1; i < count; ++i)
        g.fillRect (juce::Rectangle<float> (segment (i).getX() - 0.5f, bounds.getY(), 1.0f, bounds.getHeight()));

    for (int i = 0; i < count; ++i)
    {
        const bool on = items[(size_t) i].value == selected;
        auto colour = on ? theme::background : (i == hover ? theme::ink : theme::dimText);
        g.setColour (colour.withMultipliedAlpha (on ? 1.0f : alpha));

        auto area = segment (i).reduced (2.0f, 3.0f);

        if (drawIcon != nullptr)
        {
            auto label = area.removeFromBottom (13.0f);
            drawIcon (g, area, items[(size_t) i].value);
            g.setFont (captionFont().withHeight (10.0f));
            g.drawText (items[(size_t) i].text, label, juce::Justification::centred, false);
        }
        else
        {
            g.setFont (valueFont().withHeight (12.0f));
            g.drawFittedText (items[(size_t) i].text, area.toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
}

//==============================================================================
ActivityLamps::ActivityLamps (GoSequencerProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (false, false);

    channels[0] = p.apvts.getRawParameterValue ("blackChannel");
    channels[1] = p.apvts.getRawParameterValue ("whiteChannel");

    for (int h = 0; h < GoSequencerProcessor::maxHeadChannels; ++h)
        channels[(size_t) (2 + h)] = p.apvts.getRawParameterValue ("headChannel" + juce::String (h + 1));

    lastPosition.fill (-1);
    shownChannel.fill (0);
    startTimerHz (30);
}

bool ActivityLamps::inUse (int voice) const noexcept
{
    const int heads = processor.headCount();

    //  spiral routes by colour, the multi head modes by playhead
    return heads > 1 ? (voice >= 2 && voice - 2 < heads) : voice < 2;
}

int ActivityLamps::channelOf (int voice) const noexcept
{
    auto* channel = channels[(size_t) voice];
    return channel != nullptr ? juce::roundToInt (channel->load (std::memory_order_relaxed)) : 0;
}

void ActivityLamps::timerCallback()
{
    bool dirty = false;
    const int heads = processor.headCount();

    if (heads != shownHeads)
    {
        shownHeads = heads;
        lastPosition.fill (-1);
        dirty = true;
    }

    const auto playsAt = [this] (int cell)
    {
        return processor.stoneAt (cell) != go::Stone::none && ! processor.stoneIsSpent (cell);
    };

    if (processor.isRunning())
    {
        if (heads > 1)
        {
            for (int h = 0; h < heads; ++h)
            {
                const int pos = processor.headPosition (h);

                if (pos != lastPosition[(size_t) (2 + h)])
                {
                    lastPosition[(size_t) (2 + h)] = pos;

                    if (playsAt (processor.headCellAt (h, pos)))
                        level[(size_t) (2 + h)] = 1.0f;
                }
            }
        }
        else
        {
            const int step = processor.currentStep();

            if (step != lastPosition[0])
            {
                lastPosition[0] = step;
                const int cell = processor.spiralAt (step);

                if (playsAt (cell))
                    level[processor.stoneAt (cell) == go::Stone::black ? 0 : 1] = 1.0f;
            }
        }
    }

    for (int v = 0; v < voices; ++v)
    {
        auto& l = level[(size_t) v];

        if (l > 0.02f)       { l *= 0.6f; dirty = true; }
        else if (l > 0.0f)   { l = 0.0f;  dirty = true; }

        const int channel = channelOf (v);

        if (channel != shownChannel[(size_t) v])
        {
            shownChannel[(size_t) v] = channel;
            dirty = true;
        }
    }

    if (dirty)
        repaint();
}

void ActivityLamps::paint (juce::Graphics& g)
{
    const float columnWidth = (float) getWidth() / (float) voices;

    for (int v = 0; v < voices; ++v)
    {
        const float alpha = inUse (v) ? 1.0f : 0.32f;
        auto column = juce::Rectangle<float> ((float) v * columnWidth, 0.0f, columnWidth, (float) getHeight());

        const juce::String who = v == 0 ? juce::String ("B") : v == 1 ? juce::String ("W") : juce::String (v - 1);

        g.setFont (captionFont());
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.drawText (who, column.removeFromTop (14.0f), juce::Justification::centred, false);

        const auto lamp = juce::Rectangle<float> (10.0f, 10.0f).withCentre (column.removeFromTop (18.0f).getCentre());
        const float lit = level[(size_t) v];

        if (lit > 0.0f)
        {
            g.setColour (theme::accent.withAlpha (0.3f * lit));
            g.fillEllipse (lamp.expanded (4.0f));
            g.setColour (theme::accent.withAlpha (lit));
            g.fillEllipse (lamp);
        }

        g.setColour (theme::faintText.withMultipliedAlpha (alpha));
        g.drawEllipse (lamp.reduced (0.65f), 1.3f);

        g.setFont (valueFont());
        g.setColour (theme::ink.withMultipliedAlpha (alpha));
        g.drawText (juce::String (shownChannel[(size_t) v]), column.removeFromTop (16.0f), juce::Justification::centred, false);
    }
}

//==============================================================================
void LaunchpadDiagram::paint (juce::Graphics& g)
{
    //  The top row left to right, then the right hand column top to bottom -
    //  the order LaunchpadSurface::handleButton gives them their jobs in.
    static const char* const topJobs[8]  = { "rate +", "rate -", "step back", "step on",
                                             "run game", "free run", "place", "hold: clear" };
    static const char* const sideJobs[8] = { "play vs AI", "pass", "lift last", "loop",
                                             "wave replay", "move rate +", "move rate -", "redraw" };

    //  The jobs are what this drawing is for, so they are written large across
    //  the pads, where there is room: the top row's down the left half in order,
    //  each beside the mark its button carries, and each side button's in its
    //  own row, pointing at it. The pads themselves are only a faint grid.
    const float footerHeight = 46.0f, padding = 6.0f;
    const auto area = getLocalBounds().toFloat();

    const float pitch = std::floor (juce::jmin ((area.getWidth() - 2.0f * padding) / 9.0f,
                                                (area.getHeight() - 2.0f * padding - footerHeight - 8.0f) / 9.0f,
                                                44.0f));

    if (pitch < 16.0f)
        return;             //  too little room to draw anything worth reading

    const auto body = juce::Rectangle<float> (9.0f * pitch + 2.0f * padding, 9.0f * pitch + 2.0f * padding)
                        .withCentre ({ area.getCentreX(), area.getY() + padding + 4.5f * pitch });
    const float gx = body.getX() + padding, gy = body.getY() + padding;

    const auto cell = [&] (int col, int row)
    {
        return juce::Rectangle<float> (gx + (float) col * pitch, gy + (float) row * pitch, pitch, pitch);
    };

    //  up, down, left, right - the four arrows printed on the device - drawn
    //  rather than set in type, so no font has to have them
    const auto arrow = [&] (juce::Point<float> centre, float size, int direction)
    {
        static constexpr float turns[5] = { 0.0f, 1.0f, -0.5f, 0.5f, 0.5f };      //  up, down, left, right, (pointer)
        juce::Path p;
        p.addTriangle (0.0f, -size, size, size * 0.7f, -size, size * 0.7f);
        p.applyTransform (juce::AffineTransform::rotation (turns[direction] * juce::MathConstants<float>::pi)
                            .translated (centre.x, centre.y));
        g.fillPath (p);
    };

    const auto mark = [&] (juce::Rectangle<float> face, int topIndex, int sideIndex, float textHeight)
    {
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (face, 3.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (face, 3.0f, 1.2f);

        if (topIndex >= 0 && topIndex < 4)
        {
            arrow (face.getCentre(), face.getHeight() * 0.2f, topIndex);
            return;
        }

        g.setFont (theme::font (textHeight, juce::Font::bold));
        g.drawText (juce::String (topIndex >= 0 ? topIndex + 1 : sideIndex + 1), face, juce::Justification::centred, false);
    };

    g.setColour (theme::hairline);
    g.drawRoundedRectangle (body.reduced (0.5f), 9.0f, 1.0f);

    //  the function buttons, the part to read
    for (int i = 0; i < 8; ++i)
    {
        mark (cell (i, 0).reduced (pitch * 0.12f), i, -1, pitch * 0.38f);
        mark (cell (8, i + 1).reduced (pitch * 0.12f), -1, i, pitch * 0.38f);
    }

    //  the logo, top right, which does nothing here
    g.setColour (theme::faintText);
    g.fillEllipse (juce::Rectangle<float> (pitch * 0.34f, pitch * 0.34f).withCentre (cell (8, 0).getCentre()));

    //  the pads, faintly
    for (int row = 1; row <= 8; ++row)
        for (int col = 0; col < 8; ++col)
        {
            g.setColour (theme::ink.withAlpha (theme::isDark ? 0.06f : 0.05f));
            g.fillRoundedRectangle (cell (col, row).reduced (pitch * 0.15f), 3.0f);
        }

    //  the halves: the top row's jobs on the left, the side buttons' on the right
    {
        const float x = gx + 4.0f * pitch;
        g.setColour (theme::hairline);

        for (float y = gy + pitch + 3.0f; y < gy + 9.0f * pitch - 3.0f; y += 6.0f)
            g.fillRect (juce::Rectangle<float> (x - 0.5f, y, 1.0f, 3.0f));
    }

    const auto textFont = theme::font (juce::jmax (11.5f, pitch * 0.45f));
    const float chip = juce::jmin (16.0f, pitch * 0.62f);

    for (int r = 0; r < 8; ++r)
    {
        const auto line = juce::Rectangle<float> (gx, gy + (float) (r + 1) * pitch, 8.0f * pitch, pitch);
        auto left  = line.withWidth (4.0f * pitch).reduced (3.0f, 0.0f);
        auto right = line.withTrimmedLeft (4.0f * pitch).reduced (3.0f, 0.0f);

        //  the same mark as the button it names
        mark (left.removeFromLeft (chip).withSizeKeepingCentre (chip, chip), r, -1, chip * 0.58f);
        left.removeFromLeft (5.0f);

        g.setFont (textFont);
        g.setColour (theme::ink);
        g.drawFittedText (topJobs[r], left.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        //  and a pointer at the button in this row
        g.setColour (theme::accent);
        arrow ({ right.getRight() - 4.0f, right.getCentreY() }, 3.5f, 4);
        right.removeFromRight (12.0f);

        g.setColour (theme::ink);
        g.drawFittedText (sideJobs[r], right.toNearestInt(), juce::Justification::centredRight, 1, 0.8f);
    }

    g.setFont (theme::font (11.5f));
    g.setColour (theme::dimText);
    g.drawFittedText ("left half: the top row, left to right. right half: each side button, in its own row. "
                      "the pads are the 8 x 8 board - press to place, press a stone to lift it",
                      juce::Rectangle<float> (area.getX(), body.getBottom() + 8.0f, area.getWidth(), footerHeight).toNearestInt(),
                      juce::Justification::topLeft, 3, 1.0f);
}

//==============================================================================
GoSequencerEditor::GoSequencerEditor (GoSequencerProcessor& p)
    : AudioProcessorEditor (&p), processor (p), board (p), activity (p)
{
    setLookAndFeel (&lookAndFeel);

    //  the scheme has to be right before anything below reads a theme:: colour
    theme::setDark (processor.apvts.state.getProperty (darkModeProperty, true));
    lookAndFeel.applyColours();

    lastSgfDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    plate.painter = [this] (juce::Graphics& g) { paintPlate (g); };
    addAndMakeVisible (plate);

    //  ---- the top bar: on both faces ---------------------------------------
    faceSwitch.setItems ({ { "PLAY", playFace }, { "PATCH", patchFace } });
    faceSwitch.setTitle ("panel face");
    faceSwitch.onSelect = [this] (int face) { showFace (face); };
    addToFace (pinned, faceSwitch);

    darkModeButton.setButtonText ("Dark");
    darkModeButton.getProperties().set (ledProperty, true);
    darkModeButton.setClickingTogglesState (true);
    darkModeButton.setToggleState (theme::isDark, juce::dontSendNotification);
    darkModeButton.setTitle ("switch between light and dark");
    darkModeButton.onClick = [this] { setDarkMode (darkModeButton.getToggleState()); };
    addToFace (pinned, darkModeButton);

    //  ---- the board and what a click on it does: on both faces -------------
    addToFace (pinned, board);
    board.onMessage = [this] (const juce::String& text) { showMessage (text); };

    //  in size order on screen; the values are the parameter's append-only slots
    setUpSegments (pinned, sizeSwitch, { { "8", 3 }, { "9", 0 }, { "13", 1 }, { "19", 2 } }, "boardSize", sizeAttachment);
    setUpCaption (pinned, sizeCaption, "board");
    setUpSegments (pinned, placeSwitch,
                   { { "Alt", 0 }, { juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f")), 1 },
                     { juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b")), 2 } },
                   "colourMode", placeAttachment);
    setUpCaption (pinned, placeCaption, "place");

    setUpButton (pinned, clearButton, "Clear board", [this]
    {
        processor.clearBoard();
        showMessage ("board cleared");
        board.repaint();
    });

    //  the separators are UTF-8: JUCE must be told, or they arrive as Latin-1
    hintLabel.setText (juce::String (juce::CharPointer_UTF8 (
                           "click to place  \xc2\xb7  click a stone to lift it  \xc2\xb7  drop an .sgf anywhere  \xc2\xb7  "
                           "click any value to type it")),
                       juce::dontSendNotification);
    setUpText (pinned, hintLabel, juce::Justification::centred);
    hintLabel.setFont (valueFont().withHeight (11.5f));
    hintLabel.setColour (juce::Label::textColourId, theme::faintText);

    //  ==== PLAY ==============================================================
    //  ---- clock
    setUpSegments (playFace, modeSwitch,
                   { { "spiral", 0 }, { "poly", 1 }, { "quads out", 2 }, { "quads in", 3 } },
                   "playMode", modeAttachment);
    modeSwitch.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> area, int mode) { drawModeIcon (g, area, mode); };

    setUpKnob (playFace, rateKnob, rateCaption, "step rate", "rate", rateAttachment,
               choiceParser (GoSequencerProcessor::rateNames()));
    rateKnob.getProperties().set (detentProperty, true);
    setUpKnob (playFace, tempoKnob, tempoCaption, "free tempo", "tempo", tempoAttachment,
               parser ([] (const std::string& t) { return valuetext::number (t, { "bpm" }); }));
    setUpLed (playFace, freeRunButton, "Free run", "freeRun", freeRunAttachment);
    setUpLed (playFace, tieNotesButton, "Tie notes", "tieNotes", tieNotesAttachment);

    //  ---- voice
    setUpKnob (playFace, noteKnob, noteCaption, "note", "note", noteAttachment,
               parser ([] (const std::string& t) { return valuetext::note (t); }));
    setUpKnob (playFace, gateKnob, gateCaption, "gate", "gate", gateAttachment,
               parser ([] (const std::string& t) { return valuetext::percent (t); }));
    setUpKnob (playFace, lifeKnob, lifeCaption, "stone life", "stoneLife", lifeAttachment,
               parser ([] (const std::string& t) { return valuetext::life (t, GoSequencerProcessor::maxStoneLife); }));
    setUpKnob (playFace, spreadKnob, spreadCaption, "spread", "ringSpread", spreadAttachment,
               wholeParser ({ "semitones", "semitone", "st" }));
    spreadKnob.getProperties().set (bipolarProperty, true);
    setUpKnob (playFace, blackVelocityKnob, blackVelocityCaption,
               juce::String (juce::CharPointer_UTF8 ("vel \xe2\x97\x8f")), "blackVelocity", blackVelocityAttachment, wholeParser());
    setUpKnob (playFace, whiteVelocityKnob, whiteVelocityCaption,
               juce::String (juce::CharPointer_UTF8 ("vel \xe2\x97\x8b")), "whiteVelocity", whiteVelocityAttachment, wholeParser());
    blackVelocityKnob.setTitle ("black velocity");
    whiteVelocityKnob.setTitle ("white velocity");

    setUpSegments (playFace, lifeModeSwitch, { { "Steps", 0 }, { "Placements", 1 } }, "lifeMode", lifeModeAttachment);
    setUpCaption (playFace, lifeModeCaption, "life counts");

    //  ---- activity
    addToFace (playFace, activity);
    setUpText (playFace, activityLabel, juce::Justification::topLeft);
    setUpText (playFace, lapLabel, juce::Justification::topLeft);

    //  ---- game record
    setUpText (playFace, gameTitleLabel, juce::Justification::centredLeft);
    setUpText (playFace, gameDetailLabel, juce::Justification::centredLeft);
    gameTitleLabel.setFont (valueFont().boldened());

    setUpButton (playFace, loadButton, juce::String (juce::CharPointer_UTF8 ("Load SGF\xe2\x80\xa6")),
                 [this] { openSgfChooser(); });

    setUpButton (playFace, unloadButton, "Unload", [this]
    {
        processor.clearGame();          //  a run ends here too: it turns its own switch off
        refreshGameDisplay();
        showMessage ("game record unloaded");
    });

    setUpKnob (playFace, gameRateKnob, gameRateCaption, "move rate", "gameRate", gameRateAttachment,
               choiceParser (GoSequencerProcessor::gameRateNames()));
    gameRateKnob.getProperties().set (detentProperty, true);
    setUpKnob (playFace, waveGapKnob, waveGapCaption, "wave gap", "waveGap", waveGapAttachment, wholeParser());
    waveGapKnob.setEnabled (processor.waveReplayOn());          //  synced again every tick; this is just the initial state

    setUpLed (playFace, runGameButton, "Run game", "gameRun", runGameAttachment);
    setUpLed (playFace, loopGameButton, "Loop", "gameLoop", loopGameAttachment);
    setUpLed (playFace, waveReplayButton, "Wave replay", "waveReplay", waveReplayAttachment);

    moveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    moveSlider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 110, valueHeight);
    moveSlider.setRepaintsOnMouseActivity (true);
    moveSlider.setTitle ("position in the record");
    moveSlider.setRange (0.0, 1.0, 1.0);
    moveSlider.textFromValueFunction = [this] (double value)
    {
        return "move " + juce::String ((int) value) + " / " + juce::String (processor.gameMoveCount());
    };
    moveSlider.valueFromTextFunction = [this] (const juce::String& text)
    {
        //  "34", or the box's own "move 34 / 112": the first number is the move
        const auto digits = text.upToFirstOccurrenceOf ("/", false, false).retainCharacters ("0123456789");

        if (digits.isEmpty())
        {
            showMessage ("position: \"" + text.trim() + "\" is not a move number");
            return moveSlider.getValue();
        }

        return (double) digits.getIntValue();
    };
    moveSlider.onValueChange = [this]
    {
        const int wanted = (int) moveSlider.getValue();

        if (wanted != processor.gamePosition())
            processor.setGamePosition (wanted);
    };
    addToFace (playFace, moveSlider);
    setUpCaption (playFace, moveCaption, "position");

    setUpButton (playFace, previousMoveButton, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")),
                 [this] { processor.nudgeGamePosition (-1); refreshGameDisplay(); });
    previousMoveButton.setTitle ("previous move");

    setUpButton (playFace, nextMoveButton, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")),
                 [this] { processor.nudgeGamePosition (1); refreshGameDisplay(); });
    nextMoveButton.setTitle ("next move");

    //  ---- players: self-play
    //  The record is written rather than loaded, so Move Rate, Run and Loop
    //  drive a generated game exactly as they drive a loaded one.
    setUpLed (playFace, aiPlayButton, "AI self-play", "aiPlay", aiPlayAttachment);

    //  which pair writes the games: the classic players, or the ones that read
    //  ladders, eye shapes and areas before they choose
    setUpSegments (playFace, playersSwitch, { { "Classic", 0 }, { "Reading", 1 } }, "aiPlayers", playersAttachment);
    setUpKnob (playFace, aiMovesKnob, aiMovesCaption, "length", "aiMoves", aiMovesAttachment,
               wholeParser ({ "moves", "move", "mv" }));
    setUpKnob (playFace, aiVariationKnob, aiVariationCaption, "variation", "aiVariation", aiVariationAttachment,
               wholeParser ({ "%" }));
    setUpKnob (playFace, aiSeedKnob, aiSeedCaption, "seed", "aiSeed", aiSeedAttachment, wholeParser());

    for (auto* knob : { &aiMovesKnob, &aiVariationKnob, &aiSeedKnob })
        knob->setEnabled (processor.aiSelfPlay());                 //  as above: the tick keeps these in step

    playersSwitch.setEnabled (processor.aiSelfPlay());

    //  The opening. A position is not an opening - the order decides what is
    //  captured - so this takes the ten stones the board was clicked in, not
    //  the ten stones standing on it.
    setUpCaption (playFace, openingCaption, "opening");

    setUpButton (playFace, openingFromBoardButton, "From board", [this]
    {
        const auto error = processor.setOpeningFromBoard();

        if (error.isNotEmpty())
        {
            showMessage (error);
            return;
        }

        refreshOpeningDisplay();
        showMessage (processor.aiSelfPlay() ? "opening set - from the next game"
                                            : "opening set");
    });

    setUpButton (playFace, openingBookButton, "Use book", [this]
    {
        processor.useBookOpening();
        refreshOpeningDisplay();
        showMessage ("back to the book opening");
    });

    setUpText (playFace, openingLabel, juce::Justification::topLeft);

    //  ---- players: playing against them
    //  The same pair, answering a move at a time instead of writing a whole
    //  game. It owns the board while it is on, so the processor turns self-play
    //  and Run Game off rather than let two things write to the same stones.
    setUpLed (playFace, aiOpponentButton, "Play against", "aiOpponent", aiOpponentAttachment);
    setUpSegments (playFace, opponentSwitch, { { "White", 0 }, { "Black", 1 } }, "aiOpponentColour", opponentAttachment);
    setUpCaption (playFace, opponentCaption, "they play");

    setUpButton (playFace, passButton, "Pass", [this]
    {
        processor.passMove();
        board.repaint();
        refreshMatchDisplay();
    });

    setUpButton (playFace, newMatchButton, "New game", [this]
    {
        processor.newMatch();
        board.repaint();
        refreshMatchDisplay();
        showMessage ("a new game, from an empty board");
    });

    setUpText (playFace, matchLabel, juce::Justification::topLeft);

    //  ==== PATCH =============================================================
    //  ---- routing: velocity follows the colour in every mode; the cells the
    //  mode is not routing by are greyed out by refreshModeDisplay()
    setUpCaption (patchFace, routingCaption, "midi channel per voice - drag, or click to type");
    setUpCell (blackChannelCell, blackChannelCaption, juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f")),
               "blackChannel", blackChannelAttachment);
    setUpCell (whiteChannelCell, whiteChannelCaption, juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8b")),
               "whiteChannel", whiteChannelAttachment);
    blackChannelCell.setTitle ("black channel");
    whiteChannelCell.setTitle ("white channel");

    for (int h = 0; h < headChannels; ++h)
        setUpCell (headChannelCells[(size_t) h], headChannelCaptions[(size_t) h], juce::String (h + 1),
                   "headChannel" + juce::String (h + 1), headChannelAttachments[(size_t) h]);

    setUpText (patchFace, routingLabel, juce::Justification::topLeft);

    //  ---- the port: the channels above, kept intact for a host that would
    //  merge them on its own track to track routing - see MidiPortOut
    portBox.setTitle ("midi out port");
    portBox.onOpen = [this] { refreshPortList(); };
    portBox.onChange = [this]
    {
        const int index = portBox.getSelectedId() - 2;

        processor.setMidiOutPort (juce::isPositiveAndBelow (index, portItems.size()) ? portItems[index]
                                                                                     : juce::String());
        refreshPortStatus();
    };
    addToFace (patchFace, portBox);
    setUpText (patchFace, portStatusLabel, juce::Justification::topLeft);
    refreshPortList();

    //  ---- rules
    setUpLed (patchFace, koButton, "Ko rule", "koRule", koAttachment);
    setUpLed (patchFace, selfCaptureButton, "Self capture", "selfCapture", selfCaptureAttachment);
    rulesLabel.setText ("set once per piece: they decide which moves are legal, not how anything sounds",
                        juce::dontSendNotification);
    setUpText (patchFace, rulesLabel, juce::Justification::topLeft);

    //  ---- the Launchpad
    //  Its grid as the board. Choosing the ports is all there is to it: the
    //  plugin puts the Launchpad into Programmer mode itself and gives it back
    //  when it lets go, so nothing is set up in Novation Components.
    padsInBox.setTitle ("launchpad in");
    padsInBox.onOpen = [this] { refreshPadsLists(); };
    padsInBox.onChange = [this]
    {
        const int index = padsInBox.getSelectedId() - 2;

        choosePadsPorts (juce::isPositiveAndBelow (index, padsInItems.size()) ? padsInItems[index] : juce::String(),
                         processor.launchpadOut());
    };
    addToFace (patchFace, padsInBox);
    setUpCaption (patchFace, padsInCaption, "pads in");

    padsOutBox.setTitle ("launchpad out");
    padsOutBox.onOpen = [this] { refreshPadsLists(); };
    padsOutBox.onChange = [this]
    {
        const int index = padsOutBox.getSelectedId() - 2;

        choosePadsPorts (processor.launchpadIn(),
                         juce::isPositiveAndBelow (index, padsOutItems.size()) ? padsOutItems[index] : juce::String());
    };
    addToFace (patchFace, padsOutBox);
    setUpCaption (patchFace, padsOutCaption, "pads out");

    setUpButton (patchFace, padsFindButton, "Find Launchpad", [this]
    {
        const auto found = LaunchpadSurface::findLaunchpad();

        if (found.in.isEmpty() || found.out.isEmpty())
        {
            showMessage ("no Launchpad X found - is it plugged in?");
            refreshPadsLists();
            return;
        }

        choosePadsPorts (found.in, found.out);
    });

    setUpButton (patchFace, padsSizeButton, "Use 8 x 8", [this]
    {
        useLaunchpadBoardSize();
        showMessage ("the board is 8 x 8 now, the size of the grid");
    });

    setUpButton (patchFace, padsStopButton, "Stop", [this]
    {
        processor.setLaunchpadPorts ({}, {});
        refreshPadsLists();
        showMessage ("the Launchpad is back to its own modes");
    });

    setUpText (patchFace, padsStatusLabel, juce::Justification::topLeft);

    //  the device itself, with what each button round its edge does here
    addToFace (patchFace, padsDiagram);

    refreshPadsLists();
    refreshMatchDisplay();
    refreshOpeningDisplay();
    refreshGameDisplay();
    refreshModeDisplay();

    //  Sliders build their value boxes - colours, font, alignment - from the
    //  look-and-feel they have when it changes, and everything above was added
    //  after it was set. Tell them again now that they are all in, or the value
    //  boxes keep the default dark scheme's white text.
    sendLookAndFeelChange();

    showFace ((int) processor.apvts.state.getProperty (panelFaceProperty, (int) playFace));

    //  the plate is drawn at one size and scaled, so the window keeps its shape
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) Faceplate::width / (double) Faceplate::height);
    setResizeLimits (Faceplate::width * 4 / 5, Faceplate::height * 4 / 5,
                     Faceplate::width * 3 / 2, Faceplate::height * 3 / 2);
    setSize (Faceplate::width, Faceplate::height);

    startTimerHz (20);
}

GoSequencerEditor::~GoSequencerEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void GoSequencerEditor::addToFace (Face face, juce::Component& component)
{
    if (face == pinned)
    {
        plate.addAndMakeVisible (component);
        return;
    }

    plate.addChildComponent (component);
    faceMembers[(size_t) face].push_back (&component);
}

void GoSequencerEditor::showFace (int face)
{
    currentFace = juce::jlimit (0, faceCount - 1, face);
    faceSwitch.setSelectedValue (currentFace);

    for (int f = 0; f < faceCount; ++f)
        for (auto* component : faceMembers[(size_t) f])
            component->setVisible (f == currentFace);

    processor.apvts.state.setProperty (panelFaceProperty, currentFace, nullptr);
    plate.repaint();
}

void GoSequencerEditor::setDarkMode (bool dark)
{
    theme::setDark (dark);
    lookAndFeel.applyColours();

    for (auto* label : dimLabels)
        label->setColour (juce::Label::textColourId, theme::dimText);

    hintLabel.setColour (juce::Label::textColourId, theme::faintText);

    //  these colour themselves ink or dimText depending on state, not always
    //  dimText, so they need re-reading rather than the flat reset above
    refreshOpeningDisplay();
    refreshGameDisplay();
    refreshPortStatus();
    refreshMatchDisplay();
    refreshPadsStatus();
    refreshModeDisplay();

    processor.apvts.state.setProperty (darkModeProperty, dark, nullptr);

    sendLookAndFeelChange();
    board.refreshAll();         //  the grid is a cached image in the old colours
    repaint();
    plate.repaint();
}

void GoSequencerEditor::setUpCaption (Face face, juce::Label& caption, const juce::String& text)
{
    caption.setText (text.toUpperCase(), juce::dontSendNotification);
    caption.setFont (captionFont());
    caption.setColour (juce::Label::textColourId, theme::dimText);
    caption.setBorderSize ({ 0, 0, 0, 0 });
    caption.setJustificationType (juce::Justification::centredLeft);
    caption.setMinimumHorizontalScale (0.8f);
    caption.setInterceptsMouseClicks (false, false);
    addToFace (face, caption);
    dimLabels.push_back (&caption);
}

void GoSequencerEditor::setUpText (Face face, juce::Label& label, juce::Justification justification)
{
    label.setFont (valueFont().withHeight (12.0f));
    label.setColour (juce::Label::textColourId, theme::dimText);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setJustificationType (justification);
    label.setInterceptsMouseClicks (false, false);
    addToFace (face, label);
    dimLabels.push_back (&label);
}

void GoSequencerEditor::setUpKnob (Face face, Knob& knob, juce::Label& caption, const juce::String& text,
                                   const juce::String& parameterID, std::unique_ptr<SliderAttachment>& attachment,
                                   std::function<std::optional<double> (const juce::String&)> parse)
{
    knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth, valueHeight);
    knob.setMouseDragSensitivity (220);
    knob.setRepaintsOnMouseActivity (true);
    knob.setTitle (text);
    knob.parse = std::move (parse);
    knob.onNote = [this, text] (const juce::String& note) { showMessage (text + ": " + note); };
    addToFace (face, knob);

    setUpCaption (face, caption, text);
    caption.setJustificationType (juce::Justification::centred);

    attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, knob);
}

void GoSequencerEditor::setUpCell (Knob& cell, juce::Label& caption, const juce::String& text,
                                   const juce::String& parameterID, std::unique_ptr<SliderAttachment>& attachment)
{
    //  a bar slider is the one whose text box covers it: a click types, a drag
    //  changes - ten pixels a channel
    cell.setSliderStyle (juce::Slider::LinearBarVertical);
    cell.setSliderSnapsToMousePosition (false);
    cell.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 30, 30);
    cell.setMouseDragSensitivity (160);
    cell.setRepaintsOnMouseActivity (true);
    cell.setTitle ("head " + text + " channel");
    cell.parse = wholeParser();
    cell.onNote = [this] (const juce::String& note) { showMessage ("channel: " + note); };
    addToFace (patchFace, cell);

    setUpCaption (patchFace, caption, text);
    caption.setJustificationType (juce::Justification::centred);

    attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, cell);
}

void GoSequencerEditor::setUpSegments (Face face, SegmentedChoice& segments, std::vector<SegmentedChoice::Item> items,
                                       const juce::String& parameterID,
                                       std::unique_ptr<juce::ParameterAttachment>& attachment)
{
    segments.setItems (std::move (items));
    addToFace (face, segments);

    auto* parameter = processor.apvts.getParameter (parameterID);
    jassert (parameter != nullptr);

    //  a choice parameter's denormalised value is its index
    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [&segments] (float value)
    {
        segments.setSelectedValue (juce::roundToInt (value));
    });

    segments.onSelect = [a = attachment.get()] (int value) { a->setValueAsCompleteGesture ((float) value); };
    attachment->sendInitialUpdate();
}

void GoSequencerEditor::setUpLed (Face face, juce::TextButton& button, const juce::String& text,
                                  const juce::String& parameterID, std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText (text);
    button.getProperties().set (ledProperty, true);
    button.setClickingTogglesState (true);
    addToFace (face, button);

    attachment = std::make_unique<ButtonAttachment> (processor.apvts, parameterID, button);
}

void GoSequencerEditor::setUpButton (Face face, juce::TextButton& button, const juce::String& text,
                                     std::function<void()> onClick)
{
    button.setButtonText (text);
    button.onClick = std::move (onClick);
    addToFace (face, button);
}

//==============================================================================
void GoSequencerEditor::openSgfChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a game record", lastSgfDirectory, "*.sgf");

    const auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync (chooserFlags, [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file != juce::File())
            loadSgfFile (file);
    });
}

void GoSequencerEditor::loadSgfFile (const juce::File& file)
{
    const auto error = processor.loadSgf (file);

    if (error.isNotEmpty())
    {
        showMessage (error);
        return;
    }

    lastSgfDirectory = file.getParentDirectory();
    refreshGameDisplay();
    showMessage (file.getFileName() + " loaded");
    board.repaint();
}

void GoSequencerEditor::showMessage (const juce::String& text)
{
    message = text;
    messageCountdown = text.isEmpty() ? 0 : 90;
    plate.repaint (topBarBounds);
}

void GoSequencerEditor::refreshOpeningDisplay()
{
    const auto description = processor.openingDescription();
    const int played = processor.handPlayedCount();

    juce::String line = description;

    //  while there are not ten stones to take, say how far off it is rather
    //  than leave the button to refuse without warning
    if (played > 0 && played < GoSequencerProcessor::openingLength)
        line << "  (" << played << " of " << GoSequencerProcessor::openingLength << " played)";

    openingLabel.setText (line, juce::dontSendNotification);
    openingLabel.setColour (juce::Label::textColourId,
                            processor.hasCustomOpening() ? theme::ink : theme::dimText);

    openingFromBoardButton.setEnabled (played >= GoSequencerProcessor::openingLength);
    openingBookButton.setEnabled (processor.hasCustomOpening());

    lastOpeningShown = line;
}

void GoSequencerEditor::refreshGameDisplay()
{
    const bool has = processor.hasGame();

    gameTitleLabel.setText (has ? processor.gameTitle() : "no record loaded", juce::dontSendNotification);
    gameTitleLabel.setColour (juce::Label::textColourId, has ? theme::ink : theme::dimText);
    gameDetailLabel.setText (has ? processor.gameDetail() : "drop an .sgf here, or load one",
                             juce::dontSendNotification);

    moveSlider.setEnabled (has);
    previousMoveButton.setEnabled (has);
    nextMoveButton.setEnabled (has);
    unloadButton.setEnabled (has);
    runGameButton.setEnabled (has);

    moveSlider.setRange (0.0, (double) juce::jmax (1, processor.gameMoveCount()), 1.0);
    moveSlider.setValue ((double) processor.gamePosition(), juce::dontSendNotification);
    lastMoveShown = processor.gamePosition();
}

void GoSequencerEditor::refreshPortList()
{
    const auto wanted = processor.midiOutPort();

    portItems = MidiPortOut::availablePorts();

    //  a port the session names but the system does not have right now stays
    //  in the list, so the choice is not lost because loopMIDI is not running
    const bool missing = wanted.isNotEmpty() && ! portItems.contains (wanted);

    if (missing)
        portItems.add (wanted);

    portBox.clear (juce::dontSendNotification);
    portBox.addItem ("Off", 1);

    for (int i = 0; i < portItems.size(); ++i)
        portBox.addItem (portItems[i] + (missing && i == portItems.size() - 1 ? "  (not there)" : ""), i + 2);

    portBox.setSelectedId (wanted.isEmpty() ? 1 : portItems.indexOf (wanted) + 2, juce::dontSendNotification);

    refreshPortStatus();
}

void GoSequencerEditor::refreshPortStatus()
{
    const auto wanted = processor.midiOutPort();
    const bool open = processor.midiOutPortOpen();

    juce::String status;

    if (wanted.isEmpty())
        status = "notes go to the host only";
    else if (open)
        status = "also sending to " + wanted + " - each track listening to it can pick out a channel";
    else
        status = "waiting for " + wanted + " - is loopMIDI running?";

    portStatusLabel.setText (status, juce::dontSendNotification);
    portStatusLabel.setColour (juce::Label::textColourId,
                               (wanted.isNotEmpty() && ! open) ? theme::accent : theme::dimText);

    lastPortOpenShown = open;
}

void GoSequencerEditor::refreshMatchDisplay()
{
    const bool on   = processor.matchActive();
    const bool over = on && processor.matchIsOver();

    //  their colour stays choosable with no game on, so it can be set before one
    //  starts - changing it during a game starts that game again
    passButton.setEnabled (on && processor.yourTurn());
    newMatchButton.setEnabled (on);

    juce::String line;

    if (! on)
        line = "off - the board is yours to place on";
    else if (over)
        line = "both passed - " + juce::String (processor.capturedBlack()) + " black and "
             + juce::String (processor.capturedWhite()) + " white taken";
    else if (processor.yourTurn())
        line = juce::String ("your move - you are ")
             + (processor.yourColour() == go::Stone::black ? "black" : "white")
             + (processor.passCount() == 1 ? ", and a pass stands: pass again to end it" : "");
    else
        line = "their move";

    matchLabel.setText (line, juce::dontSendNotification);
    matchLabel.setColour (juce::Label::textColourId, over ? theme::accent : theme::dimText);
}

//==============================================================================
void GoSequencerEditor::refreshPadsLists()
{
    const auto fill = [] (RefreshingComboBox& box, juce::StringArray& items,
                          juce::StringArray available, const juce::String& wanted)
    {
        items = std::move (available);

        //  a port the session names but the system does not have right now stays
        //  in the list, so the choice survives the Launchpad being unplugged
        const bool missing = wanted.isNotEmpty() && ! items.contains (wanted);

        if (missing)
            items.add (wanted);

        box.clear (juce::dontSendNotification);
        box.addItem ("Off", 1);

        for (int i = 0; i < items.size(); ++i)
            box.addItem (items[i] + (missing && i == items.size() - 1 ? "  (not there)" : ""), i + 2);

        box.setSelectedId (wanted.isEmpty() ? 1 : items.indexOf (wanted) + 2, juce::dontSendNotification);
    };

    fill (padsInBox,  padsInItems,  LaunchpadSurface::availableInputs(),  processor.launchpadIn());
    fill (padsOutBox, padsOutItems, LaunchpadSurface::availableOutputs(), processor.launchpadOut());

    refreshPadsStatus();
}

void GoSequencerEditor::refreshPadsStatus()
{
    const auto in   = processor.launchpadIn();
    const auto out  = processor.launchpadOut();
    const bool open = processor.launchpadOpen();
    const int size  = processor.boardSize();
    const auto surface = processor.launchpad().status();

    const bool chosen = in.isNotEmpty() && out.isNotEmpty();

    juce::String line;
    bool trouble = false;

    if (in.isEmpty() && out.isEmpty())
    {
        line = "no controller - plug in a Launchpad X and press Find Launchpad";
    }
    else if (! chosen)
    {
        line = "choose an in port and an out port - Find Launchpad does both";
        trouble = true;
    }
   #if JUCE_WINDOWS
    else if (in.equalsIgnoreCase ("LPX MIDI") || out.equalsIgnoreCase ("LPX MIDI"))
    {
        //  Windows lists the Launchpad's DAW interface as a bare "LPX MIDI", just
        //  above the MIDIIN2 / MIDIOUT2 pair the grid talks on - easily picked
        line = "that is the Launchpad's DAW port - the pads need MIDIIN2 and MIDIOUT2 (LPX MIDI): "
               "press Find Launchpad";
        trouble = true;
    }
   #endif
    else
    {
        //  a restored session opens its ports a moment after the editor asks
        line = surface.isNotEmpty() ? surface : "opening " + out;
        trouble = ! open || size != lpx::boardSize;
    }

    padsStatusLabel.setText (line, juce::dontSendNotification);
    padsStatusLabel.setColour (juce::Label::textColourId, trouble ? theme::accent : theme::dimText);

    padsSizeButton.setEnabled (chosen && size != lpx::boardSize);
    padsStopButton.setEnabled (in.isNotEmpty() || out.isNotEmpty());

    lastPadsStatusShown = surface;
    lastPadsOpenShown = open;
    lastPadsSizeShown = size;
}

void GoSequencerEditor::choosePadsPorts (const juce::String& in, const juce::String& out)
{
    processor.setLaunchpadPorts (in, out);

    //  The grid only shows an 8x8, so choosing the controller is taken as asking
    //  for that board. A size change clears the board, though, so when there is
    //  anything on it to lose, the change waits for Use 8 x 8.
    if (in.isNotEmpty() && out.isNotEmpty() && processor.boardSize() != lpx::boardSize)
    {
        if (boardHasSomethingToLose())
            showMessage ("the pads stay dark until the board is 8 x 8 - Use 8 x 8 clears this one");
        else
            useLaunchpadBoardSize();
    }

    refreshPadsLists();
}

void GoSequencerEditor::useLaunchpadBoardSize()
{
    //  through the parameter, as the board dropdown does, so the host hears
    //  about it and the dropdown follows
    if (auto* size = dynamic_cast<juce::AudioParameterChoice*> (processor.apvts.getParameter ("boardSize")))
    {
        size->beginChangeGesture();
        size->setValueNotifyingHost (size->convertTo0to1 ((float) go::sizeSlot (lpx::boardSize)));
        size->endChangeGesture();
    }
}

bool GoSequencerEditor::boardHasSomethingToLose() const
{
    if (processor.hasGame())
        return true;            //  a record of another size goes with the board

    const int cells = processor.boardSize() * processor.boardSize();

    for (int i = 0; i < cells; ++i)
        if (processor.stoneAt (i) != go::Stone::none)
            return true;

    return false;
}

//==============================================================================
bool GoSequencerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& file : files)
        if (file.endsWithIgnoreCase (".sgf"))
            return true;

    return false;
}

void GoSequencerEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    dragHighlight = true;
    repaint();
}

void GoSequencerEditor::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void GoSequencerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();

    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase (".sgf"))
        {
            loadSgfFile (juce::File (file));
            return;
        }
    }
}


//==============================================================================
namespace
{
    /** What the display in the top bar shows, as key and value pairs. */
    std::vector<std::pair<juce::String, juce::String>> displayFields (const GoSequencerProcessor& processor)
    {
        const int steps = juce::jmax (1, processor.cycleSteps());
        const int step  = juce::jlimit (0, steps - 1, processor.currentStep());

        const juce::String mode = processor.isQuads()       ? juce::String ("4 quadrants")
                                : processor.isPolyrhythm()  ? juce::String (processor.ringCount()) + " rings"
                                                            : juce::String ("spiral");

        std::vector<std::pair<juce::String, juce::String>> fields
        {
            { "STEP", juce::String (step + 1) + "/" + juce::String (steps) },
            { "MODE", mode },
            { "CAPT", juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) + juce::String (processor.capturedBlack())
                      + juce::String (juce::CharPointer_UTF8 ("  \xe2\x97\x8b ")) + juce::String (processor.capturedWhite()) },
        };

        if (processor.hasGame())
            fields.push_back ({ "MOVE", juce::String (processor.gamePosition()) + "/" + juce::String (processor.gameMoveCount()) });

        return fields;
    }
}

//==============================================================================
void GoSequencerEditor::resized()
{
    //  the plate is laid out once at its own size; the window only scales it
    const float scale = juce::jmin ((float) getWidth()  / (float) Faceplate::width,
                                    (float) getHeight() / (float) Faceplate::height);

    plate.setBounds (0, 0, Faceplate::width, Faceplate::height);
    plate.setTransform (juce::AffineTransform::scale (scale));

    sections.clear();

    const auto addSection = [this] (juce::Rectangle<int> bounds, const juce::String& title, Face face)
    {
        sections.push_back ({ bounds, title, face });
        return bounds.withTrimmedLeft (sectionPadX).withTrimmedRight (sectionPadX)
                     .withTrimmedTop (sectionPadTop).withTrimmedBottom (sectionPadBottom);
    };

    const auto gap = [] (juce::Rectangle<int>& area, int pixels) { area.removeFromTop (pixels); };

    //  ---- top bar ----------------------------------------------------------
    topBarBounds = { 22, 16, Faceplate::width - 44, 48 };
    {
        auto bar = topBarBounds;

        const int dark = ledWidth (darkModeButton);
        darkModeButton.setBounds (bar.removeFromRight (dark).withSizeKeepingCentre (dark, ledHeight));
        bar.removeFromRight (18);
        faceSwitch.setBounds (bar.removeFromRight (150).withSizeKeepingCentre (150, 28));
        bar.removeFromRight (18);

        //  the wordmark and the next stone are painted; the display fills what is left
        bar.removeFromLeft (textWidth (titleFont(), "GO SEQUENCER") + 10 + 12 + 26);
        displayBounds = bar.withSizeKeepingCentre (bar.getWidth(), 36);
    }

    constexpr int top = 78, bottom = Faceplate::height - 18;
    constexpr int leftX = 22, railWidth = 282, boardX = leftX + railWidth + 19, boardSide = 556;
    constexpr int rightX = boardX + boardSide + 19, rightWidth = Faceplate::width - 22 - rightX;

    //  ==== PLAY, left: clock, voice, activity ================================
    {
        auto clock = addSection ({ leftX, top, railWidth, 164 }, "CLOCK", playFace);
        modeSwitch.setBounds (clock.removeFromTop (46));
        gap (clock, 8);

        auto row = clock.removeFromTop (knobHeight);
        placeKnob (knobCell (row), rateCaption, rateKnob);
        placeKnob (knobCell (row), tempoCaption, tempoKnob);
        row.removeFromLeft (4);
        freeRunButton.setBounds (row.getX(), row.getY() + 14, ledWidth (freeRunButton), ledHeight);
        tieNotesButton.setBounds (row.getX(), row.getY() + 42, ledWidth (tieNotesButton), ledHeight);

        auto voice = addSection ({ leftX, top + 164 + sectionGap, railWidth, 228 }, "VOICE", playFace);
        row = voice.removeFromTop (knobHeight);
        placeKnob (knobCell (row), noteCaption, noteKnob);
        placeKnob (knobCell (row), gateCaption, gateKnob);
        placeKnob (knobCell (row), lifeCaption, lifeKnob);
        gap (voice, 6);
        row = voice.removeFromTop (knobHeight);
        placeKnob (knobCell (row), spreadCaption, spreadKnob);
        placeKnob (knobCell (row), blackVelocityCaption, blackVelocityKnob);
        placeKnob (knobCell (row), whiteVelocityCaption, whiteVelocityKnob);
        gap (voice, 10);
        row = voice.removeFromTop (segmentHeight);
        lifeModeSwitch.setBounds (row.removeFromRight (164));
        lifeModeCaption.setBounds (row);

        const int activityTop = top + 164 + sectionGap + 228 + sectionGap;
        auto lamps = addSection ({ leftX, activityTop, railWidth, bottom - activityTop }, "ACTIVITY", playFace);
        activity.setBounds (lamps.removeFromTop (48));
        gap (lamps, 8);
        activityLabel.setBounds (lamps.removeFromTop (18));
        gap (lamps, 8);
        lapLabel.setBounds (lamps);
    }

    //  ==== PATCH, left: routing, the port, the rules =========================
    {
        auto routing = addSection ({ leftX, top, railWidth, 150 }, "ROUTING", patchFace);
        routingCaption.setBounds (routing.removeFromTop (captionHeight));
        gap (routing, 6);

        auto bay = routing.removeFromTop (46);
        const int columns = 2 + headChannels;
        const float columnWidth = (float) bay.getWidth() / (float) columns;

        const auto placeCell = [&] (int column, juce::Label& caption, Knob& cell)
        {
            const int x = bay.getX() + juce::roundToInt ((float) column * columnWidth);
            const int w = juce::roundToInt (columnWidth) - 3;
            caption.setBounds (x, bay.getY(), w, captionHeight);
            cell.setBounds (x, bay.getY() + 16, w, 30);
        };

        placeCell (0, blackChannelCaption, blackChannelCell);
        placeCell (1, whiteChannelCaption, whiteChannelCell);

        for (int h = 0; h < headChannels; ++h)
            placeCell (2 + h, headChannelCaptions[(size_t) h], headChannelCells[(size_t) h]);

        gap (routing, 8);
        routingLabel.setBounds (routing);

        auto port = addSection ({ leftX, top + 150 + sectionGap, railWidth, 118 }, "MIDI OUT PORT", patchFace);
        portBox.setBounds (port.removeFromTop (controlHeight));
        gap (port, 8);
        portStatusLabel.setBounds (port);

        const int rulesTop = top + 150 + sectionGap + 118 + sectionGap;
        auto rules = addSection ({ leftX, rulesTop, railWidth, bottom - rulesTop }, "RULES", patchFace);
        auto row = rules.removeFromTop (ledHeight);
        koButton.setBounds (row.removeFromLeft (ledWidth (koButton)));
        row.removeFromLeft (24);
        selfCaptureButton.setBounds (row.removeFromLeft (ledWidth (selfCaptureButton)));
        gap (rules, 8);
        rulesLabel.setBounds (rules.removeFromTop (36));
    }

    //  ==== the board, on both faces ==========================================
    {
        board.setBounds (boardX, top, boardSide, boardSide);

        auto strip = juce::Rectangle<int> (boardX, top + boardSide + 10, boardSide, 28);
        sizeCaption.setBounds (strip.removeFromLeft (44));
        sizeSwitch.setBounds (strip.removeFromLeft (188));
        strip.removeFromLeft (14);
        placeCaption.setBounds (strip.removeFromLeft (44));
        placeSwitch.setBounds (strip.removeFromLeft (120));
        clearButton.setBounds (strip.removeFromRight (pillWidth (clearButton)).withSizeKeepingCentre (pillWidth (clearButton), controlHeight));

        hintLabel.setBounds (boardX, top + boardSide + 44, boardSide, 18);
    }

    //  ==== PLAY, right: the game record, the players =========================
    {
        auto game = addSection ({ rightX, top, rightWidth, 252 }, "GAME RECORD", playFace);
        gameWellBounds = game.removeFromTop (44);
        {
            auto well = gameWellBounds.reduced (10, 5);
            gameTitleLabel.setBounds (well.removeFromTop (17));
            gameDetailLabel.setBounds (well);
        }
        gap (game, 8);
        flow (game.removeFromTop (controlHeight), { &loadButton, &unloadButton });
        gap (game, 8);

        auto row = game.removeFromTop (knobHeight);
        placeKnob (knobCell (row), gameRateCaption, gameRateKnob);
        placeKnob (knobCell (row), waveGapCaption, waveGapKnob);
        row.removeFromLeft (4);
        runGameButton.setBounds    (row.getX(), row.getY() + 4,  ledWidth (runGameButton),    ledHeight);
        loopGameButton.setBounds   (row.getX(), row.getY() + 28, ledWidth (loopGameButton),   ledHeight);
        waveReplayButton.setBounds (row.getX(), row.getY() + 52, ledWidth (waveReplayButton), ledHeight);
        gap (game, 6);

        auto scrub = game.removeFromTop (40);
        auto stepper = scrub.removeFromRight (2 * stepButtonWidth + 4);
        scrub.removeFromRight (8);
        moveSlider.setBounds (scrub);
        moveCaption.setBounds (scrub.getX(), scrub.getY(), 70, valueHeight);
        stepper.removeFromTop (16);
        previousMoveButton.setBounds (stepper.removeFromLeft (stepButtonWidth).withHeight (controlHeight));
        stepper.removeFromLeft (4);
        nextMoveButton.setBounds (stepper.removeFromLeft (stepButtonWidth).withHeight (controlHeight));

        const int playersTop = top + 252 + sectionGap;
        auto players = addSection ({ rightX, playersTop, rightWidth, bottom - playersTop }, "PLAYERS", playFace);
        row = players.removeFromTop (segmentHeight);
        playersSwitch.setBounds (row.removeFromRight (136));
        aiPlayButton.setBounds (row.removeFromLeft (ledWidth (aiPlayButton)).withSizeKeepingCentre (ledWidth (aiPlayButton), ledHeight));
        gap (players, 8);

        row = players.removeFromTop (knobHeight);
        placeKnob (knobCell (row), aiMovesCaption, aiMovesKnob);
        placeKnob (knobCell (row), aiVariationCaption, aiVariationKnob);
        placeKnob (knobCell (row), aiSeedCaption, aiSeedKnob);
        gap (players, 8);

        row = players.removeFromTop (controlHeight);
        openingCaption.setBounds (row.removeFromLeft (70));
        flowRight (row, { &openingFromBoardButton, &openingBookButton });
        gap (players, 4);
        openingLabel.setBounds (players.removeFromTop (34));
        gap (players, 6);
        playersRuleY = players.getY();
        gap (players, 10);

        row = players.removeFromTop (segmentHeight);
        opponentSwitch.setBounds (row.removeFromRight (104));
        row.removeFromRight (6);
        opponentCaption.setBounds (row.removeFromRight (64));
        aiOpponentButton.setBounds (row.removeFromLeft (ledWidth (aiOpponentButton)).withSizeKeepingCentre (ledWidth (aiOpponentButton), ledHeight));
        gap (players, 8);
        flow (players.removeFromTop (controlHeight), { &passButton, &newMatchButton });
        gap (players, 6);
        matchLabel.setBounds (players.removeFromTop (36));
    }

    //  ==== PATCH, right: the Launchpad =======================================
    {
        auto pads = addSection ({ rightX, top, rightWidth, bottom - top }, "LAUNCHPAD X", patchFace);

        //  in and out side by side, since on Windows the two are named apart
        auto row = pads.removeFromTop (captionHeight + 2 + controlHeight);
        auto in = row.removeFromLeft ((row.getWidth() - 12) / 2);
        row.removeFromLeft (12);
        padsInCaption.setBounds (in.removeFromTop (captionHeight));
        padsInBox.setBounds (in.withTrimmedTop (2));
        padsOutCaption.setBounds (row.removeFromTop (captionHeight));
        padsOutBox.setBounds (row.withTrimmedTop (2));
        gap (pads, 10);

        flow (pads.removeFromTop (controlHeight), { &padsFindButton, &padsStopButton });
        gap (pads, 6);
        flow (pads.removeFromTop (controlHeight), { &padsSizeButton });
        gap (pads, 8);

        //  three lines, because the one that says the port is taken is long
        padsStatusLabel.setBounds (pads.removeFromTop (54));
        gap (pads, 8);
        padsDiagram.setBounds (pads);
    }
}

//==============================================================================
void GoSequencerEditor::refreshModeDisplay()
{
    const int heads = processor.headCount();
    const int size  = processor.boardSize();
    const bool multi = heads > 1;

    //  spread pushes the heads apart in pitch, so it has nothing to say until
    //  there is more than one of them; velocity follows the colour in every mode
    spreadKnob.setEnabled (multi);

    //  spiral routes by colour, the multi head modes by playhead, and a 9x9 has
    //  fewer rings than a 13x13 - so grey out what this mode is not using
    //  rather than let it look assignable
    blackChannelCell.setEnabled (! multi);
    whiteChannelCell.setEnabled (! multi);

    for (int h = 0; h < headChannels; ++h)
        headChannelCells[(size_t) h].setEnabled (multi && h < heads);

    const juce::String dot (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    juce::String lapText;

    if (processor.isPolyrhythm())
    {
        //  the rings are different lengths - which is the polyrhythm - so say
        //  how long each one is, and when they all meet again
        long long together = 1;

        for (int r = 0; r < heads; ++r)
        {
            const int length = go::ringLength (size, r);
            lapText << (r > 0 ? dot : juce::String()) << length;
            together = std::lcm (together, (long long) length);
        }

        lapText << "  steps a lap\nall heads line up again after " << juce::String (together) << " steps";
        activityLabel.setText (juce::String (heads) + " rings, each head on its own channel", juce::dontSendNotification);
    }
    else if (processor.isQuads())
    {
        lapText << "4 x " << go::quadSteps (size) << "  steps a lap\nthe four quadrants mirror one another";
        activityLabel.setText ("4 quadrants, each head on its own channel", juce::dontSendNotification);
    }
    else
    {
        lapText << size * size << "  steps a lap - the whole board";
        activityLabel.setText ("one head - notes go out by stone colour", juce::dontSendNotification);
    }

    lapLabel.setText (lapText, juce::dontSendNotification);
    lapLabel.setColour (juce::Label::textColourId, theme::ink);

    routingLabel.setText (multi ? "rings and quads route by playhead - black and white are not used in this mode"
                                : "spiral routes by stone colour - heads 1 to 9 are not used in this mode",
                          juce::dontSendNotification);
}

//==============================================================================
void GoSequencerEditor::timerCallback()
{
    if (messageCountdown > 0)
        --messageCountdown;

    if (messageCountdown == 0 && message.isNotEmpty())
        message.clear();

    //  keyed on the mode itself, not on how many heads it runs: quads out, quads
    //  in and poly on a 9x9 all have four, and each greys out different things
    {
        const int modeKey = (processor.isPolyrhythm() ? 1 : 0) + (processor.isQuads() ? 2 : 0)
                          + (processor.quadsWindOut() ? 4 : 0) + 8 * processor.boardSize();

        if (modeKey != lastModeKeyShown)
        {
            lastModeKeyShown = modeKey;
            refreshModeDisplay();
        }
    }

    const bool waveReplay = processor.waveReplayOn();

    if (waveReplay != lastWaveReplayShown)
    {
        lastWaveReplayShown = waveReplay;

        //  Wave Gap only means anything once Wave Replay is on. Loop still
        //  does - it wraps the record without clearing the board, which is
        //  what keeps the wave running - so that switch stays live.
        waveGapKnob.setEnabled (waveReplay);
    }

    //  A run replaces its own record every time a game ends, and that happens on
    //  the audio thread with nothing clicked - so the title line, the position
    //  slider's range and the enables are read back here rather than only after
    //  a button.
    const bool ai = processor.aiSelfPlay();
    const int aiGame = ai ? processor.aiGameNumber() : -1;

    if (ai != lastAiShown || aiGame != lastAiGameShown)
    {
        lastAiShown = ai;
        lastAiGameShown = aiGame;

        for (auto* knob : { &aiMovesKnob, &aiVariationKnob, &aiSeedKnob })
            knob->setEnabled (ai);

        playersSwitch.setEnabled (ai);

        refreshGameDisplay();
    }

    //  stones are placed by clicking the board, which the editor does not hear
    //  about, so the opening line and its buttons are re-read here
    {
        const int played = processor.handPlayedCount();
        auto line = processor.openingDescription();

        if (played > 0 && played < GoSequencerProcessor::openingLength)
            line << "  (" << played << " of " << GoSequencerProcessor::openingLength << " played)";

        if (line != lastOpeningShown)
            refreshOpeningDisplay();
    }

    //  their answer lands with nothing clicked, and so does the end of a game,
    //  so whose move it is is read back here rather than only after a button
    {
        const int turn = ! processor.matchActive() ? -1
                       : processor.matchIsOver()   ?  2
                       : processor.yourTurn()      ?  0 : 1;

        if (turn != lastTurnShown)
        {
            lastTurnShown = turn;
            refreshMatchDisplay();
            board.repaint();
        }
    }

    //  a port can open or go away with nothing clicked - loopMIDI started or
    //  quit - so the dropdown and its line are read again when that happens,
    //  though never from under an open popup
    if (processor.midiOutPortOpen() != lastPortOpenShown && ! portBox.isPopupActive())
        refreshPortList();

    //  the same for the Launchpad, which can also be taken by another program -
    //  and whose pads go dark on a board of another size, so that is watched too
    {
        const int size = processor.boardSize();

        if (size != lastBoardSizeShown)
        {
            lastBoardSizeShown = size;
            board.refreshAll();
        }

        //  Listing the ports is a round trip to the system's MIDI service, slow
        //  enough to feel, so the lists are only read again when the Launchpad
        //  comes or goes; a new status or board size just rewrites the line.
        if (! padsInBox.isPopupActive() && ! padsOutBox.isPopupActive())
        {
            if (processor.launchpadOpen() != lastPadsOpenShown)
                refreshPadsLists();
            else if (size != lastPadsSizeShown || processor.launchpad().status() != lastPadsStatusShown)
                refreshPadsStatus();
        }
    }

    const int position = processor.gamePosition();

    if (position != lastMoveShown)
    {
        lastMoveShown = position;

        if (! moveSlider.isMouseButtonDown())
            moveSlider.setValue ((double) position, juce::dontSendNotification);
    }

    //  the top bar only when something in it reads differently, not on every tick
    juce::String header = message;

    for (const auto& field : displayFields (processor))
        header << "|" << field.second;

    header << (processor.isRunning() ? "|run" : "|stop")
           << (processor.colourForNextMove() == go::Stone::black ? "|b" : "|w");

    if (header != lastHeaderShown)
    {
        lastHeaderShown = header;
        plate.repaint (topBarBounds);
    }
}

void GoSequencerEditor::paint (juce::Graphics& g)
{
    //  the plate covers the window; this only shows in a rounding gap at its edge
    g.fillAll (theme::background);
}

void GoSequencerEditor::paintPlate (juce::Graphics& g)
{
    g.fillAll (theme::background);

    //  four screws, the one ornament
    for (auto corner : { juce::Point<float> (11.5f, 11.5f), { (float) Faceplate::width - 11.5f, 11.5f },
                         { 11.5f, (float) Faceplate::height - 11.5f },
                         { (float) Faceplate::width - 11.5f, (float) Faceplate::height - 11.5f } })
    {
        const auto head = juce::Rectangle<float> (9.0f, 9.0f).withCentre (corner);
        g.setColour (theme::well);
        g.fillEllipse (head);
        g.setColour (theme::hairline);
        g.drawEllipse (head, 1.0f);
        g.setColour (theme::faintText);
        g.drawLine (juce::Line<float> (corner.translated (-3.0f, 2.0f), corner.translated (3.0f, -2.0f)), 1.0f);
    }

    //  ---- the wordmark, and the stone the next click will place
    {
        auto bar = topBarBounds;
        const juce::String title ("GO SEQUENCER");
        const int width = textWidth (titleFont(), title) + 2;

        g.setFont (titleFont());
        g.setColour (theme::ink);
        g.drawText (title, bar.removeFromLeft (width), juce::Justification::centredLeft, false);

        bar.removeFromLeft (10);
        BoardComponent::drawStone (g, bar.removeFromLeft (12).getCentre().toFloat(), 5.5f,
                                   processor.colourForNextMove() == go::Stone::black);
    }

    paintDisplay (g, displayBounds.toFloat());

    //  ---- the section frames, each with its name cut into the top edge
    for (const auto& section : sections)
    {
        if (section.face != currentFace && section.face != pinned)
            continue;

        g.setColour (theme::hairline);
        g.drawRoundedRectangle (section.bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

        const int width = textWidth (sectionFont(), section.title) + 12;
        const auto label = juce::Rectangle<int> (section.bounds.getX() + 10, section.bounds.getY() - 8, width, 16);

        g.setColour (theme::background);
        g.fillRect (label);
        g.setFont (sectionFont());
        g.setColour (theme::dimText);
        g.drawText (section.title, label, juce::Justification::centred, false);
    }

    if (currentFace == playFace)
    {
        //  the game record's title sits in a recessed well, like a read-out
        g.setColour (theme::well);
        g.fillRoundedRectangle (gameWellBounds.toFloat(), 5.0f);

        //  self-play above the rule, playing against them below it
        g.setColour (theme::hairline);
        g.fillRect (juce::Rectangle<int> (gameWellBounds.getX(), playersRuleY, gameWellBounds.getWidth(), 1));
    }

    if (dragHighlight)
    {
        g.setColour (theme::accent);
        g.drawRect (plate.getLocalBounds(), 2);
    }
}

void GoSequencerEditor::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    auto inner = area.reduced (8.0f, 0.0f);

    if (message.isNotEmpty())
    {
        g.setFont (valueFont());
        g.setColour (theme::accent);
        g.drawFittedText (message, inner.reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        return;
    }

    //  the transport, at the right hand end: a lamp and a word
    {
        const bool running = processor.isRunning();
        const juce::String word = running ? "running" : "stopped";
        const auto font = theme::monoFont (12.5f);
        auto slot = inner.removeFromRight ((float) textWidth (font, word) + 24.0f);

        const auto lamp = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ slot.getX() + 6.0f, slot.getCentreY() });

        if (running)
        {
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillEllipse (lamp.expanded (2.5f));
        }

        g.setColour (running ? theme::accent : theme::lcdDim);
        g.fillEllipse (lamp);

        g.setFont (font);
        g.setColour (theme::lcdInk);
        g.drawText (word, slot.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
    }

    const auto keyFont = captionFont().withHeight (9.5f);
    const auto valueFontMono = theme::monoFont (12.5f);
    float x = inner.getX();

    for (const auto& [key, value] : displayFields (processor))
    {
        x += 12.0f;
        const float keyWidth = (float) textWidth (keyFont, key);
        const float valueWidth = (float) textWidth (valueFontMono, value);

        if (x + keyWidth + 6.0f + valueWidth > inner.getRight())
            break;

        g.setFont (keyFont);
        g.setColour (theme::lcdDim);
        g.drawText (key, juce::Rectangle<float> (x, area.getY(), keyWidth + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += keyWidth + 6.0f;

        g.setFont (valueFontMono);
        g.setColour (theme::lcdInk);
        g.drawText (value, juce::Rectangle<float> (x, area.getY(), valueWidth + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += valueWidth + 12.0f;

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x, area.getY() + 9.0f, 1.0f, area.getHeight() - 18.0f));
    }
}
