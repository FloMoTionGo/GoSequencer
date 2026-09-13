#include "PluginEditor.h"

#include <cmath>

//==============================================================================
namespace
{
    constexpr int captionHeight = 16, controlHeight = 24;
    constexpr int cellHeight = captionHeight + controlHeight;
    constexpr int rowGap = 14, columnGutter = 24, pillGap = 8;
    constexpr int margin = 28, columnGap = 44;
    constexpr int minControlsWidth = 400, maxControlsWidth = 560;
    constexpr int titleHeight = 22, statusHeight = 18, tabHeight = 26, tabGap = 24, headerGap = 18;
    constexpr int textLinesHeight = 36;         //  two lines of the value font
    constexpr int narrowValueWidth = 36;        //  a MIDI channel needs two digits
    constexpr int stepButtonWidth = 36;

    //  a button carrying this property is drawn as a tab rather than a switch
    const juce::Identifier tabProperty { "goTab" };

    //  the open tab rides along in the state, so a session comes back on it
    const juce::Identifier activeTabProperty { "activeTab" };

    //  so does the light/dark choice
    const juce::Identifier darkModeProperty { "darkMode" };

    juce::Font captionFont() { return theme::font (11.0f, juce::Font::plain, 0.06f); }
    juce::Font valueFont()   { return theme::font (12.5f); }
    juce::Font pillFont()    { return theme::font (11.0f, juce::Font::plain, 0.05f); }
    juce::Font tabFont()     { return theme::font (12.5f, juce::Font::plain, 0.06f); }
    juce::Font titleFont()   { return theme::font (16.0f, juce::Font::bold, 0.04f); }
    juce::Font statusFont()  { return theme::font (12.0f); }

    int textWidth (const juce::Font& font, const juce::String& text)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, text));
    }

    //  switches and buttons are as wide as what they say, not as wide as the cell
    int pillWidth (const juce::Button& button)
    {
        return textWidth (pillFont(), button.getButtonText().toUpperCase()) + 28;
    }

    std::vector<juce::Rectangle<int>> columns (juce::Rectangle<int> row, int count)
    {
        std::vector<juce::Rectangle<int>> cells;
        const int width = (row.getWidth() - columnGutter * (count - 1)) / count;

        for (int i = 0; i < count; ++i)
        {
            cells.push_back (i == count - 1 ? row : row.removeFromLeft (width));
            row.removeFromLeft (columnGutter);
        }

        return cells;
    }

    juce::Rectangle<int> nextRow (juce::Rectangle<int>& area, int height)
    {
        auto row = area.removeFromTop (height);
        area.removeFromTop (rowGap);
        return row;
    }

    void placeLabelled (juce::Rectangle<int> cell, juce::Label& caption, juce::Component& control)
    {
        caption.setBounds (cell.removeFromTop (captionHeight));
        control.setBounds (cell.removeFromTop (controlHeight));
    }

    //  the slider takes the whole cell - its value sits on the caption's line,
    //  right aligned - and the caption keeps the part of the line left of it
    void placeSlider (juce::Rectangle<int> cell, juce::Label& caption, juce::Slider& slider)
    {
        slider.setBounds (cell.withHeight (cellHeight));
        caption.setBounds (cell.removeFromTop (captionHeight).withTrimmedRight (slider.getTextBoxWidth()));
    }

    //  a switch in a cell whose neighbours have captions: on the control line
    void placeControl (juce::Rectangle<int> cell, juce::Button& button)
    {
        cell.removeFromTop (captionHeight);
        button.setBounds (cell.removeFromTop (controlHeight).withWidth (juce::jmin (cell.getWidth(), pillWidth (button))));
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

    setColour (juce::Slider::textBoxTextColourId,             theme::ink);
    setColour (juce::Slider::textBoxBackgroundColourId,       juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxOutlineColourId,          juce::Colours::transparentWhite);
    setColour (juce::Slider::textBoxHighlightColourId,        theme::hairline);

    setColour (juce::TextEditor::textColourId,                theme::ink);
    setColour (juce::TextEditor::highlightColourId,           theme::hairline);
    setColour (juce::TextEditor::focusedOutlineColourId,      theme::hairline);
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

    if (button.getProperties().contains (tabProperty))
    {
        //  a tab has no face: the open one is underlined, as far as its text runs
        if (button.getToggleState())
        {
            const auto width = (float) textWidth (tabFont(), button.getButtonText().toUpperCase());

            g.setColour (theme::ink);
            g.fillRect (juce::Rectangle<float> (bounds.getX(), bounds.getBottom() - 1.5f, width, 1.5f));
        }

        return;
    }

    const auto face = bounds.reduced (0.5f);
    const bool enabled = button.isEnabled();

    if (button.getToggleState())
    {
        g.setColour (theme::accent.withMultipliedAlpha (! enabled ? 0.4f : (shouldDrawButtonAsDown ? 0.85f : 1.0f)));
        g.fillRoundedRectangle (face, 2.0f);
    }
    else
    {
        if (shouldDrawButtonAsDown)
        {
            g.setColour (theme::boardFill);
            g.fillRoundedRectangle (face, 2.0f);
        }

        g.setColour (! enabled ? theme::hairline.withMultipliedAlpha (0.6f)
                               : (shouldDrawButtonAsHighlighted ? theme::faintText : theme::hairline));
        g.drawRoundedRectangle (face, 2.0f, 1.0f);
    }

    if (button.hasKeyboardFocus (false))
    {
        g.setColour (theme::ink);
        g.drawRoundedRectangle (face, 2.0f, 1.0f);
    }
}

