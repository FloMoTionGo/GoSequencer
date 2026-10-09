#pragma once

//  Where a Launchpad X's lights sit, and which point of the board each pad
//  shows. Deliberately free of JUCE, like GoBoard.h, so the mapping can be
//  checked without loading a host (see tests/GoRulesTests.cpp). The device
//  itself lives in LaunchpadSurface.h, which is the only thing here that talks
//  MIDI.
//
//  The device numbers its lights in a grid of ten: the last digit is the column
//  counting from the left, the tens digit the row counting from the BOTTOM. Row
//  0 is the top one everywhere else in this plugin (go::rowOf), so the two
//  conventions meet in padIndex() and nowhere else.
//
//      91 92 93 94 95 96 97 98       the top row, and it speaks CC, not notes
//
//      81 82 83 84 85 86 87 88  89
//      71 72 73 74 75 76 77 78  79   the grid is 11..88, the right hand
//      ..                       ..   column 89 down to 19, the logo 99
//      11 12 13 14 15 16 17 18  19

#include "GoBoard.h"

#include <array>
#include <cstdint>

namespace lpx
{
    inline constexpr int side  = 8;
    inline constexpr int cells = side * side;

    /** The board this controller is the shape of. An 8x8 fills the grid exactly,
        which is the whole reason go::supportedSizes has an 8 in it. */
    inline constexpr int boardSize = side;

    //==============================================================================
    //  The grid. These arrive as note on, channel 1.

    inline constexpr int padIndex (int col, int row) noexcept { return (side - row) * 10 + col + 1; }

    inline constexpr bool isPad (int index) noexcept
    {
        return index >= 11 && index <= 88 && index % 10 >= 1 && index % 10 <= side;
    }

    inline constexpr int padCol (int index) noexcept { return index % 10 - 1; }
    inline constexpr int padRow (int index) noexcept { return side - index / 10; }

    //==============================================================================
    //  The perimeter. These arrive as control change.

    /** The right hand column, top to bottom: 89, 79 ... 19. */
    inline constexpr int  sceneIndex (int row)   noexcept { return (side - row) * 10 + 9; }
    inline constexpr bool isScene    (int index) noexcept { return index >= 19 && index <= 89 && index % 10 == 9; }
    inline constexpr int  sceneRow   (int index) noexcept { return side - index / 10; }

    /** The top row, left to right: 91 ... 98. The first four are the arrows. */
    inline constexpr int  topIndex (int col)   noexcept { return 91 + col; }
    inline constexpr bool isTop    (int index) noexcept { return index >= 91 && index <= 98; }
    inline constexpr int  topCol   (int index) noexcept { return index - 91; }

    inline constexpr int logoIndex = 99;

    /** Every index a light can be sent to is below this, so one frame of the
        whole surface is a single flat array. */
    inline constexpr int indexCount = 100;

    //==============================================================================
    //  The board underneath.

    /** The point a pad stands for, or -1 if that pad shows nothing.

        The grid is only a board when the board is the grid's size. On a 9x9 or
        anything larger there is no honest mapping: 64 pads cannot reach 81
        points without either hiding some of them or making one pad mean several,
        and a surface whose whole display is the board cannot afford to do
        either. Those sizes leave the grid dark instead of lying about it. */
    inline constexpr int boardIndexFor (int padIdx, int size) noexcept
    {
        return (size == boardSize && isPad (padIdx))
                 ? go::index (padCol (padIdx), padRow (padIdx), size)
                 : -1;
    }

    /** The pad showing a point, or -1 if none does. The inverse of the above. */
    inline constexpr int padIndexFor (int boardIdx, int size) noexcept
    {
        return (size == boardSize && boardIdx >= 0 && boardIdx < size * size)
                 ? padIndex (go::colOf (boardIdx, size), go::rowOf (boardIdx, size))
                 : -1;
    }

    /** Whether the grid can show this board at all. */
    inline constexpr bool canShow (int size) noexcept { return size == boardSize; }

