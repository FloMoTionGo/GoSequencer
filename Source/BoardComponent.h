#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "PluginProcessor.h"

//==============================================================================
/** Flat and near monochrome. The accent is spent on state only - a switch that
    is on, the playhead - never on decoration. */
namespace theme
{
    //  two schemes, creamy paper or charcoal, never plain white or plain black
    inline bool isDark = false;

    inline juce::Colour background { 0xfffaf6ee };   //  the panel
    inline juce::Colour boardFill  { 0xfff1e9d8 };   //  the board, one step down from the panel
    inline juce::Colour ink        { 0xff2b2924 };   //  text, filled tracks
    inline juce::Colour dimText    { 0xff6b6455 };   //  captions, the status line
    inline juce::Colour faintText  { 0xffa79d89 };   //  closed tabs, coordinates, disabled
    inline juce::Colour hairline   { 0xffddd3bd };   //  dropdown rules, switch outlines, empty tracks
    inline juce::Colour gridLine   { 0xffc2b59c };
    inline juce::Colour accent     { 0xffc85a3c };
    inline juce::Colour error      { 0xffd6412f };   //  a refused move

    //  the faceplate: the display in the top bar, and the recessed well that
    //  frames a read-out (the game record's title, a knob's face)
    inline juce::Colour lcd        { 0xff2b2924 };
    inline juce::Colour lcdInk     { 0xfff1e9d8 };
    inline juce::Colour lcdDim     { 0xffa79d89 };
    inline juce::Colour well       { 0xfff3ede1 };

    //  a toggle switch that is off: its track and its thumb (on, both are the accent and white)
    inline juce::Colour track      { 0xffece4d3 };
    inline juce::Colour thumb      { 0xffa79d89 };

    //  the ring round a white stone, and round a black one - which the light
    //  scheme leaves without, since a black disc already stands out on cream
    inline juce::Colour stoneEdge  { 0xffa79d89 };
    inline juce::Colour blackEdge  { 0x00000000 };

    //  the stones keep their colour in both schemes: black is always the dark
    //  one, white the light one, whatever the panel behind them does. Black
    //  sits below the charcoal panel so it still reads as a filled disc there.
    inline const juce::Colour stoneBlack { 0xff171512 };
    inline const juce::Colour stoneWhite { 0xfffaf6ee };

    /** Flips every colour between the light (creamy white) and dark (near
        black) schemes. Values are copied wherever they are used, so anything
        already drawn or coloured has to be told again - see
        GoLookAndFeel::applyColours and GoSequencerEditor::setDarkMode. */
    inline void setDark (bool dark)
    {
        isDark = dark;

        if (dark)
        {
            background = juce::Colour (0xff151411);
            boardFill  = juce::Colour (0xff1e1c18);
            ink        = juce::Colour (0xffede6d4);
            dimText    = juce::Colour (0xffa49b86);
            faintText  = juce::Colour (0xff665f50);
            hairline   = juce::Colour (0xff34302a);
            gridLine   = juce::Colour (0xff4b4537);
            accent     = juce::Colour (0xffe37a52);
            error      = juce::Colour (0xffec6650);
            lcd        = juce::Colour (0xff0a0907);
            lcdInk     = juce::Colour (0xffede6d4);
            lcdDim     = juce::Colour (0xff6f6757);
            well       = juce::Colour (0xff1b1a16);
            track      = juce::Colour (0xff24221d);
            thumb      = juce::Colour (0xff7a7262);
            stoneEdge  = juce::Colour (0xff6f6757);
            blackEdge  = juce::Colour (0xff7a7262);
        }
        else
        {
            background = juce::Colour (0xfffaf6ee);
            boardFill  = juce::Colour (0xfff1e9d8);
            ink        = juce::Colour (0xff2b2924);
            dimText    = juce::Colour (0xff6b6455);
            faintText  = juce::Colour (0xffa79d89);
            hairline   = juce::Colour (0xffddd3bd);
            gridLine   = juce::Colour (0xffc2b59c);
            accent     = juce::Colour (0xffc85a3c);
            error      = juce::Colour (0xffd6412f);
            lcd        = juce::Colour (0xff2b2924);
            lcdInk     = juce::Colour (0xfff1e9d8);
            lcdDim     = juce::Colour (0xffa79d89);
            well       = juce::Colour (0xfff3ede1);
            track      = juce::Colour (0xffece4d3);
            thumb      = juce::Colour (0xffa79d89);
            stoneEdge  = juce::Colour (0xffa79d89);
            blackEdge  = juce::Colour (0x00000000);
        }
    }