juce::Font GoLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return pillFont();
}

void GoLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                    bool shouldDrawButtonAsHighlighted, bool)
{
    const auto text = button.getButtonText().toUpperCase();
    const bool on = button.getToggleState();

    if (button.getProperties().contains (tabProperty))
    {
        const bool lit = shouldDrawButtonAsHighlighted || button.hasKeyboardFocus (false);

        g.setFont (tabFont());
        g.setColour (on ? theme::ink : (lit ? theme::dimText : theme::faintText));
        g.drawText (text, button.getLocalBounds().withTrimmedBottom (5), juce::Justification::centredLeft, false);
        return;
    }

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
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    //  a hairline across the whole cell, filled up to a small dot
    const bool enabled = slider.isEnabled();
    const float centreY = (float) y + (float) height * 0.5f;

    g.setColour (theme::hairline);
    g.fillRect (0.0f, centreY - 0.75f, (float) slider.getWidth(), 1.5f);

    g.setColour (enabled ? theme::ink : theme::faintText);
    g.fillRect (0.0f, centreY - 0.75f, juce::jmax (0.0f, sliderPos), 1.5f);

    const float diameter = (enabled && slider.isMouseOverOrDragging()) ? 10.0f : 8.0f;
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre ({ sliderPos, centreY }));
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
    auto valueLine = bounds.removeFromTop (captionHeight);

    juce::Slider::SliderLayout layout;
    layout.textBoxBounds = valueLine.removeFromRight (juce::jmin (slider.getTextBoxWidth(), valueLine.getWidth()));
    layout.sliderBounds  = bounds.reduced (getSliderThumbRadius (slider), 0);

    return layout;
}

juce::Label* GoLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);

    label->setFont (valueFont());
    label->setJustificationType (juce::Justification::centredRight);
    label->setBorderSize ({ 0, 0, 0, 0 });

    return label;
}

