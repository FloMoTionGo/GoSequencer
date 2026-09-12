#include "PluginEditor.h"

//==============================================================================
GoLookAndFeel::GoLookAndFeel()
{
    setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());

    setColour (juce::ResizableWindow::backgroundColourId, theme::background);

    setColour (juce::Label::textColourId,                 theme::text);

    setColour (juce::Slider::backgroundColourId,          theme::panel);
    setColour (juce::Slider::trackColourId,               theme::accent.withAlpha (0.75f));
    setColour (juce::Slider::thumbColourId,               theme::text);
    setColour (juce::Slider::textBoxTextColourId,         theme::text);
    setColour (juce::Slider::textBoxBackgroundColourId,   theme::panel);
    setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);

    setColour (juce::ComboBox::backgroundColourId,        theme::panel);
    setColour (juce::ComboBox::textColourId,              theme::text);
    setColour (juce::ComboBox::outlineColourId,           theme::panelBright);
    setColour (juce::ComboBox::arrowColourId,             theme::dimText);

    setColour (juce::PopupMenu::backgroundColourId,       theme::panel);
    setColour (juce::PopupMenu::textColourId,             theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent);

    setColour (juce::TextButton::buttonColourId,          theme::panel);
    setColour (juce::TextButton::buttonOnColourId,        theme::accent);
    setColour (juce::TextButton::textColourOffId,         theme::dimText);
    setColour (juce::TextButton::textColourOnId,          juce::Colours::white);
}

//==============================================================================
namespace
{
    constexpr int rowHeight = 44, gap = 8, sectionHeight = 18;

    //  what opening the channel fold out adds: two rows of sliders
    constexpr int channelBlockHeight = 2 * (rowHeight + gap);

    //  the Wave Replay toggle + Wave Gap slider: one permanent extra row
    //  in the game record section, always present (not a fold out)
    constexpr int waveRowHeight = rowHeight + gap;

    //  the ValueTree property the open state rides along in, so the fold out is
    //  still open when the session comes back
    const juce::Identifier channelsOpenProperty { "channelsOpen" };
}

