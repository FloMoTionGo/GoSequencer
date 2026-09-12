#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "BoardComponent.h"
#include "PluginProcessor.h"

//==============================================================================
class GoLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    GoLookAndFeel();
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

    void timerCallback() override;

    void setUpSlider (juce::Slider&, juce::Label&, const juce::String& caption,
                      const juce::String& parameterID, std::unique_ptr<SliderAttachment>&);
    void setUpCombo (juce::ComboBox&, juce::Label&, const juce::String& caption,
                     const juce::StringArray& items, const juce::String& parameterID,
                     std::unique_ptr<ComboBoxAttachment>&);
    void setUpToggle (juce::TextButton&, const juce::String& caption,
                      const juce::String& parameterID, std::unique_ptr<ButtonAttachment>&);
    void setUpSection (juce::Label&, const juce::String& text);

    /** The fold out's header: a section label that happens to be clickable. */
    void setUpDisclosure (juce::TextButton&, const juce::String& text);

    /** Caret, visibility and window height, after the fold out is toggled. */
    void refreshChannelSection (bool resizeWindow);

    bool channelsOpen() const;

    void openSgfChooser();
    void loadSgfFile (const juce::File&);
    void showMessage (const juce::String&);
    void refreshGameDisplay();

    static void placeLabelled (juce::Rectangle<int> cell, juce::Label&, juce::Component&);

    GoSequencerProcessor& processor;
    GoLookAndFeel lookAndFeel;
    BoardComponent board;

    juce::ComboBox rateBox, colourBox, sizeBox, gameRateBox, modeBox, lifeModeBox;
    juce::Slider noteSlider, gateSlider, tempoSlider,
                 blackVelocitySlider, whiteVelocitySlider,
                 spreadSlider, lifeSlider,
                 blackChannelSlider, whiteChannelSlider,
                 moveSlider;

    //  one slider per playhead: assigned, never offset from a base
    static constexpr int headChannels = GoSequencerProcessor::maxHeadChannels;
    std::array<juce::Slider, (size_t) headChannels> headChannelSliders;
    std::array<juce::Label,  (size_t) headChannels> headChannelCaptions;
    juce::TextButton freeRunButton, koButton, selfCaptureButton, pathButton, clearButton,
                     loadButton, runGameButton, loopGameButton, unloadButton,
                     previousMoveButton, nextMoveButton, channelsToggle;

    juce::Label sequencerSection, gameSection;
    juce::Label rateCaption, noteCaption, gateCaption, tempoCaption,
                blackVelocityCaption, whiteVelocityCaption,
                blackChannelCaption, whiteChannelCaption,
                modeCaption, spreadCaption, lifeCaption, lifeModeCaption,
                colourCaption, sizeCaption, gameRateCaption, moveCaption,
                blankCaption1, blankCaption2, blankCaption3, blankCaption4,
                blankCaption5, blankCaption6, blankCaption7, blankCaption8, blankCaption9;
    juce::Label hintLabel, gameTitleLabel, gameDetailLabel;

    std::unique_ptr<SliderAttachment>   noteAttachment, gateAttachment, tempoAttachment,
                                        blackVelocityAttachment, whiteVelocityAttachment,
                                        spreadAttachment, lifeAttachment,
                                        blackChannelAttachment, whiteChannelAttachment;
    std::array<std::unique_ptr<SliderAttachment>, (size_t) headChannels> headChannelAttachments;
    std::unique_ptr<ComboBoxAttachment> rateAttachment, colourAttachment, sizeAttachment, gameRateAttachment,
                                        modeAttachment, lifeModeAttachment;
    std::unique_ptr<ButtonAttachment>   freeRunAttachment, koAttachment, selfCaptureAttachment,
                                        runGameAttachment, loopGameAttachment;

    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::File lastSgfDirectory;

    juce::Rectangle<int> headerBounds;
    juce::String message;
    int messageCountdown = 0;
    int lastMoveShown = -1;
    int lastModeShown = -1;
    bool dragHighlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoSequencerEditor)
};