//==============================================================================
GoSequencerEditor::GoSequencerEditor (GoSequencerProcessor& p)
    : AudioProcessorEditor (&p), processor (p), board (p)
{
    setLookAndFeel (&lookAndFeel);

    //  the scheme has to be right before anything below reads a theme:: colour
    theme::setDark (processor.apvts.state.getProperty (darkModeProperty, false));
    lookAndFeel.applyColours();

    lastSgfDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    darkModeButton.setButtonText ("Dark");
    darkModeButton.setClickingTogglesState (true);
    darkModeButton.setToggleState (theme::isDark, juce::dontSendNotification);
    darkModeButton.setTitle ("switch between light and dark");
    darkModeButton.onClick = [this] { setDarkMode (darkModeButton.getToggleState()); };
    addAndMakeVisible (darkModeButton);

    addAndMakeVisible (board);
    board.onMessage = [this] (const juce::String& text) { showMessage (text); };

    //  ---- the tabs ---------------------------------------------------------
    //  One tab of controls at a time, laid out in the same rectangle, so
    //  switching never resizes the window under the host.
    const char* const tabNames[tabCount] = { "Sequencer", "Board", "Channels", "Game", "AI" };

    for (int t = 0; t < tabCount; ++t)
    {
        auto& tab = tabButtons[(size_t) t];
        tab.setButtonText (tabNames[t]);
        tab.getProperties().set (tabProperty, true);
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1);
        tab.onClick = [this, t] { showTab (t); };
        addAndMakeVisible (tab);
    }

    //  ---- beside the board, whichever tab is open --------------------------
    //  what a click on the board does depends on these two, so they stay by it
    setUpCombo (pinned, sizeBox, sizeCaption, "board", GoSequencerProcessor::boardSizeNames(), "boardSize", sizeAttachment);
    setUpCombo (pinned, colourBox, colourCaption, "place", { "Alternate", "Black", "White" }, "colourMode", colourAttachment);

    //  ---- sequencer --------------------------------------------------------
    setUpCombo  (sequencerTab, modeBox, modeCaption, "mode", GoSequencerProcessor::playModeNames(), "playMode", modeAttachment);
    setUpCombo  (sequencerTab, rateBox, rateCaption, "step rate", GoSequencerProcessor::rateNames(), "rate", rateAttachment);
    setUpSlider (sequencerTab, noteSlider, noteCaption, "note", "note", noteAttachment);
    setUpSlider (sequencerTab, gateSlider, gateCaption, "gate", "gate", gateAttachment);
    setUpSlider (sequencerTab, lifeSlider, lifeCaption, "stone life", "stoneLife", lifeAttachment);
    setUpCombo  (sequencerTab, lifeModeBox, lifeModeCaption, "life counts",
                 GoSequencerProcessor::lifeModeNames(), "lifeMode", lifeModeAttachment);
    setUpToggle (sequencerTab, freeRunButton, "Free run", "freeRun", freeRunAttachment);
    setUpSlider (sequencerTab, tempoSlider, tempoCaption, "free tempo", "tempo", tempoAttachment);
    setUpSlider (sequencerTab, spreadSlider, spreadCaption, "spread", "ringSpread", spreadAttachment);

    //  ---- board ------------------------------------------------------------
    setUpToggle (boardTab, koButton, "Ko rule", "koRule", koAttachment);
    setUpToggle (boardTab, selfCaptureButton, "Self capture", "selfCapture", selfCaptureAttachment);

    pathButton.setClickingTogglesState (true);
    pathButton.setToggleState (board.getShowPath(), juce::dontSendNotification);
    setUpButton (boardTab, pathButton, "Show path", [this] { board.setShowPath (pathButton.getToggleState()); });

    setUpButton (boardTab, clearButton, "Clear board", [this]
    {
        processor.clearBoard();
        showMessage ("board cleared");
        board.repaint();
    });

    //  the separators are UTF-8: JUCE must be told, or they arrive as Latin-1
    hintLabel.setText (juce::String (juce::CharPointer_UTF8 (
                           "click to place  \xc2\xb7  click a stone to lift it  \xc2\xb7  drop an .sgf anywhere")),
                       juce::dontSendNotification);
    setUpText (boardTab, hintLabel, juce::Justification::topLeft);

    //  ---- channels ---------------------------------------------------------
    //  velocity follows the colour in every mode; the channel sliders the mode
    //  is not routing by are greyed out by timerCallback()
    setUpSlider (channelsTab, blackVelocitySlider, blackVelocityCaption, "black velocity", "blackVelocity", blackVelocityAttachment);
    setUpSlider (channelsTab, whiteVelocitySlider, whiteVelocityCaption, "white velocity", "whiteVelocity", whiteVelocityAttachment);
    setUpSlider (channelsTab, blackChannelSlider, blackChannelCaption, "black channel", "blackChannel",
                 blackChannelAttachment, narrowValueWidth);
    setUpSlider (channelsTab, whiteChannelSlider, whiteChannelCaption, "white channel", "whiteChannel",
                 whiteChannelAttachment, narrowValueWidth);

    for (int h = 0; h < headChannels; ++h)
        setUpSlider (channelsTab, headChannelSliders[(size_t) h], headChannelCaptions[(size_t) h],
                     "head " + juce::String (h + 1),
                     "headChannel" + juce::String (h + 1),
                     headChannelAttachments[(size_t) h], narrowValueWidth);

    //  ---- game record ------------------------------------------------------
    setUpText (gameTab, gameTitleLabel, juce::Justification::centredLeft);
    setUpText (gameTab, gameDetailLabel, juce::Justification::centredRight);

    setUpButton (gameTab, loadButton, juce::String (juce::CharPointer_UTF8 ("Load SGF\xe2\x80\xa6")),
                 [this] { openSgfChooser(); });

    setUpButton (gameTab, unloadButton, "Unload", [this]
    {
        processor.clearGame();          //  a run ends here too: it turns its own switch off
        refreshGameDisplay();
        showMessage ("game record unloaded");
    });

    setUpCombo  (gameTab, gameRateBox, gameRateCaption, "move rate", GoSequencerProcessor::gameRateNames(),
                 "gameRate", gameRateAttachment);
    setUpSlider (gameTab, waveGapSlider, waveGapCaption, "wave gap", "waveGap", waveGapAttachment);
    waveGapSlider.setEnabled (processor.waveReplayOn());          //  synced again every tick; this is just the initial state

    setUpToggle (gameTab, runGameButton, "Run game", "gameRun", runGameAttachment);
    setUpToggle (gameTab, loopGameButton, "Loop", "gameLoop", loopGameAttachment);
    setUpToggle (gameTab, waveReplayButton, "Wave replay", "waveReplay", waveReplayAttachment);

    moveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    moveSlider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, 110, captionHeight);
    moveSlider.setRepaintsOnMouseActivity (true);
    moveSlider.setTitle ("position in the record");
    moveSlider.setRange (0.0, 1.0, 1.0);
    moveSlider.textFromValueFunction = [this] (double value)
    {
        return "move " + juce::String ((int) value) + " / " + juce::String (processor.gameMoveCount());
    };
    moveSlider.onValueChange = [this]
    {
        const int wanted = (int) moveSlider.getValue();

        if (wanted != processor.gamePosition())
            processor.setGamePosition (wanted);
    };
    addToTab (gameTab, moveSlider);
    setUpCaption (gameTab, moveCaption, "position");

    setUpButton (gameTab, previousMoveButton, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")),
                 [this] { processor.nudgeGamePosition (-1); refreshGameDisplay(); });
    previousMoveButton.setTitle ("previous move");

    setUpButton (gameTab, nextMoveButton, juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")),
                 [this] { processor.nudgeGamePosition (1); refreshGameDisplay(); });
    nextMoveButton.setTitle ("next move");

    //  ---- self-play --------------------------------------------------------
    //  The record is written rather than loaded, so Move Rate, Run and Loop on
    //  the game tab drive a generated game exactly as they drive a loaded one.
    setUpToggle (aiTab, aiPlayButton, "AI self-play", "aiPlay", aiPlayAttachment);
    setUpSlider (aiTab, aiMovesSlider, aiMovesCaption, "game length", "aiMoves", aiMovesAttachment);
    setUpSlider (aiTab, aiVariationSlider, aiVariationCaption, "variation", "aiVariation", aiVariationAttachment);
    setUpSlider (aiTab, aiSeedSlider, aiSeedCaption, "seed", "aiSeed", aiSeedAttachment);

    for (auto* slider : { &aiMovesSlider, &aiVariationSlider, &aiSeedSlider })
        slider->setEnabled (processor.aiSelfPlay());               //  as above: the tick keeps these in step

    //  The opening. A position is not an opening - the order decides what is
    //  captured - so this takes the ten stones the board was clicked in, not
    //  the ten stones standing on it.
    setUpCaption (aiTab, openingCaption, "opening");

    setUpButton (aiTab, openingFromBoardButton, "From board", [this]
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

    setUpButton (aiTab, openingBookButton, "Use book", [this]
    {
        processor.useBookOpening();
        refreshOpeningDisplay();
        showMessage ("back to the book opening");
    });

    setUpText (aiTab, openingLabel, juce::Justification::topLeft);

    refreshOpeningDisplay();
    refreshGameDisplay();

    //  Sliders build their value boxes - colours, font, alignment - from the
    //  look-and-feel they have when it changes, and everything above was added
    //  after it was set. Tell them again now that they are all in, or the value
    //  boxes keep the default dark scheme's white text.
    sendLookAndFeelChange();

    showTab ((int) processor.apvts.state.getProperty (activeTabProperty, (int) sequencerTab));

    setResizable (true, true);
    setResizeLimits (780, 420, 1800, 1200);
    setSize (1020, 620);

    startTimerHz (20);
}

