#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "BoardComponent.h"
#include "PluginProcessor.h"

//==============================================================================
/** Hairline dropdowns, thin tracks, and switches that are only an outline until
    they are on - then they fill with the accent, the one place anything does. */
class GoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    GoLookAndFeel();

    /** Re-reads every theme:: colour. Called once at construction and again
        whenever the editor flips between the light and dark schemes. */
    void applyColours();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
/** A dropdown that fills itself again each time it opens, so a port created
    while the editor was already showing is in the list. */
class RefreshingComboBox final : public juce::ComboBox
{
public:
    std::function<void()> onOpen;

    void showPopup() override
    {
        if (onOpen != nullptr)
            onOpen();

        ComboBox::showPopup();
    }
};

//==============================================================================
/** A drawing of a Launchpad X with what each button round its edge does here.
    Novation printed its own names on those buttons, and none of them is what
    the button does in this plugin, so the Pads tab draws the device and writes
    the real job beside each one. It only draws: clicks go straight through. */
class LaunchpadDiagram final : public juce::Component
{
public:
    LaunchpadDiagram() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics&) override;
};

//==============================================================================
class GoSequencerEditor final : public juce::AudioProcessorEditor,
                                public juce::FileDragAndDropTarget,
                                private juce::Timer
{
public:
    explicit GoSequencerEditor (GoSequencerProcessor&);
    ~GoSequencerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using SliderAttachment   = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** The tabs the controls are split across. `pinned` is not a tab: it marks
        the controls that stay beside the board whichever tab is open. */
    //  The open tab is saved as its number, so a new tab goes on the end.
    enum Tab { pinned = -1, sequencerTab, boardTab, channelsTab, gameTab, aiTab, padsTab, tabCount };

    void timerCallback() override;

    /** Adds a control to the editor as a member of one tab (hidden until that
        tab is shown), or visible for good when it is pinned. */
    void addToTab (Tab, juce::Component&);
    void showTab (int tab);

    void setUpCaption (Tab, juce::Label&, const juce::String& text);
    void setUpText (Tab, juce::Label&, juce::Justification);
    void setUpSlider (Tab, juce::Slider&, juce::Label&, const juce::String& caption,
                      const juce::String& parameterID, std::unique_ptr<SliderAttachment>&,
                      int valueWidth = 72);
    void setUpCombo (Tab, juce::ComboBox&, juce::Label&, const juce::String& caption,
                     const juce::StringArray& items, const juce::String& parameterID,
                     std::unique_ptr<ComboBoxAttachment>&);
    void setUpToggle (Tab, juce::TextButton&, const juce::String& caption,
                      const juce::String& parameterID, std::unique_ptr<ButtonAttachment>&);
    void setUpButton (Tab, juce::TextButton&, const juce::String& caption, std::function<void()> onClick);

    void openSgfChooser();
    void loadSgfFile (const juce::File&);
    void showMessage (const juce::String&);
    void refreshGameDisplay();

    /** The header's status text: step, mode, captures, record position, transport. */
    juce::String statusLine() const;

    /** The always-visible light/dark switch, top right of the header. */
    void setDarkMode (bool dark);

    GoSequencerProcessor& processor;
    GoLookAndFeel lookAndFeel;
    BoardComponent board;

    std::array<juce::TextButton, (size_t) tabCount> tabButtons;
    std::array<std::vector<juce::Component*>, (size_t) tabCount> tabMembers;
    int currentTab = sequencerTab;

    //  always visible, whichever tab is open - not itself a tab member
    juce::TextButton darkModeButton;

    //  captions and text labels coloured theme::dimText at setup time; the
    //  colour is a copy, so a scheme change has to walk this list and re-set it
    std::vector<juce::Label*> dimLabels;

    juce::ComboBox rateBox, colourBox, sizeBox, gameRateBox, modeBox, lifeModeBox, aiPlayersBox,
                   aiOpponentBox;
    juce::Slider noteSlider, gateSlider, tempoSlider,
                 blackVelocitySlider, whiteVelocitySlider,
                 spreadSlider, lifeSlider,
                 blackChannelSlider, whiteChannelSlider,
                 moveSlider, waveGapSlider,
                 aiMovesSlider, aiVariationSlider, aiSeedSlider;

    //  one slider per playhead: assigned, never offset from a base
    static constexpr int headChannels = GoSequencerProcessor::maxHeadChannels;
    std::array<juce::Slider, (size_t) headChannels> headChannelSliders;
    std::array<juce::Label,  (size_t) headChannels> headChannelCaptions;
    juce::TextButton freeRunButton, koButton, selfCaptureButton, clearButton,
                     loadButton, runGameButton, loopGameButton, unloadButton,
                     previousMoveButton, nextMoveButton, waveReplayButton,
                     aiPlayButton, openingFromBoardButton, openingBookButton,
                     aiOpponentButton, passButton, newMatchButton;

    juce::Label rateCaption, noteCaption, gateCaption, tempoCaption,
                blackVelocityCaption, whiteVelocityCaption,
                blackChannelCaption, whiteChannelCaption,
                modeCaption, spreadCaption, lifeCaption, lifeModeCaption,
                colourCaption, sizeCaption, gameRateCaption, moveCaption, waveGapCaption,
                aiPlayersCaption, aiMovesCaption, aiVariationCaption, aiSeedCaption, openingCaption,
                aiOpponentCaption;
    juce::Label hintLabel, gameTitleLabel, gameDetailLabel, openingLabel, matchLabel;

    std::unique_ptr<SliderAttachment>   noteAttachment, gateAttachment, tempoAttachment,
                                        blackVelocityAttachment, whiteVelocityAttachment,
                                        spreadAttachment, lifeAttachment,
                                        blackChannelAttachment, whiteChannelAttachment,
                                        waveGapAttachment,
                                        aiMovesAttachment, aiVariationAttachment, aiSeedAttachment;
    std::array<std::unique_ptr<SliderAttachment>, (size_t) headChannels> headChannelAttachments;
    std::unique_ptr<ComboBoxAttachment> rateAttachment, colourAttachment, sizeAttachment, gameRateAttachment,
                                        modeAttachment, lifeModeAttachment, aiPlayersAttachment,
                                        aiOpponentColourAttachment;
    std::unique_ptr<ButtonAttachment>   freeRunAttachment, koAttachment, selfCaptureAttachment,
                                        runGameAttachment, loopGameAttachment, waveReplayAttachment,
                                        aiPlayAttachment, aiOpponentAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastSgfDirectory;

    juce::Rectangle<int> headerBounds;
    juce::String message;
    int messageCountdown = 0;
    int lastMoveShown = -1;
    juce::String lastHeaderShown;
    int lastModeShown = -1;
    bool lastWaveReplayShown = false;
    bool dragHighlight = false;

    //  a run swaps its own record in when a game ends, so the title line has to
    //  be re-read rather than only refreshed when something was clicked
    bool lastAiShown = false;
    int lastMoveTotalShown = -1;
    bool lastThinkingShown = false;
    int lastAiGameShown = -1;

    /** The opening line, and the enables that go with it - re-read when the
        count of hand-played stones or the opening itself changes. */
    void refreshOpeningDisplay();

    juce::String lastOpeningShown;

    /** The line saying whose move it is, and the enables that go with it. Their
        answer lands with nothing clicked, so this is re-read from the tick:
        lastTurnShown is -1 for no game, 0 yours, 1 theirs, 2 finished. */
    void refreshMatchDisplay();

    int lastTurnShown = -2;

    /** The MIDI out port dropdown, filled from the ports there are right now,
        and the line under it saying whether the chosen one is open - re-read
        when it opens or closes, since loopMIDI can start or quit at any time. */
    void refreshPortList();
    void refreshPortStatus();

    RefreshingComboBox portBox;
    juce::Label portCaption, portStatusLabel;
    juce::StringArray portItems;        //  item id i + 2 is portItems[i]; id 1 is Off
    bool lastPortOpenShown = false;

    /** The Launchpad's two port dropdowns and the line saying what it is doing -
        re-read whenever that changes, since a Launchpad can be plugged in, pulled
        out, or taken by another program with nothing clicked here. */
    void refreshPadsLists();
    void refreshPadsStatus();

    /** Drives the Launchpad on these ports, and asks for the board it can show.
        The size only changes by itself when there is nothing on the board to
        lose; otherwise Use 8 x 8 does it, once the status line has said why. */
    void choosePadsPorts (const juce::String& in, const juce::String& out);
    void useLaunchpadBoardSize();
    bool boardHasSomethingToLose() const;

    RefreshingComboBox padsInBox, padsOutBox;
    juce::Label padsInCaption, padsOutCaption, padsStatusLabel;
    LaunchpadDiagram padsDiagram;
    juce::TextButton padsFindButton, padsSizeButton, padsStopButton;
    juce::StringArray padsInItems, padsOutItems;     //  as portItems: id i + 2, and id 1 is Off
    juce::String lastPadsStatusShown;
    bool lastPadsOpenShown = false;
    int lastPadsSizeShown = 0;

    //  the board view repaints on a step or a move, and a new size is neither
    int lastBoardSizeShown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoSequencerEditor)
};