    /** Sizes are CSS pixels: the em size, which JUCE calls the point height,
        not JUCE's own height (ascent plus descent, a third more for Segoe UI).
        So 12 here is what font-size: 12px is in the faceplate's web mock, and
        tracking is letter-spacing in ems. */
    inline juce::Font sized (juce::FontOptions options, float px, float tracking)
    {
        juce::Font f (options.withPointHeight (px));

        if (tracking != 0.0f)
            f = f.withExtraKerningFactor (tracking * px / juce::jmax (1.0f, f.getHeight()));

        return f;
    }

    /** Segoe UI on Windows; elsewhere the platform's own sans, which is already
        the right kind of plain. */
    inline juce::Font font (float px, int styleFlags = juce::Font::plain, float tracking = 0.0f)
    {
       #if JUCE_WINDOWS
        return sized (juce::FontOptions ("Segoe UI", px, styleFlags), px, tracking);
       #else
        return sized (juce::FontOptions (px, styleFlags), px, tracking);
       #endif
    }

    /** The display's figures: fixed width, so a count ticking over does not
        shuffle everything after it along. */
    inline juce::Font monoFont (float px)
    {
       #if JUCE_WINDOWS
        return sized (juce::FontOptions ("Consolas", px, juce::Font::plain), px, 0.0f);
       #else
        return sized (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), px, juce::Font::plain), px, 0.0f);
       #endif
    }
}

//==============================================================================
/** The goban: draws the board at whatever size is loaded and turns clicks into
    moves.

    It is drawn in two layers, because most of it never changes. The grid layer
    - surface, lines, star points, coordinates - is kept as an image and drawn
    again only when the board size, the colour scheme or the view's size changes.
    The stones layer on top is repainted cell by cell: every tick compares what
    each point shows (stone, spent, last move) and where the playheads are with
    what was last drawn, and asks for just the cells that differ. */
class BoardComponent final : public juce::Component,
                             private juce::Timer
{
public:
    explicit BoardComponent (GoSequencerProcessor& p);

    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Draws both layers again from scratch: for a new colour scheme or board
        size, which the cached grid cannot notice by itself. */
    void refreshAll();

    /** Called with a short explanation whenever the rules refuse a move. */
    std::function<void (const juce::String&)> onMessage;

    /** Draws one stone. The editor borrows it for the next-colour swatch. */
    static void drawStone (juce::Graphics& g, juce::Point<float> centre, float radius,
                           bool black, float alpha = 1.0f);

private:
    /** Surface, lines, star points and coordinates, kept as an image. */
    struct GridLayer final : public juce::Component
    {
        explicit GridLayer (BoardComponent& b) : owner (b) {}
        void paint (juce::Graphics& g) override;
        BoardComponent& owner;
    };

    /** Stones, the last move, the playheads and a refused move's flash. */
    struct StoneLayer final : public juce::Component
    {
        explicit StoneLayer (BoardComponent& b) : owner (b) {}
        void paint (juce::Graphics& g) override;
        BoardComponent& owner;
    };

    void timerCallback() override;

    int size() const noexcept { return processor.boardSize(); }

    juce::Rectangle<float> boardArea() const;
    float inset() const;
    float spacing() const;
    juce::Point<float> pointFor (int idx) const;
    int indexFor (juce::Point<float> p) const;

    /** The part of the stones layer a point's stone, playhead ring or flash can touch. */
    juce::Rectangle<int> cellBounds (int idx) const;
    void repaintCell (int idx);

    /** What a point shows, packed: stone colour, spent, last move. */
    std::uint8_t cellState (int idx) const noexcept;

    /** Where the playheads are now; returns how many. */
    int headCells (std::array<int, go::maxRings>& cells) const noexcept;

    void rememberEverything();

    void paintSurface (juce::Graphics&);
    void paintGrid (juce::Graphics&);
    void paintStones (juce::Graphics&);
    void paintPlayhead (juce::Graphics&);

    GoSequencerProcessor& processor;
    GridLayer grid { *this };
    StoneLayer stones { *this };

    //  what is on screen, so a tick can tell what changed
    std::array<std::uint8_t, go::maxCells> drawnCells {};
    std::array<int, go::maxRings> drawnHeads {};
    int drawnHeadCount = 0;
    bool drawnRunning = false;
    int drawnSize = 0;
    bool drawnDark = false;
    go::Stone drawnHoverColour = go::Stone::none;

    int hoverIndex = -1;
    int flashIndex = -1;
    float flashAlpha = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoardComponent)
};