GoSequencerEditor::~GoSequencerEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void GoSequencerEditor::addToTab (Tab tab, juce::Component& component)
{
    if (tab == pinned)
    {
        addAndMakeVisible (component);
        return;
    }

    addChildComponent (component);
    tabMembers[(size_t) tab].push_back (&component);
}

void GoSequencerEditor::showTab (int tab)
{
    currentTab = juce::jlimit (0, tabCount - 1, tab);

    for (int t = 0; t < tabCount; ++t)
    {
        tabButtons[(size_t) t].setToggleState (t == currentTab, juce::dontSendNotification);

        for (auto* component : tabMembers[(size_t) t])
            component->setVisible (t == currentTab);
    }

    processor.apvts.state.setProperty (activeTabProperty, currentTab, nullptr);
}

void GoSequencerEditor::setDarkMode (bool dark)
{
    theme::setDark (dark);
    lookAndFeel.applyColours();

    for (auto* label : dimLabels)
        label->setColour (juce::Label::textColourId, theme::dimText);

    //  these colour themselves ink or dimText depending on state, not always
    //  dimText, so they need re-reading rather than the flat reset above
    refreshOpeningDisplay();
    refreshGameDisplay();

    processor.apvts.state.setProperty (darkModeProperty, dark, nullptr);

    sendLookAndFeelChange();
    board.repaint();
    repaint();
}

