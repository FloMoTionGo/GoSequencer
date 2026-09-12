//  Standalone checks for the spiral order, the life-and-death rules and the SGF
//  reader. Builds without JUCE:
//      g++ -std=c++17 -I../Source GoRulesTests.cpp -o tests
//  An SGF path may be passed as argv[1] to check a real game record as well.

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

    /** The rings the polyrhythm mode plays: concentric, outermost first, with
        tengen left out. They are slices of the spiral, so this checks the two
        agree. */
    void testRings()
    {
        std::printf ("ring geometry\n");

        check (go::ringCount (9)  == 4, "9x9 has four rings once tengen is left out");
        check (go::ringCount (13) == 6, "13x13 has six rings once tengen is left out");
        check (go::maxRings == 6, "storage is sized for the biggest board");

        check (go::ringLength (9, 0) == 32 && go::ringLength (9, 1) == 24
                 && go::ringLength (9, 2) == 16 && go::ringLength (9, 3) == 8,
               "9x9 rings run 32, 24, 16, 8 - a 4:3:2:1 polyrhythm");

        for (int size : { 9, 13 })
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

        check (go::quadRadius (9) == 2 && go::quadSide (9) == 5 && go::quadSteps (9) == 25,
               "9x9 gives four 5x5 quadrants of 25 points");
        check (go::quadRadius (13) == 3 && go::quadSide (13) == 7 && go::quadSteps (13) == 49,
               "13x13 gives four 7x7 quadrants of 49 points");

        for (int size : { 9, 13 })
        {
            const std::string label = " (" + std::to_string (size) + "x" + std::to_string (size) + ")";
            const int r = go::quadRadius (size);

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

                //  the inward spiral finishes on the star point
                if (order[(size_t) (n - 1)] != wantCentre[q])
                    centresMatch = false;

                for (int i = 0; i < n; ++i)
                {
                    const int idx = order[(size_t) i];

                    if (idx < 0 || idx >= size * size) inRange = false;
                    else ++seen[(size_t) idx];
                }
            }

            check (inRange, "every quadrant point lands on the board" + label);
            check (centresMatch, "each spiral winds in to its own star point" + label);

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
            check (r == (size >= 13 ? 3 : 2), "the radius is the star point's own offset" + label);
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

    void testSizeChangeClears()
    {
        std::printf ("changing size\n");

        go::Board b;
        b.play (ix (4, 4), Stone::black, false, true);
        b.setSize (13);

        check (b.size() == 13, "size changed");
        check (b.stoneCount() == 0, "the board is wiped, the points mean something else now");
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
}

int main (int argc, char** argv)
{
    testSpiral();
    testSpiral13();
    testRings();
    testQuads();
    testStarPoints();
    testLiberties();
    testSingleCapture();
    testGroupCapture();
    testCapture13();
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

    if (argc > 1)
        testSgfFile (argv[1]);

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