    //==============================================================================
    //  What the sixteen buttons round the edge do. One table, read by the device
    //  (LaunchpadSurface: what a press does and how each button is lit) and by
    //  the editor's drawing of it (LaunchpadDiagram), so the three cannot drift
    //  apart the way three hand kept lists did.
    //
    //  Each button has up to three jobs: a press, a press with Shift held, and a
    //  hold (about 0.7 s, without Shift). On a button with a hold the press acts
    //  when it is let go, since only then is it known not to be a hold; on one
    //  without, the moment it goes down.
    //
    //  The bottom button of the right hand column is Shift. A button with no
    //  Shift job does nothing then, rather than its first - a slip of the thumb
    //  should not quietly do something else. The rest of that column has no
    //  job at all for now (2026-10-09): every job is on the top row.

    /** A job a button can have. Nothing stores these, so the order is free;
        bindings that get saved (claude_instructions/midi_controller.md) will
        need names of their own. */
    enum class Job : std::uint8_t
    {
        none, shift,

        //  pressed
        rateFaster, rateSlower, moveRateSlower, moveRateFaster, cycleWalk, runGame, selfPlay, replay,

        //  pressed with Shift held
        stepBack, stepOn, newGame, playAgainst, waveReplay,

        //  held
        resetRate, resetMoveRate, firstMove, lastMove, clearBoard, bothOff, lifeThirty,

        //  on no button for now, kept so one can be given them back
        tieNotes, cyclePlace, pass, liftLast, loop, noteUp, noteDown, random,
        cyclePlayers, unload, redraw, lifeLonger, lifeShorter
    };

    struct EdgeButton
    {
        int index;
        Job plain, shifted, held;
        const char* plainLabel;         //  UTF-8, as the editor draws them
        const char* shiftedLabel;
        const char* heldLabel;
    };

    inline constexpr int shiftIndex = sceneIndex (7);

    /** The top row left to right, then the right hand column top to bottom. */
    inline constexpr std::array<EdgeButton, 16> edgeButtons
    { {
        { topIndex (0),   Job::rateFaster,     Job::none,        Job::resetRate,     "step rate +",            "",            "reset step rate" },
        { topIndex (1),   Job::rateSlower,     Job::none,        Job::resetMoveRate, "step rate \xe2\x88\x92",  "",            "reset move rate" },
        { topIndex (2),   Job::moveRateSlower, Job::stepBack,    Job::firstMove,     "move rate \xe2\x88\x92",  "position \xe2\x88\x92", "move 0" },
        { topIndex (3),   Job::moveRateFaster, Job::stepOn,      Job::lastMove,      "move rate +",            "position +",  "last move" },
        { topIndex (4),   Job::cycleWalk,      Job::none,        Job::none,          "walk",                   "",            "" },
        { topIndex (5),   Job::runGame,        Job::newGame,     Job::clearBoard,    "run game",               "new game",    "clear board" },
        { topIndex (6),   Job::selfPlay,       Job::playAgainst, Job::bothOff,       "auto play",              "you play",    "both off" },
        { topIndex (7),   Job::replay,         Job::waveReplay,  Job::lifeThirty,    "replay",                 "wave replay", "stone life 30" },

        { sceneIndex (0), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (1), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (2), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (3), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (4), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (5), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { sceneIndex (6), Job::none,           Job::none,        Job::none,          "",                       "",            "" },
        { shiftIndex,     Job::shift,          Job::none,        Job::none,          "shift",                  "",            "" },
    } };

    /** The button at this index, or nullptr for a pad, the logo or anything else. */
    inline constexpr const EdgeButton* buttonAt (int index) noexcept
    {
        for (const auto& button : edgeButtons)
            if (button.index == index)
                return &button;

        return nullptr;
    }

    /** What a press of this button does, with Shift held or not. */
    inline constexpr Job jobFor (int index, bool shift) noexcept
    {
        const auto* button = buttonAt (index);
        return button == nullptr ? Job::none : shift ? button->shifted : button->plain;
    }

    /** What holding this button does, or Job::none if a hold is only a press. */
    inline constexpr Job holdJobFor (int index) noexcept
    {
        const auto* button = buttonAt (index);
        return button == nullptr ? Job::none : button->held;
    }
}
