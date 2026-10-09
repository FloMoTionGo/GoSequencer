#pragma once

//  A random position: the board cleared, then a third of its points played out
//  at once by the AI with a seed, a variation and a pair of players nobody chose.
//  Free of JUCE, like GoAI.h, so it is checked in tests/GoRulesTests.cpp; the
//  processor only draws the Pick and puts the stones down.
//
//  It is a short self-play game with one difference: no opening. Every game the
//  players write starts on the same ten moves (goai::openingPoint) - the fixed
//  motif self-play is built around - and a position of twenty or so moves taken
//  from such a game would be half the same every time.

#include "GoAI.h"

#include <cstdint>

namespace randompos
{
    /** How many moves a random position is played to: a third of the points,
        so 21 on an 8x8, 27 on a 9x9, 56 on a 13x13 and 120 on a 19x19. Captures
        leave a few fewer stones than that on the board. */
    inline constexpr int moveCount (int size) noexcept { return size * size / 3; }

    /** The variation range a pick is drawn from, per cent. Low enough to stay
        Go, high enough that the seed matters: at 0 every seed is the same game. */
    inline constexpr int minVariation = 30, maxVariation = 90;

    /** What one press chose. */
    struct Pick
    {
        goai::Players players = goai::Players::reading;
        bool swapStyles = false;        // Black plays the fighting style and White the territorial one
        int variation = 60;             // per cent
        std::uint32_t seed = 1;         // small and readable; mixed before the players see it
    };

    /** Settings for the game a position is taken from. The styles are given as
        aiSettingsFor in the processor gives them - 8x8 has reading weights of
        its own - and handed to the other colour when the pick says so. */
    inline goai::Settings settingsFor (int size, const Pick& pick)
    {
        goai::Settings settings;

        settings.size      = size;
        settings.moves     = moveCount (size);
        settings.players   = pick.players;
        settings.variation = pick.variation;
        settings.seed      = goai::gameSeed (pick.seed, 0);     //  as self-play mixes its seed

        settings.black        = pick.swapStyles ? goai::fighting()          : goai::territorial();
        settings.white        = pick.swapStyles ? goai::territorial()       : goai::fighting();
        settings.readingBlack = pick.swapStyles ? goai::readingWhite (size) : goai::readingBlack (size);
        settings.readingWhite = pick.swapStyles ? goai::readingBlack (size) : goai::readingWhite (size);

        //  No opening. An opening of one's own whose every point is -1 asks for
        //  nothing, so generate() hands the first move to the players as it does
        //  after the tenth - the opening without touching GoAI.h, whose games
        //  saved sessions replay from a seed.
        settings.opening.fill (-1);
        settings.hasOpening = true;

        return settings;
    }
}
