#include "PluginEditor.h"
#include "ValueText.h"

#include <cmath>

//==============================================================================
namespace
{
    constexpr int captionHeight = 14, valueHeight = 17;
    constexpr int knobWidth = 84, knobHeight = 76;          //  caption, dial, value - a switch cell is the same size
    constexpr int buttonHeight = 24, smallButtonHeight = 20, segmentHeight = 26, glyphButtonWidth = 30;
    constexpr int buttonGap = 6, lineHeight = 17;
    constexpr int sectionPadX = 14, sectionPadTop = 19, sectionPadBottom = 13, sectionGap = 14;

    //  the open face rides along in the state, so a session comes back on it
    const juce::Identifier panelFaceProperty { "panelFace" };

    //  so does the light/dark choice
    const juce::Identifier darkModeProperty { "darkMode" };

    //  how a button is drawn: a toggle switch - "cell" or "inline" - rather than
    //  an outlined pill; and a pill that is the small kind
    const juce::Identifier switchProperty { "goSwitch" };
    const juce::Identifier smallProperty  { "goSmall" };

    //  a dropdown drawn as a box, not on a rule: the Launchpad's, over its pads
    const juce::Identifier boxedProperty { "goBoxed" };

    //  how a knob is drawn: from the middle (spread), or with a tick per choice
    const juce::Identifier bipolarProperty { "goBipolar" };
    const juce::Identifier detentProperty  { "goDetents" };

    //  the separators and symbols are UTF-8: JUCE must be told, or they arrive as Latin-1
    juce::String utf8 (const char* text) { return juce::String (juce::CharPointer_UTF8 (text)); }

    //  sizes are CSS px, as in the faceplate's web mock - see theme::font
    juce::Font captionFont()    { return theme::font (10.5f, juce::Font::plain, 0.08f); }
    juce::Font valueFont()      { return theme::font (12.5f); }
    juce::Font lineFont()       { return theme::font (12.0f); }
    juce::Font pillFont()       { return theme::font (11.0f, juce::Font::plain, 0.05f); }
    juce::Font smallPillFont()  { return theme::font (10.0f, juce::Font::plain, 0.05f); }
    juce::Font switchFont()     { return theme::font (12.5f); }
    juce::Font stateFont()      { return theme::font (11.0f, juce::Font::bold, 0.14f); }
    juce::Font titleFont()      { return theme::font (24.0f, juce::Font::bold, 0.16f); }
    juce::Font sectionFont()    { return theme::font (10.5f, juce::Font::bold, 0.18f); }
    juce::Font jobFont()        { return theme::font (10.5f); }
    juce::Font columnFont()     { return theme::font (10.0f, juce::Font::bold, 0.14f); }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    bool isSmall (const juce::Button& button)
    {
        return button.getProperties().contains (smallProperty);
    }

    //  outlined buttons are as wide as what they say, not as wide as the space
    int pillWidth (const juce::Button& button)
    {
        const bool small = isSmall (button);
        return textWidth (small ? smallPillFont() : pillFont(), button.getButtonText().toUpperCase()) + (small ? 18 : 22);
    }

    int pillHeight (const juce::Button& button)
    {
        return isSmall (button) ? smallButtonHeight : buttonHeight;
    }

    //  an inline switch: its track, a gap, its name
    int switchWidth (const juce::Button& button)
    {
        return 30 + 8 + textWidth (switchFont(), button.getButtonText()) + 2;
    }

    /** Buttons side by side from the left, each at its own width - or a little
        narrower each, when together they would not fit the row. */
    void flow (juce::Rectangle<int> row, std::initializer_list<juce::Button*> buttons)
    {
        const std::vector<juce::Button*> list (buttons);
        int total = buttonGap * ((int) list.size() - 1);

        for (auto* button : list)
            total += pillWidth (*button);

        const int excess = juce::jmax (0, total - row.getWidth());
        int shaved = 0;

        for (size_t i = 0; i < list.size(); ++i)
        {
            const int cut = excess * (int) (i + 1) / (int) list.size() - shaved;
            shaved += cut;

            const int w = pillWidth (*list[i]) - cut;
            list[i]->setBounds (row.removeFromLeft (w).withSizeKeepingCentre (w, pillHeight (*list[i])));
            row.removeFromLeft (buttonGap);
        }
    }

    /** The same, packed against the right hand edge. */
    void flowRight (juce::Rectangle<int> row, std::initializer_list<juce::Button*> buttons)
    {
        std::vector<juce::Button*> list (buttons);

        for (auto it = list.rbegin(); it != list.rend(); ++it)
        {
            const int w = pillWidth (**it);
            (*it)->setBounds (row.removeFromRight (w).withSizeKeepingCentre (w, pillHeight (**it)));
            row.removeFromRight (buttonGap);
        }
    }

    /** Three knob-sized cells across a row, the outer two against its edges. */
    std::array<juce::Rectangle<int>, 3> threeCells (juce::Rectangle<int> row)
    {
        const int spare = juce::jmax (0, row.getWidth() - 3 * knobWidth);
        std::array<juce::Rectangle<int>, 3> cells;

        for (int i = 0; i < 3; ++i)
            cells[(size_t) i] = { row.getX() + i * knobWidth + spare * i / 2, row.getY(), knobWidth, knobHeight };

        return cells;
    }

    /** A knob in a cell: its caption over it, its value under it. */
    void placeKnob (juce::Rectangle<int> cell, juce::Label& caption, juce::Slider& knob)
    {
        caption.setBounds (cell.removeFromTop (captionHeight));
        knob.setBounds (cell.removeFromTop (knobHeight - captionHeight));
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

    /** A dashed line, three on and three off, as the web's 1 px dashed border. */
    void dashedLine (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, float thickness)
    {
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ from, to }, dashes, 2, thickness);
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

void GoLookAndFeel::drawSwitch (juce::Graphics& g, juce::Button& button, bool cell, bool highlighted)
{
    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    const float alpha = enabled ? 1.0f : 0.45f;
    const auto bounds = button.getLocalBounds().toFloat();

    juce::Rectangle<float> track;
    float inset;

    if (cell)
    {
        //  the name, the switch, and the state in words under it
        g.setFont (captionFont());
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.drawText (button.getButtonText().toUpperCase(), bounds.withHeight ((float) captionHeight),
                    juce::Justification::centred, false);

        track = { bounds.getCentreX() - 21.0f, (float) captionHeight + 11.0f, 42.0f, 24.0f };
        inset = 3.0f;

        g.setFont (stateFont());
        g.setColour ((on ? theme::accent : theme::faintText).withMultipliedAlpha (alpha));
        g.drawText (on ? "ON" : "OFF", juce::Rectangle<float> (0.0f, track.getBottom() + 7.0f, bounds.getWidth(), (float) valueHeight),
                    juce::Justification::centred, false);
    }
    else
    {
        track = { 0.0f, std::floor (bounds.getCentreY()) - 8.0f, 30.0f, 16.0f };
        inset = 2.0f;

        g.setFont (switchFont());
        g.setColour (theme::ink.withMultipliedAlpha (alpha));
        g.drawText (button.getButtonText(), bounds.withTrimmedLeft (38.0f), juce::Justification::centredLeft, false);
    }

    const float radius = track.getHeight() * 0.5f;

    g.setColour ((on ? theme::accent : theme::track).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (track, radius);
    g.setColour ((on ? theme::accent : (highlighted && enabled ? theme::faintText : theme::hairline)).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (track.reduced (0.5f), radius - 0.5f, 1.0f);

    const float diameter = track.getHeight() - 2.0f * inset;
    const float x = on ? track.getRight() - inset - diameter : track.getX() + inset;

    g.setColour ((on ? juce::Colours::white : theme::thumb).withMultipliedAlpha (alpha));
    g.fillEllipse (x, track.getY() + inset, diameter, diameter);

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (cell ? bounds.reduced (0.5f) : track.expanded (2.5f), cell ? 4.0f : radius + 2.5f, 1.0f);
    }
}

void GoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    const auto kind = button.getProperties()[switchProperty].toString();

    if (kind.isNotEmpty())
    {
        drawSwitch (g, button, kind == "cell", shouldDrawButtonAsHighlighted);
        return;
    }

    const auto face = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool enabled = button.isEnabled();
    const float alpha = enabled ? 1.0f : 0.45f;

    if (button.getToggleState())
    {
        g.setColour (theme::accent.withMultipliedAlpha (! enabled ? 0.45f : (shouldDrawButtonAsDown ? 0.85f : 1.0f)));
        g.fillRoundedRectangle (face, 4.0f);
    }
    else
    {
        if (shouldDrawButtonAsDown && enabled)
        {
            g.setColour (theme::boardFill);
            g.fillRoundedRectangle (face, 4.0f);
        }

        g.setColour ((shouldDrawButtonAsHighlighted && enabled ? theme::faintText : theme::hairline).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (face, 4.0f, 1.0f);
    }

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (face.expanded (2.0f), 5.0f, 1.0f);
    }
}

juce::Font GoLookAndFeel::getTextButtonFont (juce::TextButton& button, int)
{
    return isSmall (button) ? smallPillFont() : pillFont();
}

void GoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    bool shouldDrawButtonAsHighlighted, bool)
{
    //  a switch draws its words with its track
    if (button.getProperties().contains (switchProperty))
        return;

    const bool on = button.getToggleState();
    const bool enabled = button.isEnabled();
    const auto text = button.getButtonText().toUpperCase();

    auto colour = on ? theme::background : (shouldDrawButtonAsHighlighted && enabled ? theme::ink : theme::dimText);

    if (! enabled && ! on)
        colour = colour.withMultipliedAlpha (0.45f);

    //  a single glyph - the step arrows - is a symbol, not a word: give it some size
    g.setFont (text.length() == 1 ? theme::font (17.0f) : getTextButtonFont (button, button.getHeight()));
    g.setColour (colour);
    g.drawFittedText (text, button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
}

void GoLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                  int, int, int, int, juce::ComboBox& box)
{
    const float alpha = box.isEnabled() ? 1.0f : 0.5f;

    if (box.getProperties().contains (boxedProperty))
    {
        //  over the Launchpad's pads: a box of its own, so the pads do not show through
        const auto face = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
        g.setColour (theme::background);
        g.fillRoundedRectangle (face, 4.0f);
        g.setColour (box.hasKeyboardFocus (true) ? theme::ink : theme::hairline);
        g.drawRoundedRectangle (face, 4.0f, 1.0f);
    }
    else
    {
        //  no box: the rule it sits on, and a chevron
        g.setColour (box.hasKeyboardFocus (true) ? theme::ink : theme::hairline);
        g.fillRect (0.0f, (float) height - 1.0f, (float) width, 1.0f);
    }

    const float cx = (float) width - 7.0f;
    const float cy = (float) (height - 1) * 0.5f;

    juce::Path chevron;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.75f);
    chevron.lineTo (cx, cy + 1.75f);
    chevron.lineTo (cx + 3.5f, cy - 1.75f);

    g.setColour (theme::faintText.withMultipliedAlpha (alpha));
    g.strokePath (chevron, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font GoLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return valueFont();
}

void GoLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    //  flush left, so the value lines up with the caption above it - or, in a
    //  box, just inside its edge
    const int left = box.getProperties().contains (boxedProperty) ? 5 : 0;

    label.setBounds (left, 0, box.getWidth() - left - 16, box.getHeight() - 1);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setFont (getComboBoxFont (box));
    label.setMinimumHorizontalScale (0.75f);
}

juce::Font GoLookAndFeel::getPopupMenuFont()
{
    return theme::font (13.0f);
}

void GoLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float sliderPos, float minSliderPos, float maxSliderPos,
                                      juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const bool enabled = slider.isEnabled();

    if (style == juce::Slider::LinearBarVertical)
    {
        //  a channel cell: a box round its number, the number being the text box
        const auto box = slider.getLocalBounds().toFloat().reduced (0.5f);

        g.setColour (theme::well);
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (enabled && slider.isMouseOverOrDragging() ? theme::faintText : theme::hairline);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        return;
    }

    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    //  the record's position: a line filled up to where the record stands and a
    //  slim mark on it; with no record loaded, a dashed line and no mark
    const float centreY = (float) y + (float) height * 0.5f;
    const float right = (float) slider.getWidth();

    if (! enabled)
    {
        g.setColour (theme::hairline);

        for (float dash = 0.0f; dash < right; dash += 8.0f)
            g.fillRect (dash, centreY - 1.0f, juce::jmin (4.0f, right - dash), 2.0f);

        return;
    }

    g.setColour (theme::hairline);
    g.fillRect (0.0f, centreY - 1.0f, right, 2.0f);

    g.setColour (theme::ink);
    g.fillRect (0.0f, centreY - 1.0f, juce::jlimit (0.0f, right, sliderPos), 2.0f);
    g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, 14.0f).withCentre ({ sliderPos, centreY }), 1.0f);
}

void GoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                      float position, float startAngle, float endAngle, juce::Slider& slider)
{
    //  a knob that has nothing to do is faded as a whole, caption and all, by
    //  GoSequencerEditor::muteKnob - so this always draws at full strength
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float radius = juce::jmin (16.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 3.0f);
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

        g.setColour (theme::faintText);

        for (int i = 0; i <= steps; ++i)
        {
            const float a = startAngle + (float) i / (float) juce::jmax (1, steps) * (endAngle - startAngle);
            g.drawLine (juce::Line<float> (pointAt (a, radius + 3.5f), pointAt (a, radius + 6.5f)), 1.0f);
        }
    }

    const juce::PathStrokeType track (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path rail;
    rail.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (theme::hairline);
    g.strokePath (rail, track);

    //  the value: from the start, or from the middle on a knob that goes both ways
    const float from = slider.getProperties().contains (bipolarProperty) ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (theme::ink);
        g.strokePath (fill, track);
    }

    const auto face = juce::Rectangle<float> (22.0f, 22.0f).withCentre (centre);
    g.setColour (theme::well);
    g.fillEllipse (face);
    g.setColour (slider.isMouseOverOrDragging() && slider.isEnabled() ? theme::faintText : theme::hairline);
    g.drawEllipse (face.reduced (0.5f), 1.0f);

    juce::Path needle;
    needle.startNewSubPath (pointAt (angle, 3.0f));
    needle.lineTo (pointAt (angle, 9.0f));
    g.setColour (theme::ink);
    g.strokePath (needle, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

int GoLookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    //  the record's mark is a slim bar, and runs nearly to both ends
    return slider.getSliderStyle() == juce::Slider::LinearHorizontal ? 2 : 5;
}

juce::Slider::SliderLayout GoLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (slider.getTextBoxPosition() != juce::Slider::TextBoxAbove)
        return LookAndFeel_V4::getSliderLayout (slider);

    //  the value on the caption's line, right aligned; the line itself under it
    auto bounds = slider.getLocalBounds();
    auto valueLine = bounds.removeFromTop (18);

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

juce::Rectangle<float> SegmentedChoice::segment (int i) const
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const float count = (float) juce::jmax (1, (int) items.size());

    if (vertical)
    {
        const float h = bounds.getHeight() / count;
        return { bounds.getX(), bounds.getY() + (float) i * h, bounds.getWidth(), h };
    }

    const float w = bounds.getWidth() / count;
    return { bounds.getX() + (float) i * w, bounds.getY(), w, bounds.getHeight() };
}

int SegmentedChoice::itemAt (juce::Point<int> p) const
{
    if (items.empty() || ! getLocalBounds().contains (p))
        return -1;

    const int along = vertical ? p.y : p.x, length = vertical ? getHeight() : getWidth();
    return juce::jlimit (0, (int) items.size() - 1, along * (int) items.size() / juce::jmax (1, length));
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
    const float alpha = isEnabled() ? 1.0f : 0.4f;

    juce::Path outline;
    outline.addRoundedRectangle (bounds, 4.0f);

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
    {
        const auto s = segment (i);

        if (vertical)
            g.fillRect (juce::Rectangle<float> (bounds.getX(), s.getY() - 0.5f, bounds.getWidth(), 1.0f));
        else
            g.fillRect (juce::Rectangle<float> (s.getX() - 0.5f, bounds.getY(), 1.0f, bounds.getHeight()));
    }

    for (int i = 0; i < count; ++i)
    {
        const bool on = items[(size_t) i].value == selected;
        auto colour = on ? theme::background : (i == hover && isEnabled() ? theme::ink : theme::dimText);
        g.setColour (colour.withMultipliedAlpha (on ? 1.0f : alpha));

        const auto area = segment (i);

        if (drawIcon != nullptr)
        {
            //  a picture over a small word: 6 px in, 22 px of picture, 3 px, the word
            drawIcon (g, juce::Rectangle<float> (area.getX(), area.getY() + 5.0f, area.getWidth(), 22.0f), items[(size_t) i].value);
            g.setFont (theme::font (10.5f, juce::Font::plain, 0.02f));
            g.drawFittedText (items[(size_t) i].text,
                              juce::Rectangle<float> (area.getX() + 2.0f, area.getY() + 30.0f, area.getWidth() - 4.0f, 15.0f).toNearestInt(),
                              juce::Justification::centred, 1, 0.8f);
        }
        else
        {
            g.setFont (theme::font (textSize, juce::Font::plain, textTracking));
            g.drawFittedText (items[(size_t) i].text, area.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        }
    }
}

//==============================================================================
ActivityLamps::ActivityLamps (GoSequencerProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (false, false);
    lastPosition.fill (-1);
    startTimerHz (30);
}

juce::Rectangle<int> ActivityLamps::column (int voice) const
{
    const float w = (float) (getWidth() - gap * (voices - 1)) / (float) voices;
    const float left = (float) voice * (w + (float) gap);
    const int x0 = juce::roundToInt (left), x1 = juce::roundToInt (left + w);

    return { x0, 0, x1 - x0, getHeight() };
}

bool ActivityLamps::inUse (int voice) const noexcept
{
    const int heads = processor.headCount();

    //  spiral routes by colour, the multi head modes by playhead
    return heads > 1 ? (voice >= 2 && voice - 2 < heads) : voice < 2;
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

    for (auto& l : level)
    {
        if (l > 0.02f)       { l *= 0.6f; dirty = true; }
        else if (l > 0.0f)   { l = 0.0f;  dirty = true; }
    }

    if (dirty)
        repaint();
}

void ActivityLamps::paint (juce::Graphics& g)
{
    for (int v = 0; v < voices; ++v)
    {
        const float alpha = inUse (v) ? 1.0f : 0.3f;
        auto area = column (v).toFloat();

        const juce::String who = v == 0 ? utf8 ("\xe2\x97\x8f") : v == 1 ? utf8 ("\xe2\x97\x8b") : juce::String (v - 1);

        g.setFont (theme::font (11.0f));
        g.setColour (theme::dimText.withMultipliedAlpha (alpha));
        g.drawText (who, area.removeFromTop (15.0f), juce::Justification::centred, false);

        const auto lamp = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ area.getCentreX(), area.getY() + 8.0f });
        const float lit = level[(size_t) v];

        if (lit > 0.0f)
        {
            g.setColour (theme::accent.withAlpha (0.3f * lit));
            g.fillEllipse (lamp.expanded (4.0f));
            g.setColour (theme::accent.withAlpha (lit));
            g.fillEllipse (lamp);
        }

        g.setColour ((lit > 0.5f ? theme::accent : theme::faintText).withMultipliedAlpha (alpha));
        g.drawEllipse (lamp.reduced (0.65f), 1.3f);
    }
}

