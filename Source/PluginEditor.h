#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "BoardComponent.h"
#include "PluginProcessor.h"

//==============================================================================
/** Hairline dropdowns, small knobs, toggle switches and buttons that are only an
    outline - the accent is spent on state alone: a switch that is on, a lit
    lamp, the playhead. */
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
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

private:
    /** A toggle switch: a track and a thumb, on = the accent with the thumb to
        the right. Inline it has its name beside it; as a cell - a knob's size -
        its name above and ON or OFF below. */
    void drawSwitch (juce::Graphics&, juce::Button&, bool cell, bool highlighted);
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
/** A knob, or a channel cell, whose value can be typed: click the value under
    it, type, press Enter.

    `parse` turns the text into a value in the parameter's own units, or into
    nothing when it is not a value - and then the knob keeps what it had rather
    than jumping to its minimum, which is what a slider does by default. Text
    outside the range is clamped, and `onNote` says so. */
class Knob final : public juce::Slider
{
public:
    std::function<std::optional<double> (const juce::String&)> parse;
    std::function<void (const juce::String&)> onNote;

    double getValueFromText (const juce::String& text) override;
};

//==============================================================================
/** A row of choices, all visible, one click each - for a parameter with only a
    few of them, where a dropdown would hide the rest. Each segment carries the
    value it stands for, so the order on screen need not be the parameter's
    (the board sizes read 8 9 13 19, while the parameter keeps its append-only
    order). */
class SegmentedChoice final : public juce::Component
{
public:
    struct Item
    {
        juce::String text;
        int value = 0;
    };

    void setItems (std::vector<Item>);
    void setSelectedValue (int value);
    int getSelectedValue() const noexcept { return selected; }

    /** Stacks the segments top to bottom instead of side by side. */
    void setVertical (bool shouldBeVertical) { vertical = shouldBeVertical; repaint(); }

    /** The size and letter-spacing of the words, in CSS px and ems. */
    void setTextStyle (float px, float tracking) { textSize = px; textTracking = tracking; repaint(); }

    /** Called with a segment's value when it is clicked. */
    std::function<void (int value)> onSelect;

    /** Draws a small picture above the text, in the colour already set. */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int value)> drawIcon;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    int itemAt (juce::Point<int>) const;
    juce::Rectangle<float> segment (int i) const;

    std::vector<Item> items;
    int selected = -1;
    int hover = -1;
    bool vertical = false;
    float textSize = 12.0f, textTracking = 0.0f;
};

//==============================================================================
/** Over each voice's channel cell - black, white and the nine heads - its name
    and a lamp that lights when that voice plays a note. Read from the
    processor's lock-free surface on its own timer: a head that has moved onto a
    live stone has just played it. A note shorter than a tick can be missed; for
    a lamp that is fine. */
class ActivityLamps final : public juce::Component,
                            private juce::Timer
{
public:
    static constexpr int voices = 2 + GoSequencerProcessor::maxHeadChannels;
    static constexpr int gap = 3, headHeight = 34;

    explicit ActivityLamps (GoSequencerProcessor&);

    /** The column a voice has, the cell under its lamp included. */
    juce::Rectangle<int> column (int voice) const;

    /** Spiral routes by colour, the multi head modes by playhead. */
    bool inUse (int voice) const noexcept;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    GoSequencerProcessor& processor;
    std::array<float, (size_t) voices> level {};
    std::array<int, (size_t) voices> lastPosition {};
    int shownHeads = -1;
};

//==============================================================================
/** Which voices go out on which channel, mode by mode, with the mode that is
    playing marked - the PATCH face's summary of the channel cells on PLAY. */
class RoutingTable final : public juce::Component
{
public:
    explicit RoutingTable (GoSequencerProcessor& p) : processor (p) { setInterceptsMouseClicks (false, false); }

    /** Re-reads the channels and the mode, and repaints if anything reads differently. */
    void refresh();

    void paint (juce::Graphics&) override;

private:
    struct Row
    {
        juce::String mode, text;
        bool on = false;
    };

    GoSequencerProcessor& processor;
    std::array<Row, 3> rows;
};

//==============================================================================
/** A drawing of a Launchpad X with what each button round its edge does here.
    Novation printed its own names on those buttons, and none of them is what
    the button does in this plugin, so the Launchpad section draws the device
    and writes the real job by each one: the top row's above it, slanted, one
    starting over each key. The right half of the pads lists what holding a
    top key does, a row each after a small copy of the key, and points at
    Shift on its own row; the side buttons have no other job for now. The left
    half is left empty for the two port dropdowns, which the editor lays over it.

    The jobs come from lpx::edgeButtons, one layer at a time: the second while
    Shift is held on the device, or while the mouse is on the drawn Shift key -
    the one place the drawing takes the mouse. Clicks anywhere else go straight
    through. The Shift layer has no holds, so the list is empty then. */
class LaunchpadDiagram final : public juce::Component
{
public:
    //  labelsHeight: room for a slanted job as long as "hold: random"
    static constexpr int pitch = 26, padding = 6, labelsHeight = 68, labelsGap = 4;
    static constexpr int sketchSide = 9 * pitch + 2 * padding + 2;
    static constexpr int width = sketchSide, height = labelsHeight + labelsGap + sketchSide;