void GoSequencerEditor::setUpCaption (Tab tab, juce::Label& caption, const juce::String& text)
{
    caption.setText (text.toUpperCase(), juce::dontSendNotification);
    caption.setFont (captionFont());
    caption.setColour (juce::Label::textColourId, theme::dimText);
    caption.setBorderSize ({ 0, 0, 0, 0 });
    caption.setJustificationType (juce::Justification::centredLeft);
    addToTab (tab, caption);
    dimLabels.push_back (&caption);
}

void GoSequencerEditor::setUpText (Tab tab, juce::Label& label, juce::Justification justification)
{
    label.setFont (valueFont());
    label.setColour (juce::Label::textColourId, theme::dimText);
    label.setBorderSize ({ 0, 0, 0, 0 });
    label.setJustificationType (justification);
    addToTab (tab, label);
    dimLabels.push_back (&label);
}

void GoSequencerEditor::setUpSlider (Tab tab, juce::Slider& slider, juce::Label& caption,
                                     const juce::String& text, const juce::String& parameterID,
                                     std::unique_ptr<SliderAttachment>& attachment, int valueWidth)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxAbove, false, valueWidth, captionHeight);
    slider.setRepaintsOnMouseActivity (true);
    slider.setTitle (text);
    addToTab (tab, slider);

    //  after the slider, so the caption sits over the cell the slider spans
    setUpCaption (tab, caption, text);

    attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, slider);
}