//==============================================================================
void RoutingTable::refresh()
{
    const auto channel = [this] (const juce::String& id)
    {
        auto* value = processor.apvts.getRawParameterValue (id);
        return value != nullptr ? juce::roundToInt (value->load (std::memory_order_relaxed)) : 0;
    };

    const auto heads = [&] (int count)
    {
        std::vector<int> list;

        for (int h = 0; h < count; ++h)
            list.push_back (channel ("headChannel" + juce::String (h + 1)));

        //  three or more in a row read as one run - 1-6, not 1 · 2 · 3 · 4 · 5 · 6 -
        //  so nine rings still fit on a line or two
        juce::StringArray parts;

        for (size_t i = 0; i < list.size();)
        {
            size_t j = i;

            while (j + 1 < list.size() && list[j + 1] == list[j] + 1)
                ++j;

            if (j - i >= 2)
            {
                parts.add (juce::String (list[i]) + utf8 ("\xe2\x80\x93") + juce::String (list[j]));
                i = j + 1;
            }
            else
            {
                parts.add (juce::String (list[i++]));
            }
        }

        return parts.joinIntoString (utf8 (" \xc2\xb7 "));
    };

    //  a no-break space keeps "ch" with its number when a line wraps
    const auto ch = utf8 ("ch\xc2\xa0");
    const int rings = processor.ringCount();
    const bool poly = processor.isPolyrhythm(), quads = processor.isQuads();

    const std::array<Row, 3> now
    {{
        { "spiral",     utf8 ("\xe2\x97\x8f ") + ch + juce::String (channel ("blackChannel"))
                          + utf8 ("  \xc2\xb7  \xe2\x97\x8b ") + ch + juce::String (channel ("whiteChannel")),
                        ! poly && ! quads },
        { "polyrhythm", utf8 ("rings 1\xe2\x80\x93") + juce::String (rings) + utf8 (" \xe2\x86\x92 ") + ch + heads (rings), poly },
        { "quads",      utf8 ("quadrants 1\xe2\x80\x93" "4 \xe2\x86\x92 ") + ch + heads (4), quads },
    }};

    bool changed = false;

    for (size_t i = 0; i < rows.size(); ++i)
        changed = changed || rows[i].text != now[i].text || rows[i].on != now[i].on;

    if (changed)
    {
        rows = now;
        repaint();
    }
}

void RoutingTable::paint (juce::Graphics& g)
{
    //  the mode on the left, with a dot if it is the one playing; what goes
    //  where on the right, wrapped when nine rings will not fit on a line
    constexpr float keyWidth = 86.0f, gapWidth = 10.0f;
    const float textWidth = (float) getWidth() - keyWidth - gapWidth;
    float y = 0.0f;

    for (const auto& row : rows)
    {
        const auto colour = row.on ? theme::ink : theme::dimText;

        juce::AttributedString text;
        text.setWordWrap (juce::AttributedString::byWord);
        text.append (row.text, lineFont(), colour);

        juce::TextLayout layout;
        layout.createLayout (text, textWidth);
        const float height = juce::jmax ((float) lineHeight, layout.getHeight());

        if (row.on)
        {
            g.setColour (theme::accent);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ 3.0f, y + (float) lineHeight * 0.5f }));
        }

        g.setFont (lineFont());
        g.setColour (colour);
        g.drawText (row.mode, juce::Rectangle<float> (12.0f, y, keyWidth - 12.0f, (float) lineHeight), juce::Justification::centredLeft, false);

        layout.draw (g, { keyWidth + gapWidth, y + 0.5f, textWidth, height });
        y += height + 5.0f;
    }
}

//==============================================================================
juce::Rectangle<int> LaunchpadDiagram::leftHalf()
{
    const int x = 1 + padding, y = labelsHeight + labelsGap + 1 + padding;
    return { x, y + pitch, 4 * pitch, 8 * pitch };
}

void LaunchpadDiagram::paint (juce::Graphics& g)
{
    //  The top row left to right, then the right hand column top to bottom -
    //  the order LaunchpadSurface::handleButton gives them their jobs in.
    static const char* const topJobs[8]  = { "rate +", "rate \xe2\x88\x92", "step back", "step on",
                                             "run game", "free run", "place", "hold: clear" };
    static const char* const sideJobs[8] = { "play vs AI", "pass", "lift last", "loop",
                                             "wave replay", "move rate +", "move rate \xe2\x88\x92", "redraw" };

    const float sketchY = (float) (labelsHeight + labelsGap);
    const float gx = 1.0f + (float) padding, gy = sketchY + 1.0f + (float) padding;
    const float p = (float) pitch;

    const auto cell = [&] (int col, int row)
    {
        return juce::Rectangle<float> (gx + (float) col * p, gy + (float) row * p, p, p);
    };

    //  up, down, left, right - the four arrows printed on the device - drawn
    //  rather than set in type, so no font has to have them
    const auto arrow = [&] (juce::Point<float> centre, float size, int direction)
    {
        static constexpr float turns[4] = { 0.0f, 1.0f, -0.5f, 0.5f };
        juce::Path path;
        path.addTriangle (0.0f, -size, size, size * 0.7f, -size, size * 0.7f);
        path.applyTransform (juce::AffineTransform::rotation (turns[direction] * juce::MathConstants<float>::pi)
                                .translated (centre.x, centre.y));
        g.fillPath (path);
    };

    //  ---- above the sketch: each top button's job, slanted, starting over its key
    {
        const float baseline = (float) labelsHeight - 2.0f;
        const auto font = theme::font (11.5f);
        g.setFont (font);
        g.setColour (theme::ink);

        for (int i = 0; i < 8; ++i)
        {
            const float x = 17.0f + p * (float) i;
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-55.0f), x, baseline));
            g.drawText (utf8 (topJobs[i]), juce::Rectangle<float> (x, baseline - 11.5f, 120.0f, 11.5f),
                        juce::Justification::centredLeft, false);
        }
    }

    //  ---- the device
    const auto body = juce::Rectangle<float> (0.0f, sketchY, (float) sketchSide, (float) sketchSide);
    g.setColour (theme::hairline);
    g.drawRoundedRectangle (body.reduced (0.5f), 9.0f, 1.0f);

    //  the function buttons, outlined in the accent: the part to read
    const auto key = [&] (juce::Rectangle<float> face, const juce::String& text, int arrowDirection)
    {
        g.setColour (theme::accent.withAlpha (0.14f));
        g.fillRoundedRectangle (face, 4.0f);
        g.setColour (theme::accent);
        g.drawRoundedRectangle (face.reduced (0.6f), 4.0f, 1.2f);

        if (arrowDirection >= 0)
        {
            arrow (face.getCentre(), 4.0f, arrowDirection);
            return;
        }

        g.setFont (theme::font (10.0f, juce::Font::bold));
        g.drawText (text, face, juce::Justification::centred, false);
    };

    for (int i = 0; i < 8; ++i)
    {
        key (cell (i, 0).reduced (3.0f), juce::String (i + 1), i < 4 ? i : -1);
        key (cell (8, i + 1).reduced (3.0f), juce::String (i + 1), -1);
    }

    //  the logo, top right, which does nothing here
    g.setColour (theme::faintText);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (cell (8, 0).getCentre()));

    //  the pads, faintly
    g.setColour (theme::ink.withAlpha (0.06f));

    for (int row = 1; row <= 8; ++row)
        for (int col = 0; col < 8; ++col)
            g.fillRoundedRectangle (cell (col, row).reduced (4.0f), 3.0f);

    //  the halves: the ports on the left, the side buttons' jobs on the right
    g.setColour (theme::hairline);
    dashedLine (g, { gx + 4.0f * p - 0.5f, gy + p }, { gx + 4.0f * p - 0.5f, gy + 9.0f * p }, 1.0f);

    g.setFont (theme::font (11.5f));

    for (int r = 0; r < 8; ++r)
    {
        auto half = juce::Rectangle<float> (gx + 4.0f * p, gy + (float) (r + 1) * p, 4.0f * p, p).withTrimmedRight (3.0f);

        //  a pointer at the button in this row
        const auto pointer = half.removeFromRight (6.0f);
        juce::Path triangle;
        triangle.addTriangle (pointer.getX(), pointer.getCentreY() - 4.0f, pointer.getRight(), pointer.getCentreY(),
                              pointer.getX(), pointer.getCentreY() + 4.0f);
        g.setColour (theme::accent);
        g.fillPath (triangle);

        half.removeFromRight (4.0f);
        g.setColour (theme::ink);
        g.drawFittedText (utf8 (sideJobs[r]), half.toNearestInt(), juce::Justification::centredRight, 1, 0.85f);
    }
}

