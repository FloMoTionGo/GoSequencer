//  Standalone checks for the spiral order, the life-and-death rules and the SGF
//  reader. Builds without JUCE:
//      g++ -std=c++17 -I../Source GoRulesTests.cpp -o tests
//  An SGF path may be passed as argv[1] to check a real game record as well.

#include "GoAI.h"
#include "GoBoard.h"
#include "SgfParser.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    int failures = 0;

    void check (bool condition, const std::string& what)
    {
        if (! condition)
        {
            std::printf ("  FAIL  %s\n", what.c_str());
            ++failures;
        }
        else
        {
            std::printf ("  ok    %s\n", what.c_str());
        }
    }

    using go::Stone;
    using go::MoveResult;

    constexpr int S = 9;

    int ix (int col, int row)             { return go::index (col, row, S); }
    int ix13 (int col, int row)           { return go::index (col, row, 13); }
    int ix19 (int col, int row)           { return go::index (col, row, 19); }

    void testSpiral()
    {
        std::printf ("spiral order, 9x9\n");

        std::array<int, go::maxCells> order {};
        const int count = go::spiralOrder (S, order);

        check (count == 81, "81 steps");

        std::array<int, go::maxCells> seen {};
        bool inRange = true;

        for (int i = 0; i < count; ++i)
        {
            if (order[(size_t) i] < 0 || order[(size_t) i] >= count) inRange = false;
            else ++seen[(size_t) order[(size_t) i]];
        }

        bool everyPointOnce = true;

        for (int i = 0; i < count; ++i)
            if (seen[(size_t) i] != 1)
                everyPointOnce = false;

        check (inRange, "every step is on the board");
        check (everyPointOnce, "all 81 points visited exactly once");
        check (order[0] == ix (0, 0), "starts top left");
        check (order[1] == ix (1, 0), "runs along the top edge first");
        check (order[8] == ix (8, 0), "reaches the top right corner on step 9");
        check (order[9] == ix (8, 1), "turns down the right edge");
        check (order[16] == ix (8, 8), "reaches the bottom right corner");
        check (order[24] == ix (0, 8), "reaches the bottom left corner");
        check (order[31] == ix (0, 1), "closes the outer ring below the start");
        check (order[32] == ix (1, 1), "steps inwards onto the second ring");
        check (order[80] == ix (4, 4), "ends on tengen");
    }

    void testSpiral13()
    {
        std::printf ("spiral order, 13x13\n");

        std::array<int, go::maxCells> order {};
        const int count = go::spiralOrder (13, order);

        check (count == 169, "169 steps");

        std::array<int, go::maxCells> seen {};

        for (int i = 0; i < count; ++i)
            ++seen[(size_t) order[(size_t) i]];

        bool everyPointOnce = true;

        for (int i = 0; i < count; ++i)
            if (seen[(size_t) i] != 1)
                everyPointOnce = false;

        check (everyPointOnce, "all 169 points visited exactly once");
        check (order[0] == ix13 (0, 0), "starts top left");
        check (order[12] == ix13 (12, 0), "reaches the top right corner on step 13");
        check (order[24] == ix13 (12, 12), "reaches the bottom right corner");
        check (order[count - 1] == ix13 (6, 6), "ends on tengen");
    }

    void testSpiral19()
    {
        std::printf ("spiral order, 19x19\n");

        std::array<int, go::maxCells> order {};
        const int count = go::spiralOrder (19, order);

        check (count == 361, "361 steps");
        check (go::maxCells == 361, "storage is sized for the full board");

        std::array<int, go::maxCells> seen {};

        for (int i = 0; i < count; ++i)
            ++seen[(size_t) order[(size_t) i]];

        bool everyPointOnce = true;

        for (int i = 0; i < count; ++i)
            if (seen[(size_t) i] != 1)
                everyPointOnce = false;

        check (everyPointOnce, "all 361 points visited exactly once");
        check (order[0] == ix19 (0, 0), "starts top left");
        check (order[18] == ix19 (18, 0), "reaches the top right corner on step 19");
        check (order[36] == ix19 (18, 18), "reaches the bottom right corner");
        check (order[count - 1] == ix19 (9, 9), "ends on tengen");
    }

    /** The rings the polyrhythm mode plays: concentric, outermost first, with
        tengen left out. They are slices of the spiral, so this checks the two
        agree. */
    void testRings()
    {
        std::printf ("ring geometry\n");

        check (go::ringCount (9)  == 4, "9x9 has four rings once tengen is left out");
        check (go::ringCount (13) == 6, "13x13 has six rings once tengen is left out");
        check (go::ringCount (19) == 9, "19x19 has nine rings once tengen is left out");
        check (go::maxRings == 9, "storage is sized for the biggest board");

        check (go::ringLength (9, 0) == 32 && go::ringLength (9, 1) == 24
                 && go::ringLength (9, 2) == 16 && go::ringLength (9, 3) == 8,
               "9x9 rings run 32, 24, 16, 8 - a 4:3:2:1 polyrhythm");

        bool fullBoardRatios = true;

        for (int r = 0; r < go::ringCount (19); ++r)
            if (go::ringLength (19, r) != 8 * (9 - r))
                fullBoardRatios = false;

        check (fullBoardRatios, "19x19 rings run 72 down to 8 - a 9:8:...:1 polyrhythm");

        for (int size : { 9, 13, 19 })
        {
            const std::string label = " (" + std::to_string (size) + "x" + std::to_string (size) + ")";

            std::array<int, go::maxCells> order {};
            const int count = go::spiralOrder (size, order);

            const int rings = go::ringCount (size);
            const int centre = go::index (size / 2, size / 2, size);

            std::array<int, go::maxCells> seen {};
            int total = 0;
            bool offsetsChain = true, depthMatches = true;

            for (int r = 0; r < rings; ++r)
            {
                if (go::ringOffset (size, r) != total)
                    offsetsChain = false;

                for (int i = 0; i < go::ringLength (size, r); ++i)
                {
                    const int idx = order[(size_t) (go::ringOffset (size, r) + i)];
                    const int col = go::colOf (idx, size), row = go::rowOf (idx, size);
                    const int depth = std::min (std::min (col, row),
                                                std::min (size - 1 - col, size - 1 - row));

                    if (depth != r)
                        depthMatches = false;

                    ++seen[(size_t) idx];
                }

                total += go::ringLength (size, r);
            }

            check (offsetsChain, "each ring starts where the one before it ended" + label);
            check (depthMatches, "every point of ring r sits r steps in from the edge" + label);
            check (total == count - 1, "the rings cover the board bar tengen" + label);
            check (go::ringOffset (size, rings) == count - 1, "tengen is the last point of the spiral" + label);

            bool coverage = true;

            for (int i = 0; i < count; ++i)
                if (seen[(size_t) i] != (i == centre ? 0 : 1))
                    coverage = false;

            check (coverage, "no point is on two rings, and only tengen is on none" + label);
        }
    }

    /** The four quadrant spirals: one per corner star point, meeting on the
        middle row and column. */
    void testQuads()
    {
        std::printf ("quadrant spirals\n");

        check (go::quadSide (9) == 5 && go::quadSteps (9) == 25,
               "9x9 gives four 5x5 quadrants of 25 points");
        check (go::quadSide (13) == 7 && go::quadSteps (13) == 49,
               "13x13 gives four 7x7 quadrants of 49 points");
        check (go::quadSide (19) == 10 && go::quadSteps (19) == 100,
               "19x19 gives four 10x10 quadrants of 100 points");

        for (int size : { 9, 13, 19 })
        {
            const std::string label = " (" + std::to_string (size) + "x" + std::to_string (size) + ")";
            const int side = go::quadSide (size);

            check (2 * go::quadSide (size) - 1 == size,
                   "two quadrants span the board, sharing one line" + label);

            //  the centre of each quadrant is the corner star point it is built on
            const auto stars = go::starPoints (size);
            const int wantCentre[4] = { stars[0], stars[1], stars[3], stars[2] };   // clockwise

            std::array<int, go::maxCells> order {};
            std::array<int, go::maxCells> seen {};
            bool centresMatch = true, inRange = true;

            for (int q = 0; q < go::quadCount; ++q)
            {
                const int n = go::quadOrder (size, q, order);

                if (n != go::quadSteps (size))
                    inRange = false;

                //  the inward spiral finishes on the star point - or, on a
                //  19x19, whose blocks are even and have no single centre, on
                //  the square of four points in the middle of the block
                const int last = order[(size_t) (n - 1)];

                if (side % 2 == 0)
                {
                    const int c  = go::colOf (last, size) - go::quadOriginCol (size, q);
                    const int rr = go::rowOf (last, size) - go::quadOriginRow (size, q);

                    if (c < side / 2 - 1 || c > side / 2 || rr < side / 2 - 1 || rr > side / 2)
                        centresMatch = false;
                }
                else if (last != wantCentre[q])
                {
                    centresMatch = false;
                }

                //  and wherever the spiral ends, the corner's star point is in
                //  its own block
                const int sc = go::colOf (wantCentre[q], size) - go::quadOriginCol (size, q);
                const int sr = go::rowOf (wantCentre[q], size) - go::quadOriginRow (size, q);

                if (sc < 0 || sr < 0 || sc >= side || sr >= side)
                    centresMatch = false;

                for (int i = 0; i < n; ++i)
                {
                    const int idx = order[(size_t) i];

                    if (idx < 0 || idx >= size * size) inRange = false;
                    else ++seen[(size_t) idx];
                }
            }

            check (inRange, "every quadrant point lands on the board" + label);
            check (centresMatch, side % 2 == 0 ? "each spiral winds in to the middle of its block, around its star point" + label
                                               : "each spiral winds in to its own star point" + label);

            //  coverage: the middle row and column belong to two quadrants,
            //  tengen to all four, everything else to exactly one
            const int m = size / 2;
            bool coverage = true;

            for (int c = 0; c < size; ++c)
                for (int rr = 0; rr < size; ++rr)
                {
                    const int want = (c == m && rr == m) ? 4 : ((c == m || rr == m) ? 2 : 1);

                    if (seen[(size_t) go::index (c, rr, size)] != want)
                        coverage = false;
                }

            check (coverage, "quadrants cover the board, overlapping only on the middle cross" + label);
        }
    }

    void testStarPoints()
    {
        std::printf ("star points\n");

        const auto small = go::starPoints (9);
        check (small[0] == ix (2, 2) && small[3] == ix (6, 6), "9x9 stars sit on the 3-3 points");
        check (small[4] == ix (4, 4), "9x9 tengen");

        const auto large = go::starPoints (13);
        check (large[0] == ix13 (3, 3) && large[3] == ix13 (9, 9), "13x13 stars sit on the 4-4 points");
        check (large[4] == ix13 (6, 6), "13x13 tengen");

        const auto full = go::starPoints (19);
        check (full[0] == ix19 (3, 3) && full[3] == ix19 (15, 15), "19x19 stars sit on the 4-4 points");
        check (full[4] == ix19 (9, 9), "19x19 tengen");

        std::array<int, go::maxHoshi> dots {};
        check (go::hoshiPoints (9, dots) == 5 && go::hoshiPoints (13, dots) == 5, "the small boards mark five points");

        const int marked = go::hoshiPoints (19, dots);

        auto isMarked = [&] (int idx)
        {
            return std::count (dots.begin(), dots.begin() + marked, idx) == 1;
        };

        check (marked == 9 && isMarked (ix19 (9, 3)) && isMarked (ix19 (3, 9))
                 && isMarked (ix19 (15, 9)) && isMarked (ix19 (9, 15)) && isMarked (ix19 (9, 9)),
               "a 19x19 marks nine, the four side stars included");
    }

    void testLiberties()
    {
        std::printf ("liberties\n");

        go::Board b;
        check (b.play (ix (0, 0), Stone::black, false, true) == MoveResult::ok, "corner move legal");
        check (go::libertiesAt (b.position(), ix (0, 0), S) == 2, "corner stone has 2 liberties");

        b.clear();
        b.play (ix (4, 0), Stone::black, false, true);
        check (go::libertiesAt (b.position(), ix (4, 0), S) == 3, "edge stone has 3 liberties");

        b.clear();
        b.play (ix (4, 4), Stone::black, false, true);
        check (go::libertiesAt (b.position(), ix (4, 4), S) == 4, "centre stone has 4 liberties");

        b.clear();
        b.play (ix (4, 4), Stone::black, false, true);
        b.play (ix (5, 4), Stone::black, false, true);
        check (go::libertiesAt (b.position(), ix (4, 4), S) == 6, "two connected stones share 6 liberties");
    }

    void testSingleCapture()
    {
        std::printf ("capturing a single stone\n");

        go::Board b;
        b.play (ix (4, 4), Stone::white, false, true);
        b.play (ix (3, 4), Stone::black, false, true);
        b.play (ix (5, 4), Stone::black, false, true);
        b.play (ix (4, 3), Stone::black, false, true);

        check (b.at (ix (4, 4)) == Stone::white, "white still standing on its last liberty");

        const auto r = b.play (ix (4, 5), Stone::black, false, true);

        check (r == MoveResult::ok, "filling the last liberty is legal");
        check (b.at (ix (4, 4)) == Stone::none, "surrounded white stone is lifted");
        check (b.capturedWhite() == 1, "one white prisoner counted");
        check (b.lastCaptureCount() == 1, "capture count reported");
    }

    void testGroupCapture()
    {
        std::printf ("capturing a group\n");

        go::Board b;
        b.play (ix (0, 0), Stone::white, false, true);
        b.play (ix (1, 0), Stone::white, false, true);
        b.play (ix (2, 0), Stone::black, false, true);
        b.play (ix (0, 1), Stone::black, false, true);

        check (b.at (ix (0, 0)) == Stone::white, "white chain alive with one liberty left");

        const auto r = b.play (ix (1, 1), Stone::black, false, true);

        check (r == MoveResult::ok, "final liberty may be filled");
        check (b.at (ix (0, 0)) == Stone::none && b.at (ix (1, 0)) == Stone::none,
               "the whole chain is lifted, not just one stone");
        check (b.capturedWhite() == 2, "two white prisoners counted");
    }

    void testCapture13()
    {
        std::printf ("capturing on the bigger board\n");

        go::Board b (13);
        check (b.size() == 13 && b.cellCount() == 169, "board reports 13x13");

        b.setStone (ix13 (12, 6), Stone::white);
        b.setStone (ix13 (12, 5), Stone::black);
        b.setStone (ix13 (12, 7), Stone::black);

        check (b.play (ix13 (11, 6), Stone::black, false, true) == MoveResult::ok, "black fills the last liberty");
        check (b.at (ix13 (12, 6)) == Stone::none, "the stone on the right edge is lifted");
        check (b.at (ix13 (12, 5)) == Stone::black, "neighbouring rows are untouched");
    }

    void testCapture19()
    {
        std::printf ("capturing on the full board\n");

        go::Board b (19);
        check (b.size() == 19 && b.cellCount() == 361, "board reports 19x19");

        //  the far corner: the last indices of the board, beyond anything a
        //  13x13 ever reached
        b.setStone (ix19 (18, 18), Stone::white);
        b.setStone (ix19 (17, 18), Stone::white);
        b.setStone (ix19 (16, 18), Stone::black);
        b.setStone (ix19 (17, 17), Stone::black);

        check (b.play (ix19 (18, 17), Stone::black, false, true) == MoveResult::ok, "black fills the corner's last liberty");
        check (b.at (ix19 (18, 18)) == Stone::none && b.at (ix19 (17, 18)) == Stone::none,
               "both stones in the bottom right corner are lifted");
        check (b.capturedWhite() == 2, "two white prisoners counted");
        check (go::libertiesAt (b.position(), ix19 (18, 17), 19) == 5,
               "the capturing chain takes the two freed points as liberties");
    }

    void testSizeChangeClears()
    {
        std::printf ("changing size\n");

        go::Board b;
        b.play (ix (4, 4), Stone::black, false, true);
        b.setSize (13);

        check (b.size() == 13, "size changed");
        check (b.stoneCount() == 0, "the board is wiped, the points mean something else now");

        b.play (ix13 (6, 6), Stone::black, false, true);
        b.setSize (19);
        check (b.size() == 19 && b.stoneCount() == 0, "on to 19x19, wiped again");

        b.play (ix19 (9, 9), Stone::black, false, true);
        b.setSize (21);
        check (b.size() == 19 && b.stoneCount() == 1, "a size there is no board for is refused, and the board kept");
    }

    void testTwoEyesLive()
    {
        std::printf ("two eyes live\n");

        go::Board b;
        const int whites[] = { ix (1, 0), ix (3, 0), ix (0, 1), ix (1, 1), ix (2, 1), ix (3, 1) };

        for (int w : whites)
            b.setStone (w, Stone::white);

        const int blacks[] = { ix (4, 0), ix (4, 1), ix (0, 2), ix (1, 2), ix (2, 2), ix (3, 2), ix (4, 2) };

        for (int bl : blacks)
            b.setStone (bl, Stone::black);

        check (b.play (ix (0, 0), Stone::black, false, true) == MoveResult::suicide, "first eye cannot be filled");
        check (b.play (ix (2, 0), Stone::black, false, true) == MoveResult::suicide, "second eye cannot be filled");
        check (b.at (ix (1, 0)) == Stone::white, "the group with two eyes is alive");
    }

    void testSuicide()
    {
        std::printf ("suicide\n");

        go::Board b;
        b.setStone (ix (1, 0), Stone::white);
        b.setStone (ix (0, 1), Stone::white);

        check (b.play (ix (0, 0), Stone::black, false, true) == MoveResult::suicide,
               "self capture rejected under Go rules");
        check (b.at (ix (0, 0)) == Stone::none, "board untouched after an illegal move");

        check (b.play (ix (0, 0), Stone::black, true, true) == MoveResult::ok,
               "self capture allowed when the rule is relaxed");
        check (b.at (ix (0, 0)) == Stone::none, "the self captured stone does not stay on the board");
        check (b.capturedBlack() == 1, "self capture counted as a black prisoner");
    }

    void testCaptureBeatsSuicide()
    {
        std::printf ("capture is resolved before suicide\n");

        go::Board b;
        b.setStone (ix (1, 0), Stone::white);
        b.setStone (ix (0, 1), Stone::white);
        b.setStone (ix (2, 0), Stone::black);
        b.setStone (ix (1, 1), Stone::black);
        b.setStone (ix (0, 2), Stone::black);

        const auto r = b.play (ix (0, 0), Stone::black, false, true);

        check (r == MoveResult::ok, "move is legal because it captures first");
        check (b.at (ix (0, 0)) == Stone::black, "black stone stays on the board");
        check (b.at (ix (1, 0)) == Stone::none && b.at (ix (0, 1)) == Stone::none, "both white stones are lifted");
        check (b.capturedWhite() == 2, "two white prisoners counted");
    }

    void testKo()
    {
        std::printf ("ko\n");

        auto build = [] (go::Board& b)
        {
            b.setStone (ix (1, 0), Stone::black);
            b.setStone (ix (0, 1), Stone::black);
            b.setStone (ix (1, 2), Stone::black);
            b.setStone (ix (1, 1), Stone::white);
            b.setStone (ix (2, 0), Stone::white);
            b.setStone (ix (3, 1), Stone::white);
            b.setStone (ix (2, 2), Stone::white);
        };

        go::Board b;
        build (b);

        check (b.play (ix (2, 1), Stone::black, false, true) == MoveResult::ok, "black takes the ko");
        check (b.at (ix (1, 1)) == Stone::none, "the white stone is lifted");

        check (b.play (ix (1, 1), Stone::white, false, true) == MoveResult::ko,
               "white may not recapture immediately");
        check (b.at (ix (2, 1)) == Stone::black, "board untouched after the ko violation");

        check (b.play (ix (8, 8), Stone::white, false, true) == MoveResult::ok, "white plays elsewhere");
        check (b.play (ix (1, 1), Stone::white, false, true) == MoveResult::ok,
               "the ko may be retaken after an intervening move");

        go::Board c;
        build (c);
        c.play (ix (2, 1), Stone::black, false, true);
        check (c.play (ix (1, 1), Stone::white, false, false) == MoveResult::ok,
               "recapture is allowed when the ko rule is switched off");
    }

    void testOccupiedAndErase()
    {
        std::printf ("occupied points and the eraser\n");

        go::Board b;
        b.play (ix (4, 4), Stone::black, false, true);

        check (b.play (ix (4, 4), Stone::white, false, true) == MoveResult::occupied, "no two stones on one point");
        check (b.at (ix (4, 4)) == Stone::black, "the standing stone is unchanged");
        check (b.removeStone (ix (4, 4)), "eraser lifts a stone");
        check (b.at (ix (4, 4)) == Stone::none, "point is empty again");
        check (! b.removeStone (ix (4, 4)), "erasing an empty point does nothing");
    }

    void testSerialisation()
    {
        std::printf ("state round trip\n");

        go::Board b;
        b.play (ix (0, 0), Stone::black, false, true);
        b.play (ix (8, 8), Stone::white, false, true);
        b.play (ix (4, 4), Stone::black, false, true);

        const auto s = b.toString();
        check ((int) s.size() == 81, "81 characters written");

        go::Board restored;
        restored.fromString (s);

        bool same = true;

        for (int i = 0; i < 81; ++i)
            if (restored.at (i) != b.at (i))
                same = false;

        check (same, "board survives the round trip");

        go::Board full (19);
        full.play (ix19 (18, 18), Stone::white, false, true);
        full.play (ix19 (0, 18), Stone::black, false, true);

        const auto t = full.toString();
        check ((int) t.size() == 361, "361 characters written for a 19x19");

        go::Board fullRestored (19);
        fullRestored.fromString (t);

        check (fullRestored.stoneCount() == 2 && fullRestored.at (ix19 (18, 18)) == Stone::white
                 && fullRestored.at (ix19 (0, 18)) == Stone::black,
               "the far corners of a 19x19 survive the round trip");
    }

    //==============================================================================
    void testSgfBasics()
    {
        std::printf ("sgf reader\n");

        const std::string text =
            "(;FF[4]GM[1]SZ[9]PB[Kuro]BR[5k]PW[Shiro]WR[4k]RE[B+R]DT[2026-01-02]"
            "C[a comment with a bracket \\] and a paren ) inside]"
            ";B[aa];W[bb]"
            "(;B[cc];W[dd])"          //  main line
            "(;B[ii];W[hh])"          //  a variation that must be ignored
            ")";

        const auto game = sgf::parse (text);

        check (game.valid, "record parses");
        check (game.size == 9, "board size read from SZ");
        check (game.blackName == "Kuro" && game.whiteName == "Shiro", "player names read");
        check (game.result == "B+R", "result read");
        check (game.players() == "Kuro (5k) vs Shiro (4k)", "players summarised with ranks");
        check (game.moveCount() == 4, "only the main line is followed");
        check (game.moves[0].colour == Stone::black && game.moves[0].index == ix (0, 0), "B[aa] is the top left point");
        check (game.moves[1].index == ix (1, 1), "W[bb] is one point in from the corner");
        check (game.moves[3].index == ix (3, 3), "the main line continues into the first subtree");
    }

    void testSgfPassesAndSetup()
    {
        std::printf ("sgf passes and handicap\n");

        const auto game = sgf::parse ("(;FF[4]SZ[9]AB[cc][gg]AW[gc];B[];W[tt];B[ee])");

        check (game.valid, "record parses");
        check (game.setup.size() == 3, "AB and AW stones collected as setup");
        check (game.setup[0].colour == Stone::black && game.setup[0].index == ix (2, 2), "AB point converted");
        check (game.setup[2].colour == Stone::white && game.setup[2].index == ix (6, 2), "AW point converted");
        check (game.moveCount() == 3, "three moves");
        check (game.moves[0].isPass, "an empty value is a pass");
        check (game.moves[1].isPass, "tt is a pass on a small board");
        check (! game.moves[2].isPass && game.moves[2].index == ix (4, 4), "a real move follows");

        const auto full = sgf::parse ("(;FF[4]SZ[19]AB[dd];W[ss];B[tt];W[pd])");

        check (full.valid && full.size == 19 && full.moveCount() == 3, "a 19x19 record parses");
        check (full.setup[0].index == ix19 (3, 3), "AB[dd] is the upper left star point");
        check (! full.moves[0].isPass && full.moves[0].index == ix19 (18, 18), "ss is the bottom right corner");
        check (full.moves[1].isPass, "tt is still a pass on the full board");
        check (full.moves[2].index == ix19 (15, 3), "pd is the upper right star point");
    }

    void testSgfFailures()
    {
        std::printf ("sgf refusals\n");

        const auto empty = sgf::parse ("");
        check (! empty.valid && ! empty.error.empty(), "empty text is refused with a reason");

        const auto noMoves = sgf::parse ("(;FF[4]SZ[9])");
        check (! noMoves.valid, "a record without moves is refused");

        const auto big = sgf::parse ("(;FF[4]SZ[19];B[aa])");
        check (big.valid && big.size == 19, "a 19x19 record still parses - the plugin decides what it supports");
        check (go::isSupportedSize (big.size), "and 19x19 is a board it has");

        const auto odd = sgf::parse ("(;FF[4]SZ[21];B[aa])");
        check (odd.valid && ! go::isSupportedSize (odd.size), "a 21x21 parses too, but there is no board for it");
        check (! go::isSupportedSize (7) && ! go::isSupportedSize (15), "nor for any size between the three");
    }

    void testSgfFile (const char* path)
    {
        std::printf ("sgf file: %s\n", path);

        std::ifstream stream (path, std::ios::binary);

        if (! stream)
        {
            std::printf ("  skip  file not found\n");
            return;
        }

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        const auto game = sgf::parse (buffer.str());

        check (game.valid, "real game record parses");
        check (game.size == 13, "13x13");
        check (game.moveCount() == 145, "145 moves, the two closing passes included");
        check (game.blackName == "FloMo" && game.whiteName == "Gruener123", "players read");
        check (game.result == "W+12.5", "result read");
        check (game.moves[0].index == go::index (3, 9, 13), "first move is B[dj]");
        check (game.moves[143].isPass && game.moves[144].isPass, "the game ends on two passes");

        //  replay the whole game through the rules engine
        go::Board board (13);
        int played = 0, refused = 0, passes = 0;

        for (const auto& move : game.moves)
        {
            if (move.isPass)
            {
                ++passes;
                continue;
            }

            if (board.play (move.index, move.colour, true, false) == MoveResult::ok)
                ++played;
            else
                ++refused;
        }

        check (passes == 2, "two passes seen");
        check (played == 143, "every played move was legal on the board");
        check (refused == 0, "no move had to be refused");
        check (board.capturedBlack() + board.capturedWhite() > 0, "stones were captured along the way");

        std::printf ("        %d stones left standing, %d black and %d white captured\n",
                     board.stoneCount(), board.capturedBlack(), board.capturedWhite());
    }

    //==============================================================================
    //  The two self-play players. What is checked here is not how well they play -
    //  that is a matter of taste, and the weights are there to be tuned - but the
    //  things the plugin relies on: the book opening is always there, every move is
    //  legal, the board does not dissolve, a seed names a game exactly, and swapping
    //  a finished game in never touches the heap.

    goai::Settings aiSettings (int size, int moves, int variation, std::uint32_t seed,
                               goai::Players players = goai::Players::classic)
    {
        goai::Settings s;
        s.size = size;
        s.moves = moves;
        s.variation = variation;
        s.seed = seed;
        s.players = players;
        return s;
    }

    /** Replays a generated record on a fresh board under the strict rules and
        says how many moves were refused. */
    int refusedMoves (const sgf::Game& game)
    {
        go::Board board (game.size);
        int refused = 0;

        for (const auto& move : game.moves)
            if (board.play (move.index, move.colour, false, true) != MoveResult::ok)
                ++refused;

        return refused;
    }

    void testAiOpening()
    {
        std::printf ("self-play: the book opening\n");

        const auto& book = goai::openingMoves (9);
        check (book.size() == 10, "ten book moves on a 9x9");
        check (goai::openingMoves (13).size() == 10, "ten on a 13x13");

        bool everyGameOpensOnIt = true, coloursAlternate = true;

        for (int seed = 1; seed <= 12; ++seed)
        {
            const auto game = goai::generate (aiSettings (9, 60, 60, (std::uint32_t) seed * 7919u));

            for (size_t i = 0; i < book.size(); ++i)
                if (game.moves[i].index != go::index (book[i].first, book[i].second, 9))
                    everyGameOpensOnIt = false;

            for (size_t i = 0; i < game.moves.size(); ++i)
                if (game.moves[i].colour != (i % 2 == 0 ? Stone::black : Stone::white))
                    coloursAlternate = false;
        }

        check (everyGameOpensOnIt, "twelve games, all ten book moves, whatever the seed");
        check (coloursAlternate, "black and white alternate throughout");

        //  and the book is the record it says it is: the first ten moves of
        //  sgf/nine_dan_9x9_43610191.sgf, which opens on tengen
        check (book[0] == std::make_pair (4, 4), "the book opens on tengen");
    }

    void testAiLegalityAndLength()
    {
        std::printf ("self-play: legality and length\n");

        bool rightLength = true, allLegal = true, noRepeats = true;

        for (int seed = 1; seed <= 8; ++seed)
        {
            const auto game = goai::generate (aiSettings (9, 60, 35, (std::uint32_t) seed * 104729u));

            if (game.moveCount() != 60)
                rightLength = false;

            if (refusedMoves (game) != 0)
                allLegal = false;

            //  a point may be played twice in one game - the first stone can be
            //  captured in between - but never twice running
            for (size_t i = 1; i < game.moves.size(); ++i)
                if (game.moves[i].index == game.moves[i - 1].index)
                    noRepeats = false;
        }

        check (rightLength, "eight games, sixty moves each");
        check (allLegal, "every move legal under no-suicide and ko");
        check (noRepeats, "no move lands on the point just played");

        const auto shortGame = goai::generate (aiSettings (9, 12, 35, 5u));
        check (shortGame.moveCount() == 12, "a twelve move game is twelve moves");

        const auto big = goai::generate (aiSettings (13, 80, 35, 11u));
        check (big.size == 13 && big.moveCount() == 80, "13x13, eighty moves");
        check (refusedMoves (big) == 0, "every 13x13 move legal too");
    }

    void testAiKeepsTheBoardAlive()
    {
        std::printf ("self-play: the board does not dissolve\n");

        //  Without the "never fill your own eye" rule both players take their own
        //  groups apart and the board empties. This is the check that would catch
        //  it: a sixty move game leaves most of its stones standing, and both
        //  colours are still on the board.
        int fewestStanding = 81, fewestBlack = 81, fewestWhite = 81;
        bool anythingCaptured = false;

        for (int seed = 1; seed <= 8; ++seed)
        {
            const auto game = goai::generate (aiSettings (9, 60, 50, (std::uint32_t) seed * 2654435761u));

            go::Board board (9);

            for (const auto& move : game.moves)
                board.play (move.index, move.colour, false, true);

            int black = 0, white = 0;

            for (int i = 0; i < board.cellCount(); ++i)
            {
                if (board.at (i) == Stone::black) ++black;
                else if (board.at (i) == Stone::white) ++white;
            }

            fewestStanding = std::min (fewestStanding, black + white);
            fewestBlack = std::min (fewestBlack, black);
            fewestWhite = std::min (fewestWhite, white);

            if (board.capturedBlack() + board.capturedWhite() > 0)
                anythingCaptured = true;
        }

        check (fewestStanding >= 30, "at least thirty stones still standing after sixty moves");
        check (fewestBlack >= 8 && fewestWhite >= 8, "neither colour is wiped out");
        check (anythingCaptured, "stones do get captured");
    }

    void testAiDeterminism()
    {
        std::printf ("self-play: a seed names a game\n");

        const auto a = goai::generate (aiSettings (9, 60, 50, 12345u));
        const auto b = goai::generate (aiSettings (9, 60, 50, 12345u));
        const auto c = goai::generate (aiSettings (9, 60, 50, 12346u));

        bool same = a.moveCount() == b.moveCount(), differs = false;

        for (int i = 0; i < a.moveCount(); ++i)
        {
            if (a.moves[(size_t) i].index != b.moves[(size_t) i].index) same = false;
            if (a.moves[(size_t) i].index != c.moves[(size_t) i].index) differs = true;
        }

        check (same, "the same seed plays the same game");
        check (differs, "a different seed plays a different one");

        //  variation 0 is the other end of the knob: the best point every time,
        //  so the seed stops mattering and a run is one game repeating
        const auto flat1 = goai::generate (aiSettings (9, 60, 0, 1u));
        const auto flat2 = goai::generate (aiSettings (9, 60, 0, 999u));

        bool identical = flat1.moveCount() == flat2.moveCount();

        for (int i = 0; i < flat1.moveCount() && identical; ++i)
            identical = flat1.moves[(size_t) i].index == flat2.moves[(size_t) i].index;

        check (identical, "variation 0 plays one game whatever the seed");

        //  and the divergence starts where it should: after the book, never inside it
        const auto x = goai::generate (aiSettings (9, 60, 80, 77u));
        const auto y = goai::generate (aiSettings (9, 60, 80, 78u));

        int firstDifference = x.moveCount();

        for (int i = 0; i < x.moveCount(); ++i)
            if (x.moves[(size_t) i].index != y.moves[(size_t) i].index) { firstDifference = i; break; }

        check (firstDifference >= 10, "two games never differ inside the opening");
    }

    void testAiGameSeeds()
    {
        std::printf ("self-play: the seeds of a run\n");

        std::array<std::uint32_t, 16> seeds {};
        bool allDifferent = true;

        for (int i = 0; i < 16; ++i)
        {
            seeds[(size_t) i] = goai::gameSeed (7u, i);

            for (int j = 0; j < i; ++j)
                if (seeds[(size_t) j] == seeds[(size_t) i])
                    allDifferent = false;
        }

        check (allDifferent, "sixteen games of a run, sixteen different seeds");
        check (goai::gameSeed (7u, 3) == goai::gameSeed (7u, 3), "and the run is repeatable");
        check (goai::gameSeed (7u, 3) != goai::gameSeed (8u, 3), "neighbouring runs do not overlap");
    }


    void testAiCustomOpening()
    {
        std::printf ("self-play: an opening of one's own\n");

        //  a corner joseki rather than the book's centre game, so the two are
        //  impossible to confuse on the board
        const int points[goai::openingLength]
        {
            ix (2, 2), ix (6, 6), ix (6, 2), ix (2, 6), ix (3, 6),
            ix (2, 5), ix (3, 5), ix (2, 4), ix (3, 4), ix (2, 3)
        };

        auto settings = aiSettings (9, 60, 40, 4242u);
        settings.hasOpening = true;

        for (int i = 0; i < goai::openingLength; ++i)
            settings.opening[(size_t) i] = points[i];

        bool followed = true, legal = true, lengths = true;

        for (int seed = 1; seed <= 8; ++seed)
        {
            settings.seed = (std::uint32_t) seed * 48271u;
            const auto game = goai::generate (settings);

            if (game.moveCount() != 60) lengths = false;
            if (refusedMoves (game) != 0) legal = false;

            for (int i = 0; i < goai::openingLength; ++i)
                if (game.moves[(size_t) i].index != points[i])
                    followed = false;
        }

        check (followed, "eight games, all ten of the given moves, whatever the seed");
        check (legal, "and the rest of each game is still legal");
        check (lengths, "and still sixty moves long");

        //  the players are unchanged by it: same opening, different middlegames
        settings.seed = 11u;
        const auto a = goai::generate (settings);
        settings.seed = 12u;
        const auto b = goai::generate (settings);

        int firstDifference = a.moveCount();

        for (int i = 0; i < a.moveCount(); ++i)
            if (a.moves[(size_t) i].index != b.moves[(size_t) i].index) { firstDifference = i; break; }

        check (firstDifference >= goai::openingLength, "two games still never differ inside it");
        check (firstDifference < a.moveCount(), "and they do differ after it");

        //  and it really is a different game from the book's
        auto book = aiSettings (9, 60, 40, 11u);
        const auto fromBook = goai::generate (book);
        check (fromBook.moves[0].index != a.moves[0].index, "a custom opening is not the book one");

        //  clearing the flag hands it back to the book, the same seed and all
        settings.seed = 11u;
        settings.hasOpening = false;
        const auto backToBook = goai::generate (settings);

        bool identical = backToBook.moveCount() == fromBook.moveCount();

        for (int i = 0; i < backToBook.moveCount() && identical; ++i)
            identical = backToBook.moves[(size_t) i].index == fromBook.moves[(size_t) i].index;

        check (identical, "and dropping it plays the book game exactly");
    }

    void testAiFullBoard()
    {
        std::printf ("self-play: 19x19\n");

        const auto& book = goai::openingMoves (19);
        check (book.size() == 10, "ten book moves on a 19x19");
        check (book[0] == std::make_pair (15, 3), "the book opens on the upper right star point");

        go::Board trial (19);
        bool bookLegal = true;

        for (size_t i = 0; i < book.size(); ++i)
            if (trial.play (ix19 (book[i].first, book[i].second),
                            i % 2 == 0 ? Stone::black : Stone::white, false, true) != MoveResult::ok)
                bookLegal = false;

        check (bookLegal, "the book line is legal from an empty board");

        bool followed = true, legal = true, lengths = true;

        for (int seed = 1; seed <= 4; ++seed)
        {
            const auto game = goai::generate (aiSettings (19, 160, 35, (std::uint32_t) seed * 65537u));

            if (game.size != 19 || game.moveCount() != 160) lengths = false;
            if (refusedMoves (game) != 0) legal = false;

            for (size_t i = 0; i < book.size(); ++i)
                if (game.moves[i].index != ix19 (book[i].first, book[i].second))
                    followed = false;
        }

        check (lengths, "four games, the longest length the plugin allows");
        check (legal, "every move legal under no-suicide and ko");
        check (followed, "and every one of them opens on the book");

        const auto a = goai::generate (aiSettings (19, 120, 50, 99u));
        const auto b = goai::generate (aiSettings (19, 120, 50, 99u));

        bool same = a.moveCount() == b.moveCount(), outsideSmallBoard = false;

        for (int i = 0; i < a.moveCount(); ++i)
        {
            const int idx = a.moves[(size_t) i].index;

            if (idx != b.moves[(size_t) i].index)
                same = false;

            if (i >= goai::openingLength && (go::colOf (idx, 19) >= 13 || go::rowOf (idx, 19) >= 13))
                outsideSmallBoard = true;
        }

        check (same, "a seed names a 19x19 game too");
        check (outsideSmallBoard, "and the players use the room a 13x13 does not have");
    }

    void testAiSwapIsAllocationFree()
    {
        std::printf ("self-play: swapping a game in\n");

        //  The audio thread swaps the waiting game onto the board. That is only
        //  safe because every member swap is a pointer exchange, so this checks
        //  that the buffers really do change hands rather than their contents
        //  being copied over.
        auto a = goai::generate (aiSettings (9, 40, 40, 3u));
        auto b = goai::generate (aiSettings (9, 60, 40, 4u));

        const auto* aData = a.moves.data();
        const auto* bData = b.moves.data();
        const int aCount = a.moveCount(), bCount = b.moveCount();
        const int aFirst = a.moves[0].index;

        goai::swapGames (a, b);

        check (a.moves.data() == bData && b.moves.data() == aData, "the move buffers changed hands");
        check (a.moveCount() == bCount && b.moveCount() == aCount, "and took their lengths with them");
        check (b.moves[0].index == aFirst, "the game that was playing is intact on the other side");
        check (b.blackRank == "territorial" && b.whiteRank == "fighting", "and so are the players' names");
    }

    //==============================================================================
    //  The classic players, pinned. A session saved with them plays its games
    //  again from the seed, so not one of their moves may ever change. These are
    //  hashes of fifteen long games a board, taken before the reading players
    //  were added beside them: a hash that moves is a saved session broken.

    std::uint32_t hashGames (int size, goai::Players players)
    {
        std::uint32_t h = 2166136261u;

        const auto mix = [&h] (int value)
        {
            for (int b = 0; b < 2; ++b)
            {
                h ^= (std::uint32_t) ((value >> (8 * b)) & 0xff);
                h *= 16777619u;
            }
        };

        for (int variation : { 0, 35, 100 })
        {
            for (int g = 0; g < 5; ++g)
            {
                const auto game = goai::generate (aiSettings (size, 160, variation, goai::gameSeed (7u, g), players));
                mix (game.moveCount());

                for (const auto& move : game.moves)
                    mix (move.index);
            }
        }

        return h;
    }

    void testAiClassicUnchanged()
    {
        std::printf ("self-play: the classic players have not moved\n");

        check (hashGames (9, goai::Players::classic)  == 0x8106a178u, "fifteen 9x9 games, move for move");
        check (hashGames (13, goai::Players::classic) == 0x16c32e81u, "fifteen 13x13 games, move for move");
        check (hashGames (19, goai::Players::classic) == 0x56a6ea8eu, "fifteen 19x19 games, move for move");

        //  The reading pair are pinned the same way. Unlike the classic pair they
        //  may be retuned - but a retune changes every game a session saved with
        //  them names, so it has to be a decision rather than an accident: these
        //  hashes change on purpose, or not at all.
        std::printf ("self-play: the reading players are pinned too\n");

        check (hashGames (9, goai::Players::reading)  == 0x3559b98du, "fifteen 9x9 games, move for move");
        check (hashGames (13, goai::Players::reading) == 0x9db553cbu, "fifteen 13x13 games, move for move");
        check (hashGames (19, goai::Players::reading) == 0xea6a617bu, "fifteen 19x19 games, move for move");
    }

    //==============================================================================
    //  What the reading players know about a board (GoTactics.h), on positions
    //  small enough to check by eye.

    /** Stones set down without rules, row by row from the top: X black, O white. */
    go::Board boardFrom (int size, std::initializer_list<const char*> rows)
    {
        go::Board board (size);
        int r = 0;

        for (const char* row : rows)
        {
            for (int c = 0; c < size && row[c] != 0; ++c)
            {
                if (row[c] == 'X') board.setStone (go::index (c, r, size), Stone::black);
                if (row[c] == 'O') board.setStone (go::index (c, r, size), Stone::white);
            }

            ++r;
        }

        return board;
    }

    void testTacticsMoveFacts()
    {
        std::printf ("tactics: what a move does, before it is played\n");

        tactics::Analysis analysis;
        tactics::Scratch scratch;
        tactics::Marks marks;

        //  a white stone on its last liberty at the top, a black one at the bottom
        const auto board = boardFrom (9, {
            "",
            "....X....",
            "...XOX...",
            "",
            "",
            "",
            "....O....",
            "...OXO...",
            "" });

        tactics::analyse (board, analysis, scratch);

        const auto capture = tactics::describeMove (board, analysis, ix (4, 3), Stone::black, marks);
        check (capture.legal && capture.captured == 1, "filling the last liberty lifts the stone");

        const auto rescue = tactics::describeMove (board, analysis, ix (4, 8), Stone::black, marks);
        check (rescue.legal && rescue.rescued == 1 && rescue.liberties == 2 && rescue.stones == 2,
               "extending brings a stone out of atari, as a chain of two on two liberties");

        const auto corner = boardFrom (9, { ".O", "O" });
        tactics::analyse (corner, analysis, scratch);
        check (! tactics::describeMove (corner, analysis, ix (0, 0), Stone::black, marks).legal,
               "a stone with no liberty that takes nothing is suicide");

        //  and through a real middlegame, what the chains say about every point
        //  and both colours is what the rules say when the move is played
        const auto game = goai::generate (aiSettings (13, 120, 60, 31337u));
        go::Board position (13);
        bool legality = true, captures = true, liberties = true;

        for (int i = 0; i < game.moveCount(); ++i)
        {
            position.play (game.moves[(size_t) i].index, game.moves[(size_t) i].colour, false, true);

            if (i % 20 != 19)
                continue;

            tactics::analyse (position, analysis, scratch);

            for (int idx = 0; idx < position.cellCount(); ++idx)
            {
                for (const auto colour : { Stone::black, Stone::white })
                {
                    auto trial = position;
                    const bool legal = trial.play (idx, colour, false, true) == MoveResult::ok;
                    const auto facts = tactics::describeMove (position, analysis, idx, colour, marks);

                    //  a ko is the one thing the chains leave to the board
                    if (legal != facts.legal && ! (facts.legal && facts.maybeKo))
                        legality = false;

                    if (legal && facts.captured != trial.lastCaptureCount())
                        captures = false;

                    if (legal && std::min (go::libertiesAt (trial.position(), idx, 13), tactics::keptLiberties) != facts.liberties)
                        liberties = false;
                }
            }
        }

        check (legality, "on a real game, the chains and the rules agree on every legal point");
        check (captures, "and on what each move captures");
        check (liberties, "and on the liberties it is left with");
    }

    void testTacticsLadders()
    {
        std::printf ("tactics: ladders\n");

        tactics::Scratch scratch;

        //  a white stone on two liberties in the shape a ladder starts from, the
        //  board open towards the lower left - and the same with two white
        //  stones sitting on the path it runs along. The white stone on the right
        //  edge closes the other way: without it the atari from below would run
        //  the stone into the near corner, a ladder no breaker down there stops.
        const auto open = boardFrom (9, { "", "......X..", ".....XO.O", ".......X." });
        const auto broken = boardFrom (9, { "", "......X..", ".....XO.O", ".......X.", "", "", "..O......", "..O......" });

        tactics::LadderBudget budget;
        check (tactics::chaserWins (open, ix (6, 2), budget, scratch), "a ladder with nothing in its way catches the stone");

        budget = {};
        check (! tactics::chaserWins (broken, ix (6, 2), budget, scratch), "a stone on its path breaks it");

        //  one move on: the atari is in, and it is White's turn to run
        const auto chased = boardFrom (9, { "", "......X..", ".....XOX.", ".......X." });
        const auto chasedBroken = boardFrom (9, { "", "......X..", ".....XOX.", ".......X.", "", "", "..O......", "..O......" });

        budget = {};
        check (tactics::runnerDies (chased, ix (6, 2), budget, scratch), "running does not save it");

        budget = {};
        check (! tactics::runnerDies (chasedBroken, ix (6, 2), budget, scratch), "unless the ladder is broken");
    }

    void testTacticsVitalPoints()
    {
        std::printf ("tactics: eye shapes\n");

        tactics::Scratch scratch;
        std::array<Stone, go::maxCells> vital {};

        //  a straight three along the bottom edge, walled in by black
        const auto three = boardFrom (9, { "", "", "", "", "", "", "", "XXXXX....", "X...X...." });
        tactics::findVitalPoints (three, scratch, vital);

        check (vital[(size_t) ix (2, 8)] == Stone::black, "the middle of a straight three is its vital point");
        check (vital[(size_t) ix (1, 8)] == Stone::none && vital[(size_t) ix (3, 8)] == Stone::none, "its ends are not");

        //  the open board is not an eye space of anyone's
        check (vital[(size_t) ix (4, 4)] == Stone::none, "nor is the open board");
    }

    void testReadingPlayersKeepTheContract()
    {
        std::printf ("self-play: the reading players keep the same promises\n");

        const auto reading = goai::Players::reading;
        const auto& book = goai::openingMoves (9);

        bool opens = true, rightLength = true, allLegal = true, noRepeats = true, anythingCaptured = false;
        int fewestStanding = 81, fewestBlack = 81, fewestWhite = 81;

        for (int seed = 1; seed <= 8; ++seed)
        {
            const auto game = goai::generate (aiSettings (9, 60, 50, (std::uint32_t) seed * 2654435761u, reading));

            rightLength = rightLength && game.moveCount() == 60;
            allLegal = allLegal && refusedMoves (game) == 0;

            for (size_t i = 0; i < book.size(); ++i)
                if (game.moves[i].index != go::index (book[i].first, book[i].second, 9))
                    opens = false;

            for (size_t i = 1; i < game.moves.size(); ++i)
                if (game.moves[i].index == game.moves[i - 1].index)
                    noRepeats = false;

            go::Board board (9);

            for (const auto& move : game.moves)
                board.play (move.index, move.colour, false, true);

            int black = 0, white = 0;

            for (int i = 0; i < board.cellCount(); ++i)
            {
                if (board.at (i) == Stone::black)      ++black;
                else if (board.at (i) == Stone::white) ++white;
            }

            fewestStanding = std::min (fewestStanding, black + white);
            fewestBlack = std::min (fewestBlack, black);
            fewestWhite = std::min (fewestWhite, white);
            anythingCaptured = anythingCaptured || board.capturedBlack() + board.capturedWhite() > 0;
        }

        check (opens, "eight games, all ten book moves");
        check (rightLength && allLegal && noRepeats, "sixty moves each, all legal, none on the point just played");
        check (fewestStanding >= 30 && fewestBlack >= 8 && fewestWhite >= 8, "the board does not dissolve");
        check (anythingCaptured, "stones do get captured");

        const auto big = goai::generate (aiSettings (13, 120, 35, 11u, reading));
        const auto full = goai::generate (aiSettings (19, 160, 35, 65537u, reading));
        check (big.moveCount() == 120 && refusedMoves (big) == 0, "13x13, 120 moves, all legal");
        check (full.moveCount() == 160 && refusedMoves (full) == 0, "19x19, 160 moves, all legal");

        const auto firstDifference = [] (const sgf::Game& x, const sgf::Game& y)
        {
            const int n = std::min (x.moveCount(), y.moveCount());

            for (int i = 0; i < n; ++i)
                if (x.moves[(size_t) i].index != y.moves[(size_t) i].index)
                    return i;

            return x.moveCount() == y.moveCount() ? -1 : n;
        };

        const auto a = goai::generate (aiSettings (9, 60, 50, 12345u, reading));
        const auto b = goai::generate (aiSettings (9, 60, 50, 12345u, reading));
        const auto c = goai::generate (aiSettings (9, 60, 50, 12346u, reading));
        const auto flat1 = goai::generate (aiSettings (9, 60, 0, 1u, reading));
        const auto flat2 = goai::generate (aiSettings (9, 60, 0, 999u, reading));
        const auto classic = goai::generate (aiSettings (9, 60, 50, 12345u));

        check (firstDifference (a, b) == -1, "the same seed plays the same game");
        check (firstDifference (a, c) >= goai::openingLength, "a different seed a different one, after the opening");
        check (firstDifference (flat1, flat2) == -1, "variation 0 plays one game whatever the seed");
        check (firstDifference (a, classic) >= goai::openingLength, "and not the game the classic players would");
        check (a.blackRank == "territorial" && a.whiteRank == "fighting", "Kuro is still territorial, Shiro still fighting");
    }

    void testReadingPlayersRead()
    {
        std::printf ("self-play: a reading player reads\n");

        goai::ReadingWorkspace ws;
        goai::Rng rng { 1 };
        const auto shiro = goai::readingFighting();
        const int run = ix (6, 3);

        //  White is in atari, and running leads down a ladder that works
        const auto chased = boardFrom (9, { "", "......X..", ".....XOX.", ".......X." });
        check (goai::chooseReadingMove (chased, Stone::white, ix (7, 2), shiro, 0, rng, ws) != run,
               "does not run out of atari into a ladder that works");

        //  with two stones of its own on the path, it runs
        const auto rescued = boardFrom (9, { "", "......X..", ".....XOX.", ".......X.", "", "", "..O......", "..O......" });
        check (goai::chooseReadingMove (rescued, Stone::white, ix (7, 2), shiro, 0, rng, ws) == run,
               "runs when its own stones break the ladder");

        //  White to play against a black stone on two liberties: the atari from
        //  the side that drives it down the ladder, not the one it runs away from
        //  (the black stone on the edge is where it would run to)
        const auto hunt = boardFrom (9, { "", "......O..", ".....OX.X", ".......O." });
        check (goai::chooseReadingMove (hunt, Stone::white, ix (6, 2), shiro, 0, rng, ws) == ix (7, 2),
               "starts the ladder that catches the stone");
    }
}

int main (int argc, char** argv)
{
    testSpiral();
    testSpiral13();
    testSpiral19();
    testRings();
    testQuads();
    testStarPoints();
    testLiberties();
    testSingleCapture();
    testGroupCapture();
    testCapture13();
    testCapture19();
    testSizeChangeClears();
    testTwoEyesLive();
    testSuicide();
    testCaptureBeatsSuicide();
    testKo();
    testOccupiedAndErase();
    testSerialisation();
    testSgfBasics();
    testSgfPassesAndSetup();
    testSgfFailures();

    testAiOpening();
    testAiLegalityAndLength();
    testAiKeepsTheBoardAlive();
    testAiDeterminism();
    testAiGameSeeds();
    testAiCustomOpening();
    testAiFullBoard();
    testAiSwapIsAllocationFree();
    testAiClassicUnchanged();

    testTacticsMoveFacts();
    testTacticsLadders();
    testTacticsVitalPoints();
    testReadingPlayersKeepTheContract();
    testReadingPlayersRead();

    if (argc > 1)
        testSgfFile (argv[1]);

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
