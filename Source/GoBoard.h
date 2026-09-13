#pragma once

//  Go / Baduk rules engine for the spiral step sequencer.
//  Deliberately free of JUCE and of any heap allocation, so the same header can
//  be unit-tested on its own (see tests/GoRulesTests.cpp) and touched from the
//  message thread without surprises.
//
//  The board size is a runtime value. Storage is always sized for the largest
//  board so nothing ever allocates; only the first size*size entries are live.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace go
{
    inline constexpr int minSize  = 9;
    inline constexpr int maxSize  = 19;
    inline constexpr int maxCells = maxSize * maxSize;   // 361

    /** The boards there are, smallest first. A size's place in this list is its
        slot: tables kept per size are indexed by it, and the plugin's board size
        choice lists them in the same order. */
    inline constexpr int sizeCount = 3;
    inline constexpr std::array<int, sizeCount> supportedSizes { 9, 13, 19 };

    inline constexpr bool isSupportedSize (int s) noexcept { return s == 9 || s == 13 || s == 19; }

    /** The slot of a supported size; anything else falls back to the 9x9's. */
    inline constexpr int sizeSlot (int s) noexcept { return s == 19 ? 2 : (s == 13 ? 1 : 0); }

    enum class Stone : std::uint8_t { none = 0, black = 1, white = 2 };

    inline constexpr Stone other (Stone s) noexcept
    {
        return s == Stone::black ? Stone::white
             : s == Stone::white ? Stone::black
                                 : Stone::none;
    }

    using Position = std::array<Stone, maxCells>;

    inline constexpr int index (int col, int row, int size) noexcept { return row * size + col; }
    inline constexpr int colOf (int idx, int size)          noexcept { return idx % size; }
    inline constexpr int rowOf (int idx, int size)          noexcept { return idx / size; }

    template <typename Fn>
    inline void forEachNeighbour (int idx, int size, Fn&& fn)
    {
        const int c = colOf (idx, size), r = rowOf (idx, size);

        if (c > 0)        fn (idx - 1);
        if (c < size - 1) fn (idx + 1);
        if (r > 0)        fn (idx - size);
        if (r < size - 1) fn (idx + size);
    }

    /** Clockwise spiral: starts top-left, runs along the outer edge and winds
        inwards, touching each intersection exactly once and finishing in the
        middle. Fills out[] and returns the number of steps. */
    inline int spiralOrder (int size, std::array<int, maxCells>& out) noexcept
    {
        int top = 0, bottom = size - 1, left = 0, right = size - 1, n = 0;

        while (top <= bottom && left <= right)
        {
            for (int c = left; c <= right; ++c)     out[(size_t) n++] = index (c, top, size);
            ++top;

            for (int r = top; r <= bottom; ++r)     out[(size_t) n++] = index (right, r, size);
            --right;

            if (top <= bottom)
            {
                for (int c = right; c >= left; --c) out[(size_t) n++] = index (c, bottom, size);
                --bottom;
            }

            if (left <= right)
            {
                for (int r = bottom; r >= top; --r) out[(size_t) n++] = index (left, r, size);
                ++left;
            }
        }

        return n;
    }

    /** The concentric rings of the board, outermost first. Tengen - the lone
        point in the middle of an odd board - is not a ring: it has nowhere to
        rotate to, so it is left out and ringCount() stops short of it.

        The spiral above already walks the board ring by ring, so a ring needs
        no table of its own: it is the slice of the spiral that starts at
        ringOffset() and runs for ringLength() points.

            9x9    4 rings of 32, 24, 16 and  8 points
            13x13  6 rings of 48, 40, 32, 24, 16 and 8 points
            19x19  9 rings of 72, 64, 56, 48, 40, 32, 24, 16 and 8 points

        The lengths fall in whole number ratios (4:3:2:1, 6:5:4:3:2:1 and
        9:8:...:1), which is what makes one playhead per ring a polyrhythm
        rather than a mess. */
    inline constexpr int ringCount  (int size)        noexcept { return (size - 1) / 2; }
    inline constexpr int ringOffset (int size, int r) noexcept { return 4 * r * (size - r); }
    inline constexpr int ringLength (int size, int r) noexcept { return 4 * (size - 1 - 2 * r); }

    inline constexpr int maxRings = ringCount (maxSize);      // 9, on a 19x19

    /** The four quadrant blocks. Each is half the board plus the line the blocks
        share: side (size + 1) / 2, so two of them side by side span exactly the
        board, and the four cover it, meeting on the middle row and column.

            9x9    four 5x5 blocks,   5 + 5 - 1 == 9
            13x13  four 7x7 blocks,   7 + 7 - 1 == 13
            19x19  four 10x10 blocks, 10 + 10 - 1 == 19

        On the two smaller boards the side is odd, and the block's centre is the
        corner star point - the san-san (3-3) point on a 9x9, the 4-4 point on a
        13x13. A 19x19 block is even, so it has no single centre to wind in to:
        its spiral ends on the square of four points in its middle, the 5-5 to
        6-6 points of that corner. Its star point is the 4-4 one, a line further
        out, on the spiral's second to last ring.

        Quadrants run clockwise from the top left, like the spiral. */
    inline constexpr int quadCount = 4;

    inline constexpr int quadSide   (int size) noexcept { return (size + 1) / 2; }
    inline constexpr int quadSteps  (int size) noexcept { return quadSide (size) * quadSide (size); }

    inline constexpr int quadOriginCol (int size, int q) noexcept
    {
        return (q == 1 || q == 2) ? quadSide (size) - 1 : 0;
    }

    inline constexpr int quadOriginRow (int size, int q) noexcept
    {
        return (q >= 2) ? quadSide (size) - 1 : 0;
    }

    /** One quadrant's spiral, winding in to the middle of its block. Reverse it
        to wind out from there instead. Fills out[] and returns the length. */
    inline int quadOrder (int size, int q, std::array<int, maxCells>& out) noexcept
    {
        const int d = quadSide (size);

        std::array<int, maxCells> local {};
        const int n = spiralOrder (d, local);

        const int ox = quadOriginCol (size, q), oy = quadOriginRow (size, q);

        for (int i = 0; i < n; ++i)
            out[(size_t) i] = index (ox + colOf (local[(size_t) i], d),
                                     oy + rowOf (local[(size_t) i], d),
                                     size);

        return n;
    }

    /** The handicap points: the four corner stars and tengen. */
    inline std::array<int, 5> starPoints (int size) noexcept
    {
        const int e = (size >= 13 ? 3 : 2);          // 4-4 points on 13x13 and 19x19, 3-3 on 9x9
        const int f = size - 1 - e;
        const int m = size / 2;

        return { index (e, e, size), index (f, e, size),
                 index (e, f, size), index (f, f, size),
                 index (m, m, size) };
    }

    inline constexpr int maxHoshi = 9;

    /** Every point a goban marks with a dot: the star points above, plus the
        four side stars a 19x19 carries half way along each 4th line. The small
        boards mark only the five. Fills out[] and returns how many. */
    inline int hoshiPoints (int size, std::array<int, maxHoshi>& out) noexcept
    {
        const auto stars = starPoints (size);
        int n = 0;

        for (int star : stars)
            out[(size_t) n++] = star;

        if (size >= 19)
        {
            const int e = 3, f = size - 1 - e, m = size / 2;

            out[(size_t) n++] = index (m, e, size);
            out[(size_t) n++] = index (e, m, size);
            out[(size_t) n++] = index (f, m, size);
            out[(size_t) n++] = index (m, f, size);
        }

        return n;
    }

    enum class MoveResult { ok, occupied, suicide, ko, outOfRange };

    /** A chain of connected stones, plus the liberties the chain shares. */
    struct GroupScan
    {
        std::array<int, maxCells> stones {};
        int count     = 0;
        int liberties = 0;
    };

    inline void scanGroup (const Position& p, int start, int size, GroupScan& out)
    {
        out.count     = 0;
        out.liberties = 0;

        const Stone colour = p[(size_t) start];

        if (colour == Stone::none)
            return;

        std::array<bool, maxCells> inGroup {}, libertySeen {};
        std::array<int, maxCells> stack {};
        int sp = 0;

        stack[(size_t) sp++]    = start;
        inGroup[(size_t) start] = true;

        while (sp > 0)
        {
            const int idx = stack[(size_t) --sp];
            out.stones[(size_t) out.count++] = idx;

            forEachNeighbour (idx, size, [&] (int n)
            {
                const Stone s = p[(size_t) n];

                if (s == Stone::none)
                {
                    if (! libertySeen[(size_t) n])
                    {
                        libertySeen[(size_t) n] = true;
                        ++out.liberties;
                    }
                }
                else if (s == colour && ! inGroup[(size_t) n])
                {
                    inGroup[(size_t) n]  = true;
                    stack[(size_t) sp++] = n;
                }
            });
        }
    }

    inline int libertiesAt (const Position& p, int idx, int size)
    {
        GroupScan scan;
        scanGroup (p, idx, size, scan);
        return scan.liberties;
    }

    //==============================================================================
    class Board
    {
    public:
        explicit Board (int boardSize = 9) : size_ (isSupportedSize (boardSize) ? boardSize : 9) { clear(); }

        int size()      const noexcept { return size_; }
        int cellCount() const noexcept { return size_ * size_; }

        /** Changing the size wipes the board - the points mean something else now. */
        void setSize (int newSize)
        {
            if (! isSupportedSize (newSize) || newSize == size_)
                return;

            size_ = newSize;
            clear();
        }

        void clear()
        {
            cells_.fill (Stone::none);
            previous_.fill (Stone::none);
            hasPrevious_      = false;
            capturedBlack_    = 0;
            capturedWhite_    = 0;
            lastMove_         = -1;
            lastCaptureCount_ = 0;
        }

        Stone at (int idx) const noexcept
        {
            return (idx >= 0 && idx < cellCount()) ? cells_[(size_t) idx] : Stone::none;
        }

        const Position& position() const noexcept { return cells_; }

        int stoneCount() const noexcept
        {
            int n = 0;

            for (int i = 0; i < cellCount(); ++i)
                if (cells_[(size_t) i] != Stone::none)
                    ++n;

            return n;
        }

        /** Plays a stone under Go rules: enemy chains left without liberties are
            lifted first, then the played chain is tested for suicide, then the
            resulting position is tested against the ko rule. The board is left
            untouched unless the move is legal. */
        MoveResult play (int idx, Stone colour, bool allowSuicide, bool applyKoRule)
        {
            if (idx < 0 || idx >= cellCount() || colour == Stone::none)
                return MoveResult::outOfRange;

            if (cells_[(size_t) idx] != Stone::none)
                return MoveResult::occupied;

            const Position before = cells_;
            Position work = cells_;
            work[(size_t) idx] = colour;

            //  1. lift enemy chains that have just run out of liberties
            const Stone opponent = other (colour);
            int capturedOpponent = 0;
            GroupScan scan;

            forEachNeighbour (idx, size_, [&] (int n)
            {
                if (work[(size_t) n] != opponent)
                    return;

                scanGroup (work, n, size_, scan);

                if (scan.liberties == 0)
                {
                    for (int i = 0; i < scan.count; ++i)
                        work[(size_t) scan.stones[(size_t) i]] = Stone::none;

                    capturedOpponent += scan.count;
                }
            });

            //  2. suicide, judged only after those captures have been taken
            scanGroup (work, idx, size_, scan);
            int capturedSelf = 0;

            if (scan.liberties == 0)
            {
                if (! allowSuicide)
                    return MoveResult::suicide;

                for (int i = 0; i < scan.count; ++i)
                    work[(size_t) scan.stones[(size_t) i]] = Stone::none;

                capturedSelf = scan.count;
            }

            //  3. ko: a move may not recreate the position that stood before the
            //     previous move - the immediate recapture
            if (applyKoRule && hasPrevious_ && work == previous_)
                return MoveResult::ko;

            previous_    = before;
            hasPrevious_ = true;
            cells_       = work;

            if (colour == Stone::black) capturedWhite_ += capturedOpponent;
            else                        capturedBlack_ += capturedOpponent;

            if (colour == Stone::black) capturedBlack_ += capturedSelf;
            else                        capturedWhite_ += capturedSelf;

            lastMove_         = (capturedSelf > 0 ? -1 : idx);
            lastCaptureCount_ = capturedOpponent + capturedSelf;
            return MoveResult::ok;
        }

        /** Lifts a stone by hand. Not a Go move - it is the sequencer's eraser -
            so it also forgets the ko history. */
        bool removeStone (int idx)
        {
            if (idx < 0 || idx >= cellCount() || cells_[(size_t) idx] == Stone::none)
                return false;

            cells_[(size_t) idx] = Stone::none;
            hasPrevious_ = false;

            if (lastMove_ == idx)
                lastMove_ = -1;

            return true;
        }

        /** Drops a stone in place, ignoring captures and legality. Used for
            handicap stones and when restoring saved state. */
        void setStone (int idx, Stone s)
        {
            if (idx >= 0 && idx < cellCount())
                cells_[(size_t) idx] = s;

            hasPrevious_ = false;
        }

        int capturedBlack()    const noexcept { return capturedBlack_; }   // black stones lifted
        int capturedWhite()    const noexcept { return capturedWhite_; }   // white stones lifted
        int lastMove()         const noexcept { return lastMove_; }
        int lastCaptureCount() const noexcept { return lastCaptureCount_; }

        void resetCaptureCounts() noexcept { capturedBlack_ = capturedWhite_ = 0; }

        std::string toString() const
        {
            std::string s ((size_t) cellCount(), '0');

            for (int i = 0; i < cellCount(); ++i)
                s[(size_t) i] = (char) ('0' + (int) cells_[(size_t) i]);

            return s;
        }

        void fromString (const std::string& s)
        {
            clear();

            const int n = (int) (s.size() < (size_t) cellCount() ? s.size() : (size_t) cellCount());

            for (int i = 0; i < n; ++i)
                cells_[(size_t) i] = (s[(size_t) i] == '1') ? Stone::black
                                   : (s[(size_t) i] == '2') ? Stone::white
                                                            : Stone::none;
        }

    private:
        int size_ = 9;
        Position cells_ {}, previous_ {};
        bool hasPrevious_     = false;
        int  capturedBlack_   = 0, capturedWhite_ = 0;
        int  lastMove_        = -1, lastCaptureCount_ = 0;
    };
}