//==============================================================================
GoSequencerEditor::GoSequencerEditor (GoSequencerProcessor& p)
    : AudioProcessorEditor (&p), processor (p), board (p), activity (p), routingTable (p)
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
    faceSwitch.setTextStyle (12.0f, 0.08f);
    faceSwitch.setTitle ("panel face");
    faceSwitch.onSelect = [this] (int face) { showFace (face); };
    addToFace (pinned, faceSwitch);

    darkModeButton.setButtonText ("Dark");
    darkModeButton.getProperties().set (switchProperty, "inline");
    darkModeButton.setClickingTogglesState (true);
    darkModeButton.setMouseClickGrabsKeyboardFocus (false);
    darkModeButton.setToggleState (theme::isDark, juce::dontSendNotification);
    darkModeButton.setTitle ("switch between light and dark");
    darkModeButton.onClick = [this] { setDarkMode (darkModeButton.getToggleState()); };
    addToFace (pinned, darkModeButton);

    //  ---- the board and the two strips under it: on both faces -------------
    addToFace (pinned, board);
    board.onMessage = [this] (const juce::String& text) { showMessage (text); };

    //  in size order on screen; the values are the parameter's append-only slots
    setUpCaption (pinned, sizeCaption, "board");
    setUpSegments (pinned, sizeSwitch, { { "8", 3 }, { "9", 0 }, { "13", 1 }, { "19", 2 } }, "boardSize", sizeAttachment);
    sizeSwitch.setTitle ("board size");
    setUpCaption (pinned, placeCaption, "place");
    setUpSegments (pinned, placeSwitch, { { "Alt", 0 }, { "Black", 1 }, { "White", 2 } }, "colourMode", placeAttachment);
    placeSwitch.setTitle ("colour to place");

    setUpButton (pinned, clearButton, "Clear board", [this]
    {
        processor.clearBoard();
        showMessage ("board cleared");
        board.repaint();
    });

    //  the rules decide which moves are legal, not how anything sounds - set once
    //  per piece, so they sit by the board rather than among the sound
    setUpCaption (pinned, rulesCaption, "rules");
    setUpSwitch (pinned, koButton, "Ko rule", false, "koRule", koAttachment);
    setUpSwitch (pinned, selfCaptureButton, "Self capture", false, "selfCapture", selfCaptureAttachment);

    hintLabel.setText (utf8 ("click: place  \xc2\xb7  click a stone: lift  \xc2\xb7  drop an .sgf"), juce::dontSendNotification);
    setUpText (pinned, hintLabel, juce::Justification::centredRight);
    hintLabel.setFont (theme::font (11.0f));

    //  ==== PLAY ==============================================================
    //  ---- playheads: how they walk, how fast, from which clock
    setUpSegments (playFace, modeSwitch,
                   { { "spiral", 0 }, { "polyrhythm", 1 }, { "quads out", 2 }, { "quads in", 3 } },
                   "playMode", modeAttachment);
    modeSwitch.setTitle ("walk");
    modeSwitch.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> area, int mode) { drawModeIcon (g, area, mode); };

    setUpKnob (playFace, rateKnob, rateCaption, "step rate", "rate", rateAttachment,
               choiceParser (GoSequencerProcessor::rateNames()));
    rateKnob.getProperties().set (detentProperty, true);
    setUpKnob (playFace, tempoKnob, tempoCaption, "free tempo", "tempo", tempoAttachment,
               parser ([] (const std::string& t) { return valuetext::number (t, { "bpm" }); }));
    setUpSwitch (playFace, freeRunButton, "free run", true, "freeRun", freeRunAttachment);
    setUpText (playFace, lapLabel, juce::Justification::topLeft);

    //  ---- voice: pitch, length and velocity, a column each
    setUpKnob (playFace, noteKnob, noteCaption, "note", "note", noteAttachment,
               parser ([] (const std::string& t) { return valuetext::note (t); }));
    setUpKnob (playFace, spreadKnob, spreadCaption, "spread", "ringSpread", spreadAttachment,
               wholeParser ({ "semitones", "semitone", "st" }));
    spreadKnob.getProperties().set (bipolarProperty, true);
    setUpKnob (playFace, gateKnob, gateCaption, "gate", "gate", gateAttachment,
               parser ([] (const std::string& t) { return valuetext::percent (t); }));
    setUpSwitch (playFace, tieNotesButton, "tie notes", true, "tieNotes", tieNotesAttachment);
    setUpKnob (playFace, blackVelocityKnob, blackVelocityCaption, utf8 ("\xe2\x97\x8f black"), "blackVelocity",
               blackVelocityAttachment, wholeParser());
    setUpKnob (playFace, whiteVelocityKnob, whiteVelocityCaption, utf8 ("\xe2\x97\x8b white"), "whiteVelocity",
               whiteVelocityAttachment, wholeParser());
    blackVelocityKnob.setTitle ("black velocity");
    whiteVelocityKnob.setTitle ("white velocity");

    //  ---- output: per voice, the lamp that lights as it plays, over the channel
    //  it plays on. Velocity follows the colour in every mode; the cells the mode
    //  is not routing by are faded by refreshModeDisplay(), and stay settable.
    addToFace (playFace, activity);
    setUpCell (blackChannelCell, "black channel", "blackChannel", blackChannelAttachment);
    setUpCell (whiteChannelCell, "white channel", "whiteChannel", whiteChannelAttachment);

    for (int h = 0; h < headChannels; ++h)
        setUpCell (headChannelCells[(size_t) h], "head " + juce::String (h + 1) + " channel",
                   "headChannel" + juce::String (h + 1), headChannelAttachments[(size_t) h]);

    setUpText (playFace, outputLabel, juce::Justification::centredLeft);
    setUpText (playFace, outputRouteLabel, juce::Justification::topLeft);

    //  ---- the record
    setUpText (playFace, gameTitleLabel, juce::Justification::centredLeft);
    setUpText (playFace, gameDetailLabel, juce::Justification::centredLeft);
    gameTitleLabel.setFont (valueFont().boldened());
    gameDetailLabel.setFont (theme::font (11.5f));

    setUpButton (playFace, loadButton, utf8 ("Load SGF\xe2\x80\xa6"), [this] { openSgfChooser(); }, true);

    setUpButton (playFace, unloadButton, "Unload", [this]
    {
        processor.clearGame();          //  a run ends here too: it turns its own switch off
        refreshGameDisplay();
        showMessage ("record unloaded - the board stays as it stood");
    }, true);

    setUpKnob (playFace, gameRateKnob, gameRateCaption, "move rate", "gameRate", gameRateAttachment,
               choiceParser (GoSequencerProcessor::gameRateNames()));
    gameRateKnob.getProperties().set (detentProperty, true);
    setUpSwitch (playFace, runGameButton, "run game", true, "gameRun", runGameAttachment);
    setUpSwitch (playFace, loopGameButton, "loop", true, "gameLoop", loopGameAttachment);

    moveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    moveSlider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 130, 18);
    moveSlider.setRepaintsOnMouseActivity (true);
    moveSlider.setTitle ("position in the record");
    moveSlider.setRange (0.0, 1.0, 1.0);
    moveSlider.textFromValueFunction = [this] (double value)
    {
        if (! processor.hasGame())
            return juce::String ("no record");

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

    setUpButton (playFace, previousMoveButton, utf8 ("\xe2\x80\xb9"),
                 [this] { processor.nudgeGamePosition (-1); refreshGameDisplay(); });
    previousMoveButton.setTitle ("previous move");

    setUpButton (playFace, nextMoveButton, utf8 ("\xe2\x80\xba"),
                 [this] { processor.nudgeGamePosition (1); refreshGameDisplay(); });
    nextMoveButton.setTitle ("next move");

    //  ---- the players: self-play
    //  The record is written rather than loaded, so Move Rate, Run and Loop
    //  drive a generated game exactly as they drive a loaded one.
    setUpSwitch (playFace, aiPlayButton, "self-play", true, "aiPlay", aiPlayAttachment);
    aiPlayButton.setTitle ("AI self-play");

    //  ---- and playing against them
    //  The same pair, answering a move at a time instead of writing a whole
    //  game. It owns the board while it is on, so the processor turns self-play
    //  and Run Game off rather than let two things write to the same stones.
    setUpSwitch (playFace, aiOpponentButton, "play against", true, "aiOpponent", aiOpponentAttachment);
    aiOpponentButton.setTitle ("play against the AI");

    //  which pair writes the games: the classic players, or the ones that read
    //  ladders, eye shapes and areas before they choose
    setUpCaption (playFace, playersCaption, "players");
    playersCaption.setJustificationType (juce::Justification::centred);
    setUpSegments (playFace, playersSwitch, { { "Classic", 0 }, { "Reading", 1 } }, "aiPlayers", playersAttachment);
    playersSwitch.setVertical (true);
    playersSwitch.setTitle ("players");

    setUpKnob (playFace, aiMovesKnob, aiMovesCaption, "length", "aiMoves", aiMovesAttachment,
               wholeParser ({ "moves", "move", "mv" }));
    setUpKnob (playFace, aiVariationKnob, aiVariationCaption, "variation", "aiVariation", aiVariationAttachment,
               wholeParser ({ "%" }));
    setUpKnob (playFace, aiSeedKnob, aiSeedCaption, "seed", "aiSeed", aiSeedAttachment, wholeParser());

    //  the faceplate's own readouts for these two; the host keeps the parameter's
    //  "60 mv" and "35%". Set after the attachment, which writes its own.
    aiMovesKnob.textFromValueFunction     = [] (double v) { return juce::String (juce::roundToInt (v)) + " moves"; };
    aiVariationKnob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
    aiMovesKnob.updateText();
    aiVariationKnob.updateText();

    //  The opening. A position is not an opening - the order decides what is
    //  captured - so this takes the ten stones the board was clicked in, not
    //  the ten standing on it. Short of ten, the button says how many so far.
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
        showMessage (processor.aiSelfPlay() ? "opening set: your ten moves - from the next game"
                                            : "opening set: your ten moves");
    }, true);

    setUpButton (playFace, openingBookButton, "Use book", [this]
    {
        processor.useBookOpening();
        refreshOpeningDisplay();
        showMessage ("back to the book line");
    }, true);

    //  ● is the parameter's Black, ○ its White - shown black first, like the board
    setUpCaption (playFace, opponentCaption, "they play");
    setUpSegments (playFace, opponentSwitch, { { utf8 ("\xe2\x97\x8f"), 1 }, { utf8 ("\xe2\x97\x8b"), 0 } },
                   "aiOpponentColour", opponentAttachment);
    opponentSwitch.setTitle ("they play");

    setUpButton (playFace, passButton, "Pass", [this]
    {
        processor.passMove();
        board.repaint();
        refreshMatchDisplay();
    }, true);

    setUpButton (playFace, newMatchButton, "New game", [this]
    {
        processor.newMatch();
        board.repaint();
        refreshMatchDisplay();
        showMessage ("a new game, from an empty board");
    }, true);

    //  ---- stone life: how long a stone sounds, and the wave that replays them
    setUpKnob (playFace, lifeKnob, lifeCaption, "stone life", "stoneLife", lifeAttachment,
               parser ([] (const std::string& t) { return valuetext::life (t, GoSequencerProcessor::maxStoneLife); }));
    setUpSwitch (playFace, waveReplayButton, "wave replay", true, "waveReplay", waveReplayAttachment);
    setUpKnob (playFace, waveGapKnob, waveGapCaption, "wave gap", "waveGap", waveGapAttachment, wholeParser());

    setUpCaption (playFace, lifeModeCaption, "life counts");
    setUpSegments (playFace, lifeModeSwitch, { { "Steps", 0 }, { "Placements", 1 } }, "lifeMode", lifeModeAttachment);
    lifeModeSwitch.setTitle ("life counts");

    //  ==== PATCH =============================================================
    //  ---- the port: the channels set on PLAY, kept intact for a host that
    //  would merge them on its own track to track routing - see MidiPortOut
    setUpCaption (patchFace, portCaption, "port");

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

    portNoteLabel.setText ("Live puts a plugin's notes all on one channel. Sent through a loopMIDI port as well, "
                           "each voice keeps its own, for a track to pick out.", juce::dontSendNotification);
    setUpText (patchFace, portNoteLabel, juce::Justification::topLeft);

    addToFace (patchFace, routingTable);
    routingNoteLabel.setText ("Each voice's channel is set in OUTPUT, on the PLAY face.", juce::dontSendNotification);
    setUpText (patchFace, routingNoteLabel, juce::Justification::topLeft);
    refreshPortList();

    //  ---- the Launchpad
    //  Its grid as the board. Choosing the ports is all there is to it: the
    //  plugin puts the Launchpad into Programmer mode itself and gives it back
    //  when it lets go, so nothing is set up in Novation Components.
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

    setUpButton (patchFace, padsStopButton, "Stop", [this]
    {
        processor.setLaunchpadPorts ({}, {});
        refreshPadsLists();
        showMessage ("the Launchpad is back to its own modes");
    });

    setUpButton (patchFace, padsSizeButton, utf8 ("Use 8 \xc3\x97 8"), [this]
    {
        useLaunchpadBoardSize();
        showMessage ("the board is 8 x 8 now, the size of the grid");
    });

    setUpText (patchFace, padsStatusLabel, juce::Justification::topLeft);

    //  the device itself, with what each button round its edge does here - and
    //  over the left half of its pads, the two ports it is driven on
    addToFace (patchFace, padsDiagram);

    padsInBox.setTitle ("launchpad in");
    padsInBox.getProperties().set (boxedProperty, true);
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
    padsOutBox.getProperties().set (boxedProperty, true);
    padsOutBox.onOpen = [this] { refreshPadsLists(); };
    padsOutBox.onChange = [this]
    {
        const int index = padsOutBox.getSelectedId() - 2;

        choosePadsPorts (processor.launchpadIn(),
                         juce::isPositiveAndBelow (index, padsOutItems.size()) ? padsOutItems[index] : juce::String());
    };
    addToFace (patchFace, padsOutBox);
    setUpCaption (patchFace, padsOutCaption, "pads out");

    //  the captions sit on the pads, so they carry the plate's colour behind them
    for (auto* caption : { &padsInCaption, &padsOutCaption })
    {
        caption->setColour (juce::Label::backgroundColourId, theme::background);
        caption->setBorderSize ({ 0, 3, 0, 3 });
    }

    padsNoteLabel.setText (utf8 ("Above: each top button's job, left to right. Right: each side button's job, on its own row. "
                                 "Inside: the ports the pads use. The pads are the 8 \xc3\x97 8 board: press to place, "
                                 "press a stone to lift it."), juce::dontSendNotification);
    setUpText (patchFace, padsNoteLabel, juce::Justification::topLeft);

    //  the tick only follows changes, so the knobs a switch leaves idle start out faded here
    lastWaveReplayShown = processor.waveReplayOn();
    muteKnob (waveGapKnob, waveGapCaption, ! lastWaveReplayShown);

    lastAiShown = processor.aiSelfPlay();
    lastAiGameShown = lastAiShown ? processor.aiGameNumber() : -1;

    for (auto [knob, caption] : { std::pair (&aiMovesKnob, &aiMovesCaption), std::pair (&aiVariationKnob, &aiVariationCaption),
                                  std::pair (&aiSeedKnob, &aiSeedCaption) })
        muteKnob (*knob, *caption, ! lastAiShown);

    playersSwitch.setEnabled (lastAiShown);
    playersCaption.setAlpha (lastAiShown ? 1.0f : 0.38f);

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

    for (auto* caption : { &padsInCaption, &padsOutCaption })
        caption->setColour (juce::Label::backgroundColourId, theme::background);

    //  these colour themselves ink or dimText depending on state, not always
    //  dimText, so they need re-reading rather than the flat reset above
    refreshGameDisplay();
    refreshPortStatus();
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
    label.setFont (lineFont());
    label.setColour (juce::Label::textColourId, theme::dimText);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setJustificationType (justification);
    label.setMinimumHorizontalScale (1.0f);         //  wrap a long line, never squeeze it
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