    /** The left half of the pads, in this component's coordinates. */
    static juce::Rectangle<int> leftHalf();

    /** Whether Shift is held on the device; repaints only on a change. */
    void setDeviceShift (bool held);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    /** The bottom side button - Shift - in this component's coordinates. */
    static juce::Rectangle<int> shiftKey();

    bool showingShift() const noexcept { return deviceShift || hovering; }

    bool deviceShift = false, hovering = false;
};

//==============================================================================
/** The faceplate every control sits on. It is laid out once, at 1200 x 720, and
    the window scales it as a whole - so a resize never re-flows anything, and
    every knob keeps its place. */
class Faceplate final : public juce::Component
{
public:
    static constexpr int width = 1200, height = 720;

    std::function<void (juce::Graphics&)> painter;

    void paint (juce::Graphics& g) override { if (painter != nullptr) painter (g); }
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
    using ButtonAttachment   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    /** The two faces of the panel. PLAY holds what is played with - the
        playheads, the voice and its channels, the record, the players, stone
        life; PATCH what is set up once - the MIDI out port, the Launchpad.
        `pinned` is not a face: the top bar, the board and the two strips under
        it stay on both. */
    //  The open face is saved as its number (panelFace), so a new one goes on the end.
    enum Face { pinned = -1, playFace, patchFace, faceCount };

    void timerCallback() override;

    /** Adds a control to the faceplate as a member of one face (hidden until
        that face is shown), or visible for good when it is pinned. */
    void addToFace (Face, juce::Component&);
    void showFace (int face);

    void setUpCaption (Face, juce::Label&, const juce::String& text);
    void setUpText (Face, juce::Label&, juce::Justification);
    void setUpKnob (Face, Knob&, juce::Label& caption, const juce::String& text, const juce::String& parameterID,
                    std::unique_ptr<SliderAttachment>&, std::function<std::optional<double> (const juce::String&)> parse);
    void setUpCell (Knob&, const juce::String& title, const juce::String& parameterID,
                    std::unique_ptr<SliderAttachment>&);
    void setUpSegments (Face, SegmentedChoice&, std::vector<SegmentedChoice::Item>, const juce::String& parameterID,
                        std::unique_ptr<juce::ParameterAttachment>&);

    /** A toggle switch bound to a parameter: a knob-sized cell (name, switch,
        ON / OFF), or a small one with its name beside it. */
    void setUpSwitch (Face, juce::TextButton&, const juce::String& text, bool cell,
                      const juce::String& parameterID, std::unique_ptr<ButtonAttachment>&);
    void setUpButton (Face, juce::TextButton&, const juce::String& caption, std::function<void()> onClick,
                      bool small = false);

    /** Greyed out and left alone, with its caption, while the mode or a switch
        gives it nothing to do. */
    static void muteKnob (Knob&, juce::Label& caption, bool muted);

    void openSgfChooser();
    void loadSgfFile (const juce::File&);
    void showMessage (const juce::String&);
    void refreshGameDisplay();

    /** Greys out what the mode is not using and rewrites the lines that explain
        it - re-read whenever the mode or the board size changes. */
    void refreshModeDisplay();

    /** The two lines under the channel cells: what routes by what, and where
        the notes go out. */
    void refreshOutputLines();

    /** The light/dark switch, top right. */
    void setDarkMode (bool dark);