//==============================================================================
GoSequencerEditor::GoSequencerEditor (GoSequencerProcessor& p)
    : AudioProcessorEditor (&p), processor (p), board (p)
{
    setLookAndFeel (&lookAndFeel);

    lastSgfDirectory = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);

    addAndMakeVisible (board);
    board.onMessage = [this] (const juce::String& text) { showMessage (text); };

    setUpSection (sequencerSection, "SEQUENCER");
    setUpSection (gameSection, "GAME RECORD");

    setUpCombo  (modeBox, modeCaption, "mode", GoSequencerProcessor::playModeNames(), "playMode", modeAttachment);
    setUpCombo  (rateBox, rateCaption, "step rate", GoSequencerProcessor::rateNames(), "rate", rateAttachment);
    setUpSlider (noteSlider, noteCaption, "note", "note", noteAttachment);
    setUpSlider (gateSlider, gateCaption, "gate", "gate", gateAttachment);
    setUpSlider (tempoSlider, tempoCaption, "free tempo", "tempo", tempoAttachment);

    setUpSlider (blackVelocitySlider, blackVelocityCaption, "black velocity", "blackVelocity", blackVelocityAttachment);
    setUpSlider (whiteVelocitySlider, whiteVelocityCaption, "white velocity", "whiteVelocity", whiteVelocityAttachment);

    setUpSlider (spreadSlider, spreadCaption, "spread", "ringSpread", spreadAttachment);
    setUpSlider (lifeSlider, lifeCaption, "stone life", "stoneLife", lifeAttachment);
    setUpCombo  (lifeModeBox, lifeModeCaption, "life counts",
                 GoSequencerProcessor::lifeModeNames(), "lifeMode", lifeModeAttachment);

    //  ---- MIDI channels, behind the fold out -------------------------------
    setUpDisclosure (channelsToggle, "MIDI CHANNELS");

    setUpSlider (blackChannelSlider, blackChannelCaption, "black channel", "blackChannel", blackChannelAttachment);
    setUpSlider (whiteChannelSlider, whiteChannelCaption, "white channel", "whiteChannel", whiteChannelAttachment);

    for (int h = 0; h < headChannels; ++h)
        setUpSlider (headChannelSliders[(size_t) h], headChannelCaptions[(size_t) h],
                     "head " + juce::String (h + 1) + " channel",
                     "headChannel" + juce::String (h + 1),
                     headChannelAttachments[(size_t) h]);

    channelsToggle.setToggleState ((bool) processor.apvts.state.getProperty (channelsOpenProperty, false),
                                   juce::dontSendNotification);

    channelsToggle.onClick = [this]
    {
        processor.apvts.state.setProperty (channelsOpenProperty, channelsOpen(), nullptr);
        refreshChannelSection (true);
    };

    setUpCombo  (sizeBox, sizeCaption, "board", GoSequencerProcessor::boardSizeNames(), "boardSize", sizeAttachment);
    setUpCombo  (colourBox, colourCaption, "place", { "Alternate", "Black", "White" }, "colourMode", colourAttachment);

    setUpToggle (koButton, "Ko rule", "koRule", koAttachment);
    setUpToggle (selfCaptureButton, "Self capture", "selfCapture", selfCaptureAttachment);
    setUpToggle (freeRunButton, "Free run", "freeRun", freeRunAttachment);

    pathButton.setButtonText ("Show path");
    pathButton.setClickingTogglesState (true);
    pathButton.setToggleState (board.getShowPath(), juce::dontSendNotification);
    pathButton.onClick = [this] { board.setShowPath (pathButton.getToggleState()); };
    addAndMakeVisible (pathButton);

    clearButton.setButtonText ("Clear board");
    clearButton.onClick = [this]
    {
        processor.clearBoard();
        showMessage ("board cleared");
        board.repaint();
    };
    addAndMakeVisible (clearButton);

    //  ---- game record ------------------------------------------------------
    loadButton.setButtonText ("Load SGF...");
    loadButton.onClick = [this] { openSgfChooser(); };
    addAndMakeVisible (loadButton);

    setUpCombo  (gameRateBox, gameRateCaption, "move rate", GoSequencerProcessor::gameRateNames(),
                 "gameRate", gameRateAttachment);
    setUpToggle (runGameButton, "Run game", "gameRun", runGameAttachment);
    setUpToggle (loopGameButton, "Loop", "gameLoop", loopGameAttachment);

    setUpToggle (waveReplayButton, "Wave replay", "waveReplay", waveReplayAttachment);
    setUpSlider (waveGapSlider, waveGapCaption, "wave gap", "waveGap", waveGapAttachment);
    waveGapSlider.setEnabled (processor.waveReplayOn());          //  synced again every tick; this is just the initial state

    unloadButton.setButtonText ("Unload");
    unloadButton.onClick = [this]
    {
        processor.clearGame();
        refreshGameDisplay();
        showMessage ("game record unloaded");
    };
    addAndMakeVisible (unloadButton);

    previousMoveButton.setButtonText ("<");
    previousMoveButton.onClick = [this] { processor.nudgeGamePosition (-1); refreshGameDisplay(); };
    addAndMakeVisible (previousMoveButton);

    nextMoveButton.setButtonText (">");
    nextMoveButton.onClick = [this] { processor.nudgeGamePosition (1); refreshGameDisplay(); };
    addAndMakeVisible (nextMoveButton);

    moveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    moveSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 104, 20);
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
    addAndMakeVisible (moveSlider);

    moveCaption.setText ("position in the record", juce::dontSendNotification);
    moveCaption.setColour (juce::Label::textColourId, theme::dimText);
    moveCaption.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (moveCaption);

    gameTitleLabel.setJustificationType (juce::Justification::centredLeft);
    gameTitleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (gameTitleLabel);

    gameDetailLabel.setJustificationType (juce::Justification::centredRight);
    gameDetailLabel.setColour (juce::Label::textColourId, theme::dimText);
    addAndMakeVisible (gameDetailLabel);

    for (auto* blank : { &blankCaption1, &blankCaption2, &blankCaption3, &blankCaption4, &blankCaption5,
                         &blankCaption6, &blankCaption7, &blankCaption8, &blankCaption9, &blankCaption10 })
        addAndMakeVisible (*blank);

    //  the separators are UTF-8: JUCE must be told, or they arrive as Latin-1
    hintLabel.setText (juce::String (juce::CharPointer_UTF8 (
                           "click to place  \xc2\xb7  click a stone to lift it  \xc2\xb7  drop an .sgf anywhere")),
                       juce::dontSendNotification);
    hintLabel.setColour (juce::Label::textColourId, theme::dimText);
    hintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (hintLabel);

    refreshGameDisplay();

    setResizable (true, true);
    setResizeLimits (660, 922 + waveRowHeight, 1500, 1900 + channelBlockHeight + waveRowHeight);
    setSize (740, 1042 + waveRowHeight + (channelsOpen() ? channelBlockHeight : 0));

    refreshChannelSection (false);       //  the window is already the right height

    startTimerHz (20);
}

