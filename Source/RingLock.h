#pragma once

//  Locked rings. A locked ring is frozen and silent: nothing is placed on its
//  points, nothing is lifted from them, no move captures a stone that stands on
//  them, and a playhead that walks over them sounds nothing.
//
//  This lives above the rules. go::Board knows nothing of it, and with an empty
//  mask every function here says "no" - which is why the AI can ask the same
//  question on every move and still write exactly the games it always wrote.
//
//  A ring is the lock's unit, not a playhead: the same points are locked whether
//  the board is walked as a spiral, as rings or as quadrants. Tengen belongs to
//  no ring (go::ringCount stops short of it), so it can never be locked.
//
//  Free of JUCE, like GoBoard.h, so tests/GoRulesTests.cpp checks it.

#include "GoBoard.h"

#include <algorithm>
#include <cstdint>

namespace ringlock
{
    /** Bit r is ring r, outermost first - the same numbering as go::ringOffset. */
    using Mask = std::uint16_t;

    static_assert (go::maxRings <= 16, "one bit per ring");

    inline constexpr Mask bit (int ring) noexcept { return (Mask) (1u << ring); }

    /** The ring a point lies on, or -1 for tengen. The ring is how far the point
        is from the nearest edge, which is also where the spiral puts it. */
    inline constexpr int ringOf (int size, int idx) noexcept
    {
        const int c = go::colOf (idx, size), r = go::rowOf (idx, size);
        const int ring = std::min (std::min (c, r), std::min (size - 1 - c, size - 1 - r));

        return ring < go::ringCount (size) ? ring : -1;
    }

    inline constexpr bool isLocked (Mask mask, int size, int idx) noexcept
    {
        if (mask == 0 || idx < 0 || idx >= size * size)
            return false;

        const int ring = ringOf (size, idx);
        return ring >= 0 && (mask & bit (ring)) != 0;
    }

    /** True if `after` has lost a stone that `before` had on a locked point -
        which is what a capture, or a suicide, does to one. Locked stones are
        immune, so a move that would do it is refused rather than played. */
    inline bool losesLockedStone (const go::Board& before, const go::Board& after, Mask mask) noexcept
    {
        if (mask == 0)
            return false;

        const int size = before.size();

        for (int idx = 0, cells = before.cellCount(); idx < cells; ++idx)
            if (before.at (idx) != go::Stone::none && after.at (idx) == go::Stone::none
                  && isLocked (mask, size, idx))
                return true;

        return false;
    }

    /** Whether the move may be played at all, judged by the lock alone: the point
        is not locked, and playing it (on a copy, with these rules) takes no locked
        stone off the board. A move the rules refuse is not this function's to
        judge - it answers true for it and lets the board say why. */
    inline bool allows (const go::Board& board, int idx, go::Stone colour,
                        bool allowSuicide, bool applyKo, Mask mask)
    {
        if (mask == 0)
            return true;

        if (isLocked (mask, board.size(), idx))
            return false;

        go::Board trial = board;

        if (trial.play (idx, colour, allowSuicide, applyKo) != go::MoveResult::ok)
            return true;

        return ! losesLockedStone (board, trial, mask);
    }
}