void GoSequencerEditor::setUpCell (Knob& cell, const juce::String& title,
                                   const juce::String& parameterID, std::unique_ptr<SliderAttachment>& attachment)
{
    //  a bar slider is the one whose text box covers it: a click types, a drag
    //  changes - ten pixels a channel
    cell.setSliderStyle (juce::Slider::LinearBarVertical);
    cell.setSliderSnapsToMousePosition (false);
    cell.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 30, 30);
    cell.setMouseDragSensitivity (160);
    cell.setRepaintsOnMouseActivity (true);
    cell.setTitle (title);
    cell.parse = wholeParser();
    cell.onNote = [this] (const juce::String& note) { showMessage ("channel: " + note); };
    addToFace (playFace, cell);

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

void GoSequencerEditor::setUpSwitch (Face face, juce::TextButton& button, const juce::String& text, bool cell,
                                     const juce::String& parameterID, std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText (text);
    button.setTitle (text);
    button.getProperties().set (switchProperty, cell ? "cell" : "inline");
    button.setClickingTogglesState (true);
    button.setMouseClickGrabsKeyboardFocus (false);
    addToFace (face, button);

    attachment = std::make_unique<ButtonAttachment> (processor.apvts, parameterID, button);
}

void GoSequencerEditor::setUpButton (Face face, juce::TextButton& button, const juce::String& text,
                                     std::function<void()> onClick, bool small)
{
    button.setButtonText (text);
    button.onClick = std::move (onClick);
    button.setMouseClickGrabsKeyboardFocus (false);

    if (small)
        button.getProperties().set (smallProperty, true);

    addToFace (face, button);
}