GoSequencerEditor::~GoSequencerEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void GoSequencerEditor::setUpSection (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setColour (juce::Label::textColourId, theme::accent);
    label.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    label.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (label);
}

void GoSequencerEditor::setUpDisclosure (juce::TextButton& button, const juce::String& text)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);

    //  a section header that can be clicked: no button face, just accent text
    button.setColour (juce::TextButton::buttonColourId,   juce::Colours::transparentBlack);
    button.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    button.setColour (juce::TextButton::textColourOffId,  theme::accent);
    button.setColour (juce::TextButton::textColourOnId,   theme::accent);

    addAndMakeVisible (button);
}

bool GoSequencerEditor::channelsOpen() const
{
    return channelsToggle.getToggleState();
}

void GoSequencerEditor::refreshChannelSection (bool resizeWindow)
{
    const bool open = channelsOpen();

    //  the caret is UTF-8, so JUCE has to be told
    channelsToggle.setButtonText (juce::String (juce::CharPointer_UTF8 (
        open ? "\xe2\x96\xbc  MIDI CHANNELS" : "\xe2\x96\xba  MIDI CHANNELS")));

    blackChannelSlider.setVisible (open);
    blackChannelCaption.setVisible (open);
    whiteChannelSlider.setVisible (open);
    whiteChannelCaption.setVisible (open);

    for (int h = 0; h < headChannels; ++h)
    {
        headChannelSliders[(size_t) h].setVisible (open);
        headChannelCaptions[(size_t) h].setVisible (open);
    }

    lastModeShown = -1;                 //  the enables are re-read on the next tick

    if (resizeWindow)
        setSize (getWidth(), getHeight() + (open ? channelBlockHeight : -channelBlockHeight));
    else
        resized();
}

void GoSequencerEditor::setUpSlider (juce::Slider& slider, juce::Label& caption,
                                     const juce::String& text, const juce::String& parameterID,
                                     std::unique_ptr<SliderAttachment>& attachment)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, theme::dimText);
    caption.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (processor.apvts, parameterID, slider);
}

void GoSequencerEditor::setUpCombo (juce::ComboBox& box, juce::Label& caption,
                                    const juce::String& text, const juce::StringArray& items,
                                    const juce::String& parameterID,
                                    std::unique_ptr<ComboBoxAttachment>& attachment)
{
    box.addItemList (items, 1);
    addAndMakeVisible (box);

    caption.setText (text, juce::dontSendNotification);
    caption.setColour (juce::Label::textColourId, theme::dimText);
    caption.setJustificationType (juce::Justification::bottomLeft);
    addAndMakeVisible (caption);

    attachment = std::make_unique<ComboBoxAttachment> (processor.apvts, parameterID, box);
}

