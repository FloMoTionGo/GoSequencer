#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "PluginProcessor.h"

//==============================================================================
/** Flat and near monochrome. The accent is spent on state only - a switch that
    is on, the playhead, the path it walks - never on decoration. */
namespace theme
{
    inline const juce::Colour background { 0xfffbfbfa };   //  the panel
    inline const juce::Colour boardFill  { 0xfff2f2f0 };   //  the board, one step down from the panel
    inline const juce::Colour ink        { 0xff1c1c1e };   //  text, black stones, filled tracks
    inline const juce::Colour dimText    { 0xff7a7a7d };   //  captions, the status line
    inline const juce::Colour faintText  { 0xffa5a5a8 };   //  closed tabs, coordinates, disabled
    inline const juce::Colour hairline   { 0xffd8d8da };   //  dropdown rules, switch outlines, empty tracks
    inline const juce::Colour gridLine   { 0xffbdbdbb };
    inline const juce::Colour accent     { 0xffc85a3c };
    inline const juce::Colour error      { 0xffd6412f };   //  a refused move

    /** Segoe UI on Windows; elsewhere the platform's own sans, which is already
        the right kind of plain. Tracking is a proportion of the height. */
    inline juce::Font font (float height, int styleFlags = juce::Font::plain, float tracking = 0.0f)
    {
       #if JUCE_WINDOWS
        return juce::Font (juce::FontOptions ("Segoe UI", height, styleFlags).withKerningFactor (tracking));
       #else
        return juce::Font (juce::FontOptions (height, styleFlags).withKerningFactor (tracking));
       #endif
    }
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

    juce::Rectangle<float> boardArea() const;
    float inset() const;
    float spacing() const;
    juce::Point<float> pointFor (int idx) const;
    int indexFor (juce::Point<float> p) const;

    void paintSurface (juce::Graphics&);
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