void GoSequencerEditor::muteKnob (Knob& knob, juce::Label& caption, bool muted)
{
    knob.setEnabled (! muted);
    knob.setAlpha (muted ? 0.38f : 1.0f);
    caption.setAlpha (muted ? 0.38f : 1.0f);
}

Knob& GoSequencerEditor::channelCell (int voice)
{
    return voice == 0 ? blackChannelCell : voice == 1 ? whiteChannelCell : headChannelCells[(size_t) (voice - 2)];
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
    openingBookButton.setEnabled (processor.hasCustomOpening());
    lastCustomOpeningShown = processor.hasCustomOpening();
}

void GoSequencerEditor::refreshGameDisplay()
{
    const bool has = processor.hasGame();

    gameTitleLabel.setText (has ? processor.gameTitle() : "no record loaded", juce::dontSendNotification);
    gameTitleLabel.setColour (juce::Label::textColourId, has ? theme::ink : theme::dimText);
    gameDetailLabel.setText (has ? processor.gameDetail() : "drop an .sgf on the plate",
                             juce::dontSendNotification);

    moveSlider.setEnabled (has);
    previousMoveButton.setEnabled (has);
    nextMoveButton.setEnabled (has);
    unloadButton.setEnabled (has);
    runGameButton.setEnabled (has);

    //  a new colour rebuilds the value box, so only when it is a new colour
    const auto valueColour = has ? theme::ink : theme::dimText;

    if (moveSlider.findColour (juce::Slider::textBoxTextColourId) != valueColour)
        moveSlider.setColour (juce::Slider::textBoxTextColourId, valueColour);

    moveSlider.setRange (0.0, (double) juce::jmax (1, processor.gameMoveCount()), 1.0);
    moveSlider.setValue ((double) processor.gamePosition(), juce::dontSendNotification);
    moveSlider.updateText();
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
    portBox.addItem ("Off - notes go to the host", 1);

    for (int i = 0; i < portItems.size(); ++i)
        portBox.addItem (portItems[i] + (missing && i == portItems.size() - 1 ? "  (not there)" : ""), i + 2);

    portBox.setSelectedId (wanted.isEmpty() ? 1 : portItems.indexOf (wanted) + 2, juce::dontSendNotification);

    refreshPortStatus();
}

void GoSequencerEditor::refreshPortStatus()
{
    const auto wanted = processor.midiOutPort();
    const bool open = processor.midiOutPortOpen();

    //  with no port the dropdown already says where the notes go, so the line
    //  only speaks - and takes up room - once there is a port to say something about
    juce::String status;

    if (wanted.isEmpty())
        status = {};
    else if (open)
        status = "also sending to " + wanted + " - each track listening to it can pick out a channel";
    else
        status = "waiting for " + wanted + " - is loopMIDI running?";

    const bool hadStatus = portStatusLabel.getText().isNotEmpty();

    portStatusLabel.setText (status, juce::dontSendNotification);
    portStatusLabel.setColour (juce::Label::textColourId,
                               (wanted.isNotEmpty() && ! open) ? theme::accent : theme::ink);

    if (hadStatus != status.isNotEmpty() && getWidth() > 0)
        resized();

    lastPortOpenShown = open;
    refreshOutputLines();
}