    /** Everything drawn on the plate itself: the wordmark, the display, the
        section frames with their names and jobs, the column heads, the record's
        well, the screws. */
    void paintPlate (juce::Graphics&);
    void paintWordmark (juce::Graphics&, juce::Rectangle<float>);
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>);

    GoSequencerProcessor& processor;
    GoLookAndFeel lookAndFeel;
    Faceplate plate;
    BoardComponent board;

    std::array<std::vector<juce::Component*>, (size_t) faceCount> faceMembers;
    int currentFace = playFace;

    /** A frame drawn round a group of controls, with its name cut into the
        top edge on the left and what it is for on the right. */
    struct Section
    {
        juce::Rectangle<int> bounds;
        juce::String title, job;
        int face;
    };

    /** A small heading over a column of controls, ruled underneath. */
    struct ColumnHead
    {
        juce::Rectangle<int> bounds;
        juce::String text;
        int face;
    };

    std::vector<Section> sections;
    std::vector<ColumnHead> columnHeads;
    juce::Rectangle<int> displayBounds, topBarBounds, wordmarkBounds, gameWellBounds;

    //  always visible, whichever face is open - not face members
    SegmentedChoice faceSwitch;
    juce::TextButton darkModeButton;

    //  captions and text labels coloured theme::dimText at setup time; the
    //  colour is a copy, so a scheme change has to walk this list and re-set it
    std::vector<juce::Label*> dimLabels;

    //  ---- knobs, and the captions over them
    Knob rateKnob, tempoKnob, noteKnob, gateKnob, lifeKnob, spreadKnob,
         blackVelocityKnob, whiteVelocityKnob, gameRateKnob, waveGapKnob,
         aiMovesKnob, aiVariationKnob, aiSeedKnob;
    juce::Label rateCaption, tempoCaption, noteCaption, gateCaption, lifeCaption, spreadCaption,
                blackVelocityCaption, whiteVelocityCaption, gameRateCaption, waveGapCaption,
                aiMovesCaption, aiVariationCaption, aiSeedCaption;
    std::unique_ptr<SliderAttachment> rateAttachment, tempoAttachment, noteAttachment, gateAttachment,
                                      lifeAttachment, spreadAttachment, blackVelocityAttachment,
                                      whiteVelocityAttachment, gameRateAttachment, waveGapAttachment,
                                      aiMovesAttachment, aiVariationAttachment, aiSeedAttachment;

    //  ---- the output bay: one cell per voice - black, white, the nine heads -
    //  each its MIDI channel, set outright rather than offset from a base, under
    //  the lamp that lights as that voice plays
    static constexpr int headChannels = GoSequencerProcessor::maxHeadChannels;
    ActivityLamps activity;
    Knob blackChannelCell, whiteChannelCell;
    std::array<Knob, (size_t) headChannels> headChannelCells;
    std::unique_ptr<SliderAttachment> blackChannelAttachment, whiteChannelAttachment;
    std::array<std::unique_ptr<SliderAttachment>, (size_t) headChannels> headChannelAttachments;

    /** The cell for a voice, in the order the lamps have them. */
    Knob& channelCell (int voice);

    //  ---- segment switches
    SegmentedChoice modeSwitch, sizeSwitch, placeSwitch, lifeModeSwitch, playersSwitch, opponentSwitch;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment, sizeAttachment, placeAttachment,
                                               lifeModeAttachment, playersAttachment, opponentAttachment;

    //  ---- toggle switches and buttons
    juce::TextButton freeRunButton, tieNotesButton, koButton, selfCaptureButton,
                     runGameButton, loopGameButton, waveReplayButton, aiPlayButton, aiOpponentButton;
    std::unique_ptr<ButtonAttachment> freeRunAttachment, tieNotesAttachment, koAttachment, selfCaptureAttachment,
                                      runGameAttachment, loopGameAttachment, waveReplayAttachment,
                                      aiPlayAttachment, aiOpponentAttachment;

    juce::TextButton clearButton, randomButton, loadButton, unloadButton, previousMoveButton, nextMoveButton,
                     openingFromBoardButton, openingBookButton, passButton, newMatchButton;

    juce::Slider moveSlider;

    juce::Label sizeCaption, placeCaption, rulesCaption, lifeModeCaption, playersCaption,
                openingCaption, opponentCaption, moveCaption;
    juce::Label hintLabel, gameTitleLabel, gameDetailLabel, lapLabel, outputLabel, outputRouteLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastSgfDirectory;

    juce::String message;
    int messageCountdown = 0;
    int lastMoveShown = -1;
    juce::String lastHeaderShown;
    int lastModeKeyShown = -1;
    bool lastWaveReplayShown = false;
    bool dragHighlight = false;

    //  a run swaps its own record in when a game ends, so the title line has to
    //  be re-read rather than only refreshed when something was clicked
    bool lastAiShown = false;
    int lastAiGameShown = -1;

    //  and a record can go with nothing clicked here either - unloaded from the
    //  Launchpad, or thrown out by a random position - so its length is watched
    int lastGameMovesShown = -1;

    /** The opening's two buttons: Use book only once there is an opening of
        your own to go back from. */
    void refreshOpeningDisplay();

    bool lastCustomOpeningShown = false;

    /** The enables that go with whose move it is. The answer lands with
        nothing clicked, so this is re-read from the tick: lastTurnShown is -1
        for no game, 0 yours, 1 theirs, 2 finished. Whose move it is shows in
        the display, and the end of a game as a message. */
    void refreshMatchDisplay();

    int lastTurnShown = -2;

    /** The MIDI out port dropdown, filled from the ports there are right now,
        and the line under it saying whether the chosen one is open - re-read
        when it opens or closes, since loopMIDI can start or quit at any time. */
    void refreshPortList();
    void refreshPortStatus();

    RefreshingComboBox portBox;
    RoutingTable routingTable;
    juce::Label portCaption, portStatusLabel, portNoteLabel, routingNoteLabel, playersNoteLabel;
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

    LaunchpadDiagram padsDiagram;
    RefreshingComboBox padsInBox, padsOutBox;
    juce::Label padsInCaption, padsOutCaption, padsStatusLabel, padsNoteLabel;
    juce::TextButton padsFindButton, padsSizeButton, padsStopButton;
    juce::StringArray padsInItems, padsOutItems;     //  as portItems: id i + 2, and id 1 is Off
    juce::String lastPadsStatusShown;
    bool lastPadsOpenShown = false;
    int lastPadsSizeShown = 0;

    //  the board view repaints on a step or a move, and a new size is neither
    int lastBoardSizeShown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoSequencerEditor)
};