void GoSequencerEditor::setUpToggle (juce::TextButton& button, const juce::String& text,
                                     const juce::String& parameterID,
                                     std::unique_ptr<ButtonAttachment>& attachment)
{
    button.setButtonText (text);
    button.setClickingTogglesState (true);
    addAndMakeVisible (button);

    attachment = std::make_unique<ButtonAttachment> (processor.apvts, parameterID, button);
}

void GoSequencerEditor::placeLabelled (juce::Rectangle<int> cell, juce::Label& caption, juce::Component& control)
{
    caption.setBounds (cell.removeFromTop (16));
    control.setBounds (cell.removeFromTop (24));
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

void GoSequencerEditor::refreshGameDisplay()
{
    const bool has = processor.hasGame();

    gameTitleLabel.setText (has ? processor.gameTitle() : "no record loaded", juce::dontSendNotification);
    gameTitleLabel.setColour (juce::Label::textColourId, has ? theme::text : theme::dimText);
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
    auto area = getLocalBounds().reduced (14);

    headerBounds = area.removeFromTop (42);
    area.removeFromTop (4);

    //  three section headers, six slider rows, the hint and the record's title
    //  line, the wave replay row, plus the fold out when it is open
    auto controls = area.removeFromBottom (3 * sectionHeight + 6 * rowHeight + 10 * gap + 44
                                             + waveRowHeight
                                             + (channelsOpen() ? channelBlockHeight : 0));
    area.removeFromBottom (8);

    board.setBounds (area);

    auto nextRow = [&controls] (int height)
    {
        auto row = controls.removeFromTop (height);

        if (! controls.isEmpty())
            controls.removeFromTop (gap);

        return row;
    };

    auto columns = [] (juce::Rectangle<int> row, int count)
    {
        std::vector<juce::Rectangle<int>> out;
        const int columnWidth = row.getWidth() / count;

        for (int i = 0; i < count; ++i)
        {
            auto cell = row.removeFromLeft (i == count - 1 ? row.getWidth() : columnWidth);
            out.push_back (cell.withTrimmedRight (10));
        }

        return out;
    };

    sequencerSection.setBounds (nextRow (sectionHeight));

    {
        auto cells = columns (nextRow (rowHeight), 4);
        placeLabelled (cells[0], rateCaption, rateBox);
        placeLabelled (cells[1], noteCaption, noteSlider);
        placeLabelled (cells[2], gateCaption, gateSlider);
        placeLabelled (cells[3], tempoCaption, tempoSlider);
    }

    {
        auto cells = columns (nextRow (rowHeight), 4);
        placeLabelled (cells[0], modeCaption, modeBox);
        placeLabelled (cells[1], spreadCaption, spreadSlider);
        placeLabelled (cells[2], lifeCaption, lifeSlider);
        placeLabelled (cells[3], lifeModeCaption, lifeModeBox);
    }

    {
        auto cells = columns (nextRow (rowHeight), 4);
        placeLabelled (cells[0], blackVelocityCaption, blackVelocitySlider);
        placeLabelled (cells[1], whiteVelocityCaption, whiteVelocitySlider);
        placeLabelled (cells[2], sizeCaption, sizeBox);
        placeLabelled (cells[3], colourCaption, colourBox);
    }

    //  five switches share one row
    {
        auto cells = columns (nextRow (rowHeight), 5);
        placeLabelled (cells[0], blankCaption1, koButton);
        placeLabelled (cells[1], blankCaption2, selfCaptureButton);
        placeLabelled (cells[2], blankCaption3, freeRunButton);
        placeLabelled (cells[3], blankCaption4, pathButton);
        placeLabelled (cells[4], blankCaption5, clearButton);
    }

    //  ---- the channel fold out ---------------------------------------------
    //  The header is always there; the two rows under it only take space while
    //  it is open, which is what keeps the window short for a one channel set.
    channelsToggle.setBounds (nextRow (sectionHeight).removeFromLeft (190));

    if (channelsOpen())
    {
        {
            auto cells = columns (nextRow (rowHeight), 4);
            placeLabelled (cells[0], blackChannelCaption, blackChannelSlider);
            placeLabelled (cells[1], whiteChannelCaption, whiteChannelSlider);
            placeLabelled (cells[2], headChannelCaptions[0], headChannelSliders[0]);
            placeLabelled (cells[3], headChannelCaptions[1], headChannelSliders[1]);
        }

        {
            auto cells = columns (nextRow (rowHeight), 4);

            for (int h = 2; h < headChannels; ++h)
                placeLabelled (cells[(size_t) (h - 2)], headChannelCaptions[(size_t) h],
                               headChannelSliders[(size_t) h]);
        }
    }

    hintLabel.setBounds (nextRow (22));

    gameSection.setBounds (nextRow (sectionHeight));

    {
        auto row = nextRow (22);
        gameTitleLabel.setBounds (row.removeFromLeft (row.getWidth() / 2));
        gameDetailLabel.setBounds (row.withTrimmedRight (10));
    }

    {
        auto cells = columns (nextRow (rowHeight), 4);
        placeLabelled (cells[0], blankCaption6, loadButton);
        placeLabelled (cells[1], gameRateCaption, gameRateBox);
        placeLabelled (cells[2], blankCaption7, runGameButton);
        placeLabelled (cells[3], blankCaption8, loopGameButton);
    }

    {
        //  Wave Replay ignores Loop, and Wave Gap only means anything once
        //  it's on - timerCallback() greys the slider out the rest of the time
        auto cells = columns (nextRow (rowHeight), 2);
        placeLabelled (cells[0], blankCaption10, waveReplayButton);
        placeLabelled (cells[1], waveGapCaption, waveGapSlider);
    }

    {
        auto row = nextRow (rowHeight);
        auto sliderCell = row.removeFromLeft (row.getWidth() / 2).withTrimmedRight (10);
        placeLabelled (sliderCell, moveCaption, moveSlider);

        auto cells = columns (row, 2);

        auto stepCell = cells[0];
        stepCell.removeFromTop (16);
        auto stepRow = stepCell.removeFromTop (24);
        previousMoveButton.setBounds (stepRow.removeFromLeft (stepRow.getWidth() / 2 - 3));
        nextMoveButton.setBounds (stepRow.withTrimmedLeft (3));

        placeLabelled (cells[1], blankCaption9, unloadButton);
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

    g.setColour (theme::text);
    g.setFont (juce::Font (juce::FontOptions (21.0f, juce::Font::bold)));
    g.drawText ("GO SEQUENCER", header.removeFromLeft (200), juce::Justification::centredLeft);

    //  the stone the next click will place
    auto swatch = header.removeFromLeft (52).withSizeKeepingCentre (18, 18);
    BoardComponent::drawStone (g, swatch.getCentre().toFloat(), 9.0f,
                               processor.colourForNextMove() == go::Stone::black);

    g.setFont (12.0f);

    if (message.isNotEmpty())
    {
        g.setColour (theme::accent);
        g.drawText (message, header, juce::Justification::centredRight);
    }
    else
    {
        g.setColour (theme::dimText);

        const int steps = juce::jmax (1, processor.cycleSteps());
        const int step = juce::jlimit (0, steps - 1, processor.currentStep());

        juce::String status;
        status << "step " << (step + 1) << "/" << steps;

        if (processor.isQuads())
            status << "   4 quadrants";
        else if (processor.isPolyrhythm())
            status << "   " << processor.ringCount() << " rings";

        status << "   captured  black " << processor.capturedBlack()
               << "  white " << processor.capturedWhite();

        if (processor.hasGame())
            status << "   move " << processor.gamePosition() << "/" << processor.gameMoveCount();

        status << (processor.isRunning() ? "   running" : "   stopped");

        g.drawText (status, header, juce::Justification::centredRight);
    }

    if (dragHighlight)
    {
        g.setColour (theme::accent.withAlpha (0.8f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 5.0f, 2.0f);
    }
}
