#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "PluginProcessor.h"

//==============================================================================
/** Flat and near monochrome. The accent is spent on state only - a switch that
    is on, the playhead, the path it walks - never on decoration. */
namespace theme
{
    //  two schemes, creamy paper or charcoal, never plain white or plain black
    inline bool isDark = false;

    inline juce::Colour background { 0xfffaf6ee };   //  the panel
    inline juce::Colour boardFill  { 0xfff1e9d8 };   //  the board, one step down from the panel
    inline juce::Colour ink        { 0xff2b2924 };   //  text, filled tracks
    inline juce::Colour dimText    { 0xff726b5c };   //  captions, the status line
    inline juce::Colour faintText  { 0xffa79d89 };   //  closed tabs, coordinates, disabled
    inline juce::Colour hairline   { 0xffddd3bd };   //  dropdown rules, switch outlines, empty tracks
    inline juce::Colour gridLine   { 0xffc2b59c };
    inline juce::Colour accent     { 0xffc85a3c };
    inline juce::Colour error      { 0xffd6412f };   //  a refused move

    //  the stones keep their colour in both schemes: black is always the dark
    //  one, white the light one, whatever the panel behind them does. Black
    //  sits below the charcoal panel so it still reads as a filled disc there.
    inline const juce::Colour stoneBlack { 0xff171512 };
    inline const juce::Colour stoneWhite { 0xfffaf6ee };

    /** Flips every colour between the light (creamy white) and dark (charcoal)
        schemes. Values are copied wherever they are used, so anything already
        drawn or coloured has to be told again - see GoLookAndFeel::applyColours
        and GoSequencerEditor::setDarkMode. */
    inline void setDark (bool dark)
    {
        isDark = dark;

        if (dark)
        {
            background = juce::Colour (0xff2a2822);
            boardFill  = juce::Colour (0xff34302a);
            ink        = juce::Colour (0xfff2ecdd);
            dimText    = juce::Colour (0xffada387);
            faintText  = juce::Colour (0xff756e5c);
            hairline   = juce::Colour (0xff4a4438);
            gridLine   = juce::Colour (0xff5c5544);
            accent     = juce::Colour (0xffe0754f);
            error      = juce::Colour (0xffe8604a);
        }
        else
        {
            background = juce::Colour (0xfffaf6ee);
            boardFill  = juce::Colour (0xfff1e9d8);
            ink        = juce::Colour (0xff2b2924);
            dimText    = juce::Colour (0xff726b5c);
            faintText  = juce::Colour (0xffa79d89);
            hairline   = juce::Colour (0xffddd3bd);
            gridLine   = juce::Colour (0xffc2b59c);
            accent     = juce::Colour (0xffc85a3c);
            error      = juce::Colour (0xffd6412f);
        }
    }

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