void GoSequencerEditor::refreshMatchDisplay()
{
    const bool on = processor.matchActive();

    //  their colour stays choosable with no game on, so it can be set before one
    //  starts - changing it during a game starts that game again
    passButton.setEnabled (on && processor.yourTurn());
    newMatchButton.setEnabled (on);
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
    padsStatusLabel.setColour (juce::Label::textColourId, trouble ? theme::accent : theme::ink);

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
    //  through the parameter, as the size switch does, so the host hears
    //  about it and the switch follows
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
        };

        //  where the stones come from - or, while playing against the AI, whose move it is
        if (processor.matchActive())
            fields.push_back ({ "MATCH", processor.matchIsOver() ? juce::String ("over")
                                       : processor.yourTurn()    ? juce::String ("your move")
                                                                 : juce::String ("AI's move") });
        else
            fields.push_back ({ "SOURCE", processor.aiSelfPlay() ? "AI game " + juce::String (processor.aiGameNumber())
                                        : processor.hasGame()    ? juce::String ("record")
                                                                 : juce::String ("hand") });

        if (processor.hasGame())
            fields.push_back ({ "MOVE", juce::String (processor.gamePosition()) + "/" + juce::String (processor.gameMoveCount()) });

        fields.push_back ({ "CAPT", utf8 ("\xe2\x97\x8f ") + juce::String (processor.capturedBlack())
                                  + utf8 ("  \xe2\x97\x8b ") + juce::String (processor.capturedWhite()) });

        return fields;
    }

    //  the wordmark: G, a white stone for the O, SE, a black stone for the Q, UENCER
    constexpr float wordmarkStone = 0.74f, stoneLeft = 0.05f, stoneRight = 0.16f;

    float wordmarkWidth()
    {
        const auto font = titleFont();
        const float em = font.getHeightInPoints();
        float width = 0.0f;

        for (auto* piece : { "G", " SE", "UENCER" })
            width += juce::GlyphArrangement::getStringWidth (font, piece);

        return width + 2.0f * (stoneLeft + wordmarkStone + stoneRight) * em;
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
    columnHeads.clear();

    const auto addSection = [this] (juce::Rectangle<int> bounds, const juce::String& title, const juce::String& job, Face face)
    {
        sections.push_back ({ bounds, title, job, face });
        return bounds.withTrimmedLeft (sectionPadX).withTrimmedRight (sectionPadX)
                     .withTrimmedTop (sectionPadTop).withTrimmedBottom (sectionPadBottom);
    };

    const auto gap = [] (juce::Rectangle<int>& area, int pixels) { area.removeFromTop (pixels); };
    const auto dot = utf8 (" \xc2\xb7 ");

    //  ---- top bar: the wordmark, the display, the faces, Dark ---------------
    topBarBounds = { 22, 16, Faceplate::width - 44, 48 };
    {
        auto bar = topBarBounds;

        const int dark = switchWidth (darkModeButton);
        darkModeButton.setBounds (bar.removeFromRight (dark).withSizeKeepingCentre (dark, 24));
        bar.removeFromRight (14);
        faceSwitch.setBounds (bar.removeFromRight (164).withSizeKeepingCentre (164, 34));
        bar.removeFromRight (14);

        wordmarkBounds = bar.removeFromLeft ((int) std::ceil (wordmarkWidth()));
        bar.removeFromLeft (14);
        displayBounds = bar.withSizeKeepingCentre (bar.getWidth(), 36);
    }

    constexpr int top = 78, bottom = Faceplate::height - 18;
    constexpr int leftX = 22, railWidth = 282, boardX = leftX + railWidth + 19, boardSide = 556;
    constexpr int rightX = boardX + boardSide + 19, rightWidth = Faceplate::width - 22 - rightX;

    //  ==== PLAY, left: playheads, voice, output ==============================
    {
        constexpr int headsHeight = 212, voiceHeight = 214;

        auto heads = addSection ({ leftX, top, railWidth, headsHeight }, "PLAYHEADS", "walk" + dot + "rate" + dot + "clock", playFace);
        modeSwitch.setBounds (heads.removeFromTop (52));
        gap (heads, 10);

        auto cells = threeCells (heads.removeFromTop (knobHeight));
        placeKnob (cells[0], rateCaption, rateKnob);
        placeKnob (cells[1], tempoCaption, tempoKnob);
        freeRunButton.setBounds (cells[2]);
        gap (heads, 8);
        lapLabel.setBounds (heads);                 //  two lines: nine rings do not fit on one

        const int voiceTop = top + headsHeight + sectionGap;
        auto voice = addSection ({ leftX, voiceTop, railWidth, voiceHeight }, "VOICE", "how each note sounds", playFace);
        cells = threeCells (voice);

        const char* const columnNames[] = { "pitch", "length", "velocity" };

        for (size_t i = 0; i < 3; ++i)
            columnHeads.push_back ({ cells[i].withHeight (18), columnNames[i], playFace });

        const int firstRow = voice.getY() + 18 + 6, secondRow = firstRow + knobHeight + 6;
        placeKnob (cells[0].withY (firstRow),  noteCaption, noteKnob);
        placeKnob (cells[0].withY (secondRow), spreadCaption, spreadKnob);
        placeKnob (cells[1].withY (firstRow),  gateCaption, gateKnob);
        tieNotesButton.setBounds (cells[1].withY (secondRow));
        placeKnob (cells[2].withY (firstRow),  blackVelocityCaption, blackVelocityKnob);
        placeKnob (cells[2].withY (secondRow), whiteVelocityCaption, whiteVelocityKnob);

        const int outputTop = voiceTop + voiceHeight + sectionGap;
        auto output = addSection ({ leftX, outputTop, railWidth, bottom - outputTop }, "OUTPUT",
                                  utf8 ("voices \xe2\x86\x92 MIDI channels"), playFace);

        const auto bay = output.removeFromTop (ActivityLamps::headHeight + 30);
        activity.setBounds (bay);

        for (int v = 0; v < ActivityLamps::voices; ++v)
            channelCell (v).setBounds (activity.column (v).translated (bay.getX(), bay.getY())
                                                          .withTrimmedTop (ActivityLamps::headHeight));

        gap (output, 8);
        outputLabel.setBounds (output.removeFromTop (lineHeight));
        gap (output, 3);
        outputRouteLabel.setBounds (output.removeFromTop (2 * lineHeight));     //  a port's name can be long
    }

    //  ==== PATCH, left: the MIDI out port and what goes out on it ============
    {
        auto out = addSection ({ leftX, top, railWidth, bottom - top }, "MIDI OUT", "where the notes go", patchFace);
        portCaption.setBounds (out.removeFromTop (15));
        portBox.setBounds (out.removeFromTop (26));
        gap (out, 8);

        if (portStatusLabel.getText().isNotEmpty())
        {
            portStatusLabel.setBounds (out.removeFromTop (2 * lineHeight));
            gap (out, 6);
        }

        portNoteLabel.setBounds (out.removeFromTop (3 * lineHeight));
        gap (out, 12);
        routingTable.setBounds (out.removeFromTop (110));
        gap (out, 6);
        routingNoteLabel.setBounds (out.removeFromTop (2 * lineHeight));
    }

    //  ==== the board and its two strips, on both faces =======================
    {
        board.setBounds (boardX, top, boardSide, boardSide);

        auto strip = juce::Rectangle<int> (boardX, top + boardSide + 8, boardSide, 28);
        sizeCaption.setBounds (strip.removeFromLeft (44));
        sizeSwitch.setBounds (strip.removeFromLeft (168).withSizeKeepingCentre (168, segmentHeight));
        strip.removeFromLeft (18);
        placeCaption.setBounds (strip.removeFromLeft (44));
        placeSwitch.setBounds (strip.removeFromLeft (168).withSizeKeepingCentre (168, segmentHeight));

        const int clear = pillWidth (clearButton);
        clearButton.setBounds (strip.removeFromRight (clear).withSizeKeepingCentre (clear, buttonHeight));

        auto rules = juce::Rectangle<int> (boardX, top + boardSide + 8 + 28 + 6, boardSide, 24);
        rulesCaption.setBounds (rules.removeFromLeft (44));
        rules.removeFromLeft (14);
        koButton.setBounds (rules.removeFromLeft (switchWidth (koButton)));
        rules.removeFromLeft (14);
        selfCaptureButton.setBounds (rules.removeFromLeft (switchWidth (selfCaptureButton)));
        rules.removeFromLeft (14);
        hintLabel.setBounds (rules);
    }

    //  ==== PLAY, right: the record, the players, stone life ==================
    {
        constexpr int recordHeight = 214, playersHeight = 241;

        auto record = addSection ({ rightX, top, rightWidth, recordHeight }, "RECORD", "moves from an .sgf", playFace);

        //  the file in a well, with its two buttons
        gameWellBounds = record.removeFromTop (48);
        {
            auto well = gameWellBounds.withTrimmedLeft (10).withTrimmedRight (8);
            const int w = juce::jmax (pillWidth (loadButton), pillWidth (unloadButton));

            auto buttons = well.removeFromRight (w).withSizeKeepingCentre (w, 2 * smallButtonHeight + 4);
            loadButton.setBounds (buttons.removeFromTop (smallButtonHeight));
            buttons.removeFromTop (4);
            unloadButton.setBounds (buttons);
            well.removeFromRight (8);

            auto text = well.withSizeKeepingCentre (well.getWidth(), 34);
            gameTitleLabel.setBounds (text.removeFromTop (18));
            gameDetailLabel.setBounds (text);
        }
        gap (record, 10);

        //  its transport
        auto cells = threeCells (record.removeFromTop (knobHeight));
        placeKnob (cells[0], gameRateCaption, gameRateKnob);
        runGameButton.setBounds (cells[1]);
        loopGameButton.setBounds (cells[2]);
        gap (record, 6);

        //  and where it stands: the caption and the move on one line, the line under them
        auto scrub = record.removeFromTop (42);
        auto stepper = scrub.removeFromRight (2 * glyphButtonWidth + 4).withTrimmedTop (18);
        scrub.removeFromRight (8);
        moveSlider.setBounds (scrub);
        moveCaption.setBounds (scrub.getX(), scrub.getY(), 80, 18);
        previousMoveButton.setBounds (stepper.removeFromLeft (glyphButtonWidth));
        stepper.removeFromLeft (4);
        nextMoveButton.setBounds (stepper.removeFromLeft (glyphButtonWidth));

        const int playersTop = top + recordHeight + sectionGap;
        auto players = addSection ({ rightX, playersTop, rightWidth, playersHeight }, "AI PLAYERS",
                                   "self-play" + dot + "play against", playFace);
        cells = threeCells (players.removeFromTop (knobHeight));
        aiPlayButton.setBounds (cells[0]);
        aiOpponentButton.setBounds (cells[1]);
        playersCaption.setBounds (cells[2].withHeight (captionHeight));
        playersSwitch.setBounds (cells[2].getX(), cells[2].getY() + captionHeight + 7, knobWidth, 48);
        gap (players, 3);

        cells = threeCells (players.removeFromTop (knobHeight));
        placeKnob (cells[0], aiMovesCaption, aiMovesKnob);
        placeKnob (cells[1], aiVariationCaption, aiVariationKnob);
        placeKnob (cells[2], aiSeedCaption, aiSeedKnob);
        gap (players, 3);

        auto row = players.removeFromTop (buttonHeight);
        openingCaption.setBounds (row.removeFromLeft (66));
        flow (row, { &openingFromBoardButton, &openingBookButton });
        gap (players, 3);

        row = players.removeFromTop (buttonHeight);
        opponentCaption.setBounds (row.removeFromLeft (66));
        opponentSwitch.setBounds (row.removeFromLeft (48));
        flowRight (row, { &passButton, &newMatchButton });

        const int lifeTop = playersTop + playersHeight + sectionGap;
        auto life = addSection ({ rightX, lifeTop, rightWidth, bottom - lifeTop }, "STONE LIFE", "how long a stone sounds", playFace);
        cells = threeCells (life.removeFromTop (knobHeight));
        placeKnob (cells[0], lifeCaption, lifeKnob);
        waveReplayButton.setBounds (cells[1]);
        placeKnob (cells[2], waveGapCaption, waveGapKnob);
        gap (life, 8);

        row = life.removeFromTop (buttonHeight);
        lifeModeSwitch.setBounds (row.removeFromRight (168));
        lifeModeCaption.setBounds (row);
    }

    //  ==== PATCH, right: the Launchpad =======================================
    {
        auto pads = addSection ({ rightX, top, rightWidth, bottom - top }, "LAUNCHPAD X",
                                utf8 ("its 8 \xc3\x97 8 pads as the board"), patchFace);
        flow (pads.removeFromTop (buttonHeight), { &padsFindButton, &padsStopButton, &padsSizeButton });
        gap (pads, 8);

        //  three lines, because the one that says the port is taken is long
        padsStatusLabel.setBounds (pads.removeFromTop (3 * lineHeight));
        gap (pads, 8);

        padsDiagram.setBounds (pads.removeFromTop (LaunchpadDiagram::height)
                                   .withSizeKeepingCentre (LaunchpadDiagram::width, LaunchpadDiagram::height));
        gap (pads, 8);
        padsNoteLabel.setBounds (pads.removeFromTop (5 * lineHeight));

        //  the two ports, over the left half of the pads
        auto half = LaunchpadDiagram::leftHalf().translated (padsDiagram.getX(), padsDiagram.getY())
                                                .withTrimmedLeft (4).withTrimmedRight (10);
        auto group = half.withSizeKeepingCentre (half.getWidth(), 2 * 42 + 16);

        const auto place = [&] (juce::Label& caption, juce::ComboBox& box)
        {
            auto slot = group.removeFromTop (42);
            caption.setBounds (slot.removeFromTop (15).withWidth (textWidth (captionFont(), caption.getText()) + 8));
            slot.removeFromTop (3);
            box.setBounds (slot);
        };

        place (padsInCaption, padsInBox);
        group.removeFromTop (16);
        place (padsOutCaption, padsOutBox);
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
    muteKnob (spreadKnob, spreadCaption, ! multi);

    //  spiral routes by colour, the multi head modes by playhead, and a 9x9 has
    //  fewer rings than a 13x13 - so fade what this mode is not using. It stays
    //  settable, ready for the mode that does use it.
    for (int v = 0; v < ActivityLamps::voices; ++v)
        channelCell (v).setAlpha (activity.inUse (v) ? 1.0f : 0.3f);

    activity.repaint();

    const juce::String dot (utf8 (" \xc2\xb7 "));
    juce::String lap;

    if (processor.isPolyrhythm())
    {
        //  the rings are different lengths - which is the polyrhythm
        lap << heads << " heads, one per ring" << dot << "laps of ";

        for (int r = 0; r < heads; ++r)
            lap << (r > 0 ? dot : juce::String()) << go::ringLength (size, r);
    }
    else if (processor.isQuads())
    {
        lap << "4 heads, one per quadrant" << dot << go::quadSteps (size) << " steps each";
    }
    else
    {
        lap << "1 head walks all " << size * size << " points";
    }

    lapLabel.setText (lap, juce::dontSendNotification);
    lapLabel.setColour (juce::Label::textColourId, theme::ink);

    refreshOutputLines();
    routingTable.refresh();
}

void GoSequencerEditor::refreshOutputLines()
{
    const int heads = processor.headCount();
    const bool multi = heads > 1;

    outputLabel.setText (multi ? juce::String (heads) + (processor.isQuads() ? " quadrants" : " rings")
                                     + ": each head on its own channel"
                               : juce::String ("spiral: black and white each have a channel"),
                         juce::dontSendNotification);
    outputLabel.setColour (juce::Label::textColourId, theme::ink);

    const auto port = processor.midiOutPort();

    outputRouteLabel.setText ((multi ? utf8 ("\xe2\x97\x8f \xe2\x97\x8b unused in this mode")
                                     : utf8 ("heads 1\xe2\x80\x93" "9 unused in this mode"))
                              + utf8 ("  \xc2\xb7  out: ") + (port.isEmpty() ? utf8 ("to the\xc2\xa0host") : "host + " + port),
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

    //  a channel can be set from the host, so the PATCH face's table is kept up
    //  with them while it shows - it only repaints when a line reads differently
    if (currentFace == patchFace)
        routingTable.refresh();

    const bool waveReplay = processor.waveReplayOn();

    if (waveReplay != lastWaveReplayShown)
    {
        lastWaveReplayShown = waveReplay;

        //  Wave Gap only means anything once Wave Replay is on. Loop still
        //  does - it wraps the record without clearing the board, which is
        //  what keeps the wave running - so that switch stays live.
        muteKnob (waveGapKnob, waveGapCaption, ! waveReplay);
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

        for (auto [knob, caption] : { std::pair (&aiMovesKnob, &aiMovesCaption), std::pair (&aiVariationKnob, &aiVariationCaption),
                                      std::pair (&aiSeedKnob, &aiSeedCaption) })
            muteKnob (*knob, *caption, ! ai);

        playersSwitch.setEnabled (ai);
        playersCaption.setAlpha (ai ? 1.0f : 0.38f);

        refreshGameDisplay();
    }

    if (processor.hasCustomOpening() != lastCustomOpeningShown)
        refreshOpeningDisplay();

    //  their answer lands with nothing clicked, and so does the end of a game,
    //  so whose move it is is read back here rather than only after a button
    {
        const int turn = ! processor.matchActive() ? -1
                       : processor.matchIsOver()   ?  2
                       : processor.yourTurn()      ?  0 : 1;

        if (turn != lastTurnShown)
        {
            if (turn == 2 && lastTurnShown >= 0)
                showMessage ("both passed - " + juce::String (processor.capturedBlack()) + " black and "
                             + juce::String (processor.capturedWhite()) + " white taken");
            else if (turn == 0 && lastTurnShown == -1)
                showMessage (juce::String ("your move - you are ")
                             + (processor.yourColour() == go::Stone::black ? "black" : "white"));

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
            if (lastBoardSizeShown != 0)
                showMessage ("board size changed - the board was cleared");

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

    header << (processor.isRunning() ? "|run" : "|stop");

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
        g.drawEllipse (head.reduced (0.5f), 1.0f);
        g.setColour (theme::faintText);
        g.drawLine (juce::Line<float> (corner.translated (-2.9f, 2.0f), corner.translated (2.9f, -2.0f)), 1.0f);
    }

    paintWordmark (g, wordmarkBounds.toFloat());
    paintDisplay (g, displayBounds.toFloat());

    //  ---- the section frames: the name cut into the top edge on the left, the
    //  job on the right
    for (const auto& section : sections)
    {
        if (section.face != currentFace && section.face != pinned)
            continue;

        g.setColour (theme::hairline);
        g.drawRoundedRectangle (section.bounds.toFloat().reduced (0.5f), 6.0f, 1.0f);

        const int titleWidth = textWidth (sectionFont(), section.title) + 12;
        const auto title = juce::Rectangle<int> (section.bounds.getX() + 10, section.bounds.getY() - 8, titleWidth, 16);

        g.setColour (theme::background);
        g.fillRect (title);
        g.setFont (sectionFont());
        g.setColour (theme::ink);
        g.drawText (section.title, title, juce::Justification::centred, false);

        if (section.job.isNotEmpty())
        {
            const int jobWidth = textWidth (jobFont(), section.job) + 12;
            const auto job = juce::Rectangle<int> (section.bounds.getRight() - 10 - jobWidth, section.bounds.getY() - 8, jobWidth, 16);

            g.setColour (theme::background);
            g.fillRect (job);
            g.setFont (jobFont());
            g.setColour (theme::dimText);
            g.drawText (section.job, job, juce::Justification::centred, false);
        }
    }

    //  ---- the voice's column heads, ruled underneath
    for (const auto& head : columnHeads)
    {
        if (head.face != currentFace)
            continue;

        g.setFont (columnFont());
        g.setColour (theme::dimText);
        g.drawText (head.text.toUpperCase(), head.bounds.withHeight (14), juce::Justification::centred, false);
        g.setColour (theme::hairline);
        g.fillRect (head.bounds.withTop (head.bounds.getBottom() - 1));
    }

    //  ---- the record's file sits in a recessed well, like a read-out
    if (currentFace == playFace)
    {
        g.setColour (theme::well);
        g.fillRoundedRectangle (gameWellBounds.toFloat(), 5.0f);
    }

    if (dragHighlight)
    {
        g.setColour (theme::accent);
        g.drawRect (plate.getLocalBounds(), 2);
    }
}

void GoSequencerEditor::paintWordmark (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto font = titleFont();
    const float em = font.getHeightInPoints();

    //  the capitals centred on the bar; the stones sit on the same baseline
    const float baseline = std::round (area.getCentreY() + 0.35f * em);
    float x = area.getX();

    const auto text = [&] (const char* piece)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, piece, x, baseline);
        g.setColour (theme::ink);
        glyphs.draw (g);
        x += juce::GlyphArrangement::getStringWidth (font, piece);
    };

    const auto stone = [&] (bool black)
    {
        const float d = wordmarkStone * em;
        x += stoneLeft * em;
        BoardComponent::drawStone (g, { x + d * 0.5f, baseline + 0.02f * em - d * 0.5f }, d * 0.5f, black);
        x += d + stoneRight * em;
    };

    text ("G");
    stone (false);
    text (" SE");
    stone (true);
    text ("UENCER");
}

void GoSequencerEditor::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme::lcd);
    g.fillRoundedRectangle (area, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawRoundedRectangle (area.reduced (0.5f), 5.0f, 1.0f);

    auto inner = area.reduced (8.0f, 0.0f);

    //  the transport, at the right hand end: a lamp and a word - shown under a
    //  message too, which only takes the fields' place
    {
        const bool running = processor.isRunning();
        const juce::String word = running ? "running" : "stopped";
        const auto font = theme::monoFont (12.5f);
        auto slot = inner.removeFromRight ((float) textWidth (font, word) + 8.0f + 8.0f + 6.0f);

        const auto lamp = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ slot.getX() + 4.0f, slot.getCentreY() });

        if (running)
        {
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillEllipse (lamp.expanded (3.0f));
        }

        g.setColour (running ? theme::accent : theme::lcdDim);
        g.fillEllipse (lamp);

        g.setFont (font);
        g.setColour (theme::lcdInk);
        g.drawText (word, slot.withTrimmedLeft (16.0f), juce::Justification::centredLeft, false);
        inner.removeFromRight (12.0f);
    }

    if (message.isNotEmpty())
    {
        g.setFont (valueFont());
        g.setColour (theme::accent);
        g.drawText (message, inner.reduced (6.0f, 0.0f), juce::Justification::centredLeft, true);
        return;
    }

    const auto keyFont = theme::font (9.5f, juce::Font::bold, 0.1f);
    const auto valueFontMono = theme::monoFont (12.5f);
    float x = inner.getX();

    for (const auto& [key, value] : displayFields (processor))
    {
        const float keyWidth = (float) textWidth (keyFont, key);
        const float valueWidth = (float) textWidth (valueFontMono, value);

        if (x + 9.0f + keyWidth + 6.0f + valueWidth > inner.getRight())
            break;

        x += 9.0f;

        g.setFont (keyFont);
        g.setColour (theme::lcdDim);
        g.drawText (key, juce::Rectangle<float> (x, area.getY(), keyWidth + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += keyWidth + 6.0f;

        g.setFont (valueFontMono);
        g.setColour (theme::lcdInk);
        g.drawText (value, juce::Rectangle<float> (x, area.getY(), valueWidth + 2.0f, area.getHeight()),
                    juce::Justification::centredLeft, false);
        x += valueWidth + 9.0f;

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x, area.getCentreY() - 7.0f, 1.0f, 14.0f));
    }
}
