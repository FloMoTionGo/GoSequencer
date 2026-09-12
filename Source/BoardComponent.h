#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "PluginProcessor.h"

//==============================================================================
namespace theme
{
    inline const juce::Colour background  { 0xff16151a };
    inline const juce::Colour panel       { 0xff222129 };
    inline const juce::Colour panelBright { 0xff2c2b35 };
    inline const juce::Colour text        { 0xffe8e4dc };
    inline const juce::Colour dimText     { 0xff8d8a96 };
    inline const juce::Colour accent      { 0xffc85a3c };
    inline const juce::Colour woodLight   { 0xffeaca90 };
    inline const juce::Colour woodDark    { 0xffcf9d58 };
    inline const juce::Colour lines       { 0xff3b2a19 };
}

//==============================================================================
/** The goban: draws the board at whatever size is loaded and turns clicks into
    moves. */
class BoardComponent final : public juce::Component,
                             private juce::Timer
{
public:
    explicit BoardComponent (GoSequencerProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void setShowPath (bool shouldShow);
    bool getShowPath() const noexcept { return showPath; }

    /** Called with a short explanation whenever the rules refuse a move. */
    std::function<void (const juce::String&)> onMessage;

    /** Draws one stone. The editor borrows it for the next-colour swatch. */
    static void drawStone (juce::Graphics& g, juce::Point<float> centre, float radius,
                           bool black, float alpha = 1.0f);

private:
    void timerCallback() override;

    int size() const noexcept { return processor.boardSize(); }

    juce::Rectangle<float> woodArea() const;
    float inset() const;
    float spacing() const;
    juce::Point<float> pointFor (int idx) const;
    int indexFor (juce::Point<float> p) const;

    void paintWood (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    void paintPath (juce::Graphics&);
    void paintStones (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);

    GoSequencerProcessor& processor;

    bool showPath = true;
    int lastDrawnStep = -1;
    int lastDrawnMove = -1;
    int hoverIndex = -1;
    int flashIndex = -1;
    float flashAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardComponent)
};