void GoSequencerEditor::setUpCombo (Tab tab, juce::ComboBox& box, juce::Label& caption,
                                    const juce::String& text, const juce::StringArray& items,
                                    const juce::String& parameterID,
                                    std::unique_ptr<ComboBoxAttachment>& attachment)
{
    box.addItemList (items, 1);
    box.setTitle (text);
    addToTab (tab, box);

    setUpCaption (tab, caption, text);

    attachment = std::make_unique<ComboBoxAttachment> (processor.apvts, parameterID, box);
}

void GoSequencerEditor::setUpToggle (Tab tab, juce::TextButton& button, const juce::String& text,
                                     const juce::String& parameterID,
                                     std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    addToTab (tab, button);

    attachment = std::make_unique<ButtonAttachment> (processor.apvts, parameterID, button);
}

void GoSequencerEditor::setUpButton (Tab tab, juce::TextButton& button, const juce::String& text,
                                     std::function<void()> onClick)
{
    button.setButtonText (text);
    button.onClick = std::move (onClick);
    addToTab (tab, button);
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
    repaint (headerBounds);
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
void GoSequencerEditor::resized()
{
    auto area = getLocalBounds().reduced (margin);

    //  ---- the board: as big as the height allows, its two dropdowns under it
    const int boardSide = juce::jmax (160, juce::jmin (area.getHeight() - rowGap - cellHeight,
                                                       area.getWidth() - columnGap - minControlsWidth));

    auto boardColumn = area.removeFromLeft (boardSide);
    area.removeFromLeft (columnGap);

    board.setBounds (boardColumn.removeFromTop (boardSide));
    boardColumn.removeFromTop (rowGap);

    {
        auto cells = columns (boardColumn.removeFromTop (cellHeight), 2);
        placeLabelled (cells[0], sizeCaption, sizeBox);
        placeLabelled (cells[1], colourCaption, colourBox);
    }

    //  ---- the controls column: title and status, the tabs, then one tab ----
    auto column = area.removeFromLeft (juce::jmin (area.getWidth(), maxControlsWidth));

    headerBounds = column.removeFromTop (titleHeight + statusHeight);
    column.removeFromTop (headerGap);

    {
        //  top right of the title line, whichever tab is open
        auto titleLine = headerBounds.withHeight (titleHeight);
        darkModeButton.setBounds (titleLine.removeFromRight (pillWidth (darkModeButton)).reduced (0, 2));
    }

    {
        auto strip = column.removeFromTop (tabHeight);

        for (auto& tab : tabButtons)
        {
            tab.setBounds (strip.removeFromLeft (textWidth (tabFont(), tab.getButtonText().toUpperCase()) + 2));
            strip.removeFromLeft (tabGap);
        }
    }

    column.removeFromTop (headerGap);

    //  Every tab is laid out in the same rectangle, whether it is open or not,
    //  so showTab() only has to flip visibility.

    //  ---- sequencer --------------------------------------------------------
    {
        auto rows = column;

        auto cells = columns (nextRow (rows, cellHeight), 2);
        placeLabelled (cells[0], modeCaption, modeBox);
        placeLabelled (cells[1], rateCaption, rateBox);

        cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], noteCaption, noteSlider);
        placeSlider (cells[1], gateCaption, gateSlider);

        cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], lifeCaption, lifeSlider);
        placeLabelled (cells[1], lifeModeCaption, lifeModeBox);

        cells = columns (nextRow (rows, cellHeight), 2);
        placeControl (cells[0], freeRunButton);
        placeSlider (cells[1], tempoCaption, tempoSlider);

        //  spread only means anything with more than one head, so it goes last
        cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], spreadCaption, spreadSlider);
    }

    //  ---- board ------------------------------------------------------------
    {
        auto rows = column;

        flow (nextRow (rows, controlHeight), { &koButton, &selfCaptureButton, &pathButton, &clearButton });
        hintLabel.setBounds (rows.removeFromTop (textLinesHeight));
    }

    //  ---- channels ---------------------------------------------------------
    {
        auto rows = column;

        auto cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], blackVelocityCaption, blackVelocitySlider);
        placeSlider (cells[1], whiteVelocityCaption, whiteVelocitySlider);

        cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], blackChannelCaption, blackChannelSlider);
        placeSlider (cells[1], whiteChannelCaption, whiteChannelSlider);

        //  one per playhead, three to a row - three rows for the nine rings of a 19x19
        for (int first = 0; first < headChannels; first += 3)
        {
            cells = columns (nextRow (rows, cellHeight), 3);

            for (int h = first; h < juce::jmin (first + 3, headChannels); ++h)
                placeSlider (cells[(size_t) (h - first)], headChannelCaptions[(size_t) h],
                             headChannelSliders[(size_t) h]);
        }
    }

    //  ---- game record ------------------------------------------------------
    {
        auto rows = column;

        {
            auto line = nextRow (rows, 20);
            gameTitleLabel.setBounds (line.removeFromLeft (line.getWidth() / 2));
            gameDetailLabel.setBounds (line);
        }

        flow (nextRow (rows, controlHeight), { &loadButton, &unloadButton });

        {
            //  Wave Gap only means anything once Wave Replay is on -
            //  timerCallback() greys the slider out the rest of the time
            auto cells = columns (nextRow (rows, cellHeight), 2);
            placeLabelled (cells[0], gameRateCaption, gameRateBox);
            placeSlider (cells[1], waveGapCaption, waveGapSlider);
        }

        flow (nextRow (rows, controlHeight), { &runGameButton, &loopGameButton, &waveReplayButton });

        {
            auto row = nextRow (rows, cellHeight);
            auto stepper = row.removeFromRight (2 * stepButtonWidth + pillGap);
            row.removeFromRight (columnGutter);
            placeSlider (row, moveCaption, moveSlider);

            stepper.removeFromTop (captionHeight);
            auto buttons = stepper.removeFromTop (controlHeight);
            previousMoveButton.setBounds (buttons.removeFromLeft (stepButtonWidth));
            buttons.removeFromLeft (pillGap);
            nextMoveButton.setBounds (buttons.removeFromLeft (stepButtonWidth));
        }
    }

    //  ---- self-play --------------------------------------------------------
    {
        auto rows = column;

        //  the switch and the three numbers a game is written from; the numbers
        //  are greyed out by timerCallback() while the switch is off
        auto cells = columns (nextRow (rows, cellHeight), 2);
        placeControl (cells[0], aiPlayButton);
        placeSlider (cells[1], aiMovesCaption, aiMovesSlider);

        cells = columns (nextRow (rows, cellHeight), 2);
        placeSlider (cells[0], aiVariationCaption, aiVariationSlider);
        placeSlider (cells[1], aiSeedCaption, aiSeedSlider);

        {
            //  two buttons, and under them the line that says which opening is in force
            auto row = nextRow (rows, cellHeight);
            openingCaption.setBounds (row.removeFromTop (captionHeight));
            flow (row.removeFromTop (controlHeight), { &openingFromBoardButton, &openingBookButton });
        }

        openingLabel.setBounds (rows.removeFromTop (textLinesHeight));
    }
}

//==============================================================================
void GoSequencerEditor::timerCallback()
{
    if (messageCountdown > 0)
        --messageCountdown;

    if (messageCountdown == 0 && message.isNotEmpty())
        message.clear();

    const int heads = processor.headCount();

    if (heads != lastModeShown)
    {
        lastModeShown = heads;

        const bool multi = (heads > 1);

        //  spread pushes the heads apart in pitch, so it has nothing to say
        //  until there is more than one of them; velocity follows the colour
        //  in every mode
        spreadSlider.setEnabled (multi);

        //  spiral routes by colour, the multi head modes by playhead, and a
        //  9x9 has fewer rings than a 13x13 - so grey out what this mode is
        //  not using rather than let it look assignable
        blackChannelSlider.setEnabled (! multi);
        whiteChannelSlider.setEnabled (! multi);

        for (int h = 0; h < headChannels; ++h)
            headChannelSliders[(size_t) h].setEnabled (multi && h < heads);
    }

    const bool waveReplay = processor.waveReplayOn();

    if (waveReplay != lastWaveReplayShown)
    {
        lastWaveReplayShown = waveReplay;

        //  Wave Gap only means anything once Wave Replay is on. Loop still
        //  does - it wraps the record without clearing the board, which is
        //  what keeps the wave running - so that switch stays live.
        waveGapSlider.setEnabled (waveReplay);
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

        for (auto* slider : { &aiMovesSlider, &aiVariationSlider, &aiSeedSlider })
            slider->setEnabled (ai);

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

    const int position = processor.gamePosition();

    if (position != lastMoveShown)
    {
        lastMoveShown = position;

        if (! moveSlider.isMouseButtonDown())
            moveSlider.setValue ((double) position, juce::dontSendNotification);
    }

    repaint (headerBounds);
}

void GoSequencerEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = headerBounds;
    auto titleLine = header.removeFromTop (titleHeight);

    const juce::String title ("GO SEQUENCER");

    g.setFont (titleFont());
    g.setColour (theme::ink);
    g.drawText (title, titleLine.removeFromLeft (textWidth (titleFont(), title) + 2),
                juce::Justification::centredLeft, false);

    //  the stone the next click will place
    titleLine.removeFromLeft (10);
    BoardComponent::drawStone (g, titleLine.removeFromLeft (12).getCentre().toFloat(), 5.5f,
                               processor.colourForNextMove() == go::Stone::black);

    g.setFont (statusFont());

    if (message.isNotEmpty())
    {
        g.setColour (theme::accent);
        g.drawText (message, header, juce::Justification::centredLeft, true);
    }
    else
    {
        const juce::String dot (juce::CharPointer_UTF8 ("  \xc2\xb7  "));

        const int steps = juce::jmax (1, processor.cycleSteps());
        const int step = juce::jlimit (0, steps - 1, processor.currentStep());

        juce::String status;
        status << "step " << (step + 1) << "/" << steps;

        if (processor.isQuads())
            status << dot << "4 quadrants";
        else if (processor.isPolyrhythm())
            status << dot << processor.ringCount() << " rings";

        status << dot << "captured  black " << processor.capturedBlack()
               << "  white " << processor.capturedWhite();

        if (processor.hasGame())
            status << dot << "move " << processor.gamePosition() << "/" << processor.gameMoveCount();

        status << dot << (processor.isRunning() ? "running" : "stopped");

        g.setColour (theme::dimText);
        g.drawText (status, header, juce::Justification::centredLeft, true);
    }

    if (dragHighlight)
    {
        g.setColour (theme::accent);
        g.drawRect (getLocalBounds(), 2);
    }
}
