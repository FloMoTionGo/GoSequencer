#pragma once

//  Two Go players that write a game record - in two generations.
//
//  The classic pair are heuristic, not searching: every legal point is scored
//  by a handful of terms a beginner would recognise - take stones, save your
//  own, cut, connect, stay near the fight, keep off the first line - and one of
//  the best few is drawn at random. There is no reading, no playout and no net,
//  which is the point: a whole game is a few milliseconds of plain arithmetic -
//  about one on a 9x9, six on a 19x19, for the default length - so the plugin
//  can keep a record ready without ever leaving the machine.
//
//  The reading pair (Players::reading) are the same two people a few years of
//  play later. Before they score a point they look at the board as chains
//  (GoTactics.h): what a move takes or saves, which self-atari is a blunder and
//  which a throw-in, the vital point of a small eye space, which liberties to
//  fill in a race, whose area a point already lies in. Then the best two dozen
//  points, and every atari and rescue among the rest, are read out as ladders:
//  an atari that a ladder turns into a capture is worth the capture, and running
//  a chain out of atari into a ladder that works is worth less than nothing.
//
//  The list of things to look at is Leela's - Gian-Carlo Pascutto's Go engine,
//  https://github.com/gcp/Leela - and GoTactics.h sets out what was taken from
//  it and what was not: its knowledge of tactics, not its search and not its
//  neural networks. Neither pair is AI in the machine learning sense. Both are
//  algorithms - scoring rules, and for the reading pair a ladder reader - whose
//  weights are numbers written by hand, the reading pair's then settled by test
//  matches played offline between versions (tools/GoAiTune.cpp). The numbers
//  are fixed in this file: nothing is trained, nothing learns while the plugin
//  runs, and a game is still milliseconds of integer work.
//
//  What makes two players out of one function is the weights. Black and White
//  hold a different Style, so they want different points from the same board -
//  the way two people with different habits do - and the games that come out
//  have a shape rather than a spread. The reading pair keep those temperaments.
//
//  Every game opens on the same ten moves and diverges from the first move
//  after them. That is deliberate: the sequencer reads position as pitch, so a
//  fixed opening is a fixed motif, and what follows is the variation on it. The
//  ten are either the book line below or an opening of the player's own (see
//  Settings::opening) - which one makes no difference to how the two of them
//  play afterwards.
//
//  Determinism is part of the contract: a seed names a game exactly, on any
//  compiler and any machine, today and after a reload. That is why there is not
//  a single float below - scores are integers in hundredths of a point, the
//  random draw is xorshift32, and even the softmax runs off an integer table.
//  Floating point would have been easier to read and nearly always agree; nearly
//  is no use here, because two near-tied points falling the other way once is a
//  different game from there on, and a saved session would come back wrong. It
//  is also why the classic pair are not retired: a session saved with them
//  plays its games again from the seed, so they stay exactly as they were.
//
//  Like GoBoard.h and SgfParser.h this is plain C++: no JUCE, so it can be
//  tested without a host.

#include "GoBoard.h"
#include "GoTactics.h"
#include "SgfParser.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace goai
{
    //==============================================================================
    /** xorshift32: small, fast, and bit for bit the same everywhere, which is
        what makes a seed worth showing to the user. */
    struct Rng
    {
        explicit Rng (std::uint32_t seed) noexcept : state (seed != 0 ? seed : 0x9e3779b9u) {}

        std::uint32_t next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        /** A draw below limit, which must not be 0. The modulo bias is far below
            anything the shortlist could notice, and it is the same bias every
            time - which matters more here than the bias does. */
        std::uint32_t below (std::uint32_t limit) noexcept { return next() % limit; }

        std::uint32_t state;
    };

    /** 65536 * exp(-x), for x in 16.16 fixed point: the softmax weight of a move
        that scored x below the best one. A 2^-k table read with the exponent
        split into its whole and fractional parts, which is exact integer work
        where std::exp is a library's own business and differs between them. */
    inline std::uint32_t expNegFixed (std::uint32_t x) noexcept
    {
        //  exp(-x) is 2^-(x * log2 e); 94548 is log2(e) in 16.16
        const std::uint64_t y = ((std::uint64_t) x * 94548u) >> 16;
        const std::uint32_t whole = (std::uint32_t) (y >> 16);

        if (whole >= 24)
            return 0;                   // below one part in sixteen million: gone

        //  65536 * 2^(-k/16), linearly interpolated between the sixteenths
        static constexpr std::uint32_t halving[17]
        {
            65536, 62757, 60097, 57549, 55109, 52773, 50535, 48393, 46341,
            44376, 42495, 40693, 38968, 37316, 35734, 34219, 32768
        };

        const std::uint32_t frac = (std::uint32_t) (y & 0xffffu);
        const auto a = halving[frac >> 12];
        const auto b = halving[(frac >> 12) + 1];
        const auto interpolated = a - (std::uint32_t) (((std::uint64_t) (a - b) * (frac & 0xfff)) >> 12);

        return interpolated >> whole;
    }

    /** The seed for game n of a run. Mixed rather than added, so two runs one
        apart on the seed slider are not two runs of near identical games. */
    inline std::uint32_t gameSeed (std::uint32_t base, int gameNumber) noexcept
    {
        std::uint32_t x = base * 2654435761u + (std::uint32_t) gameNumber * 0x9e3779b9u + 0x85ebca6bu;
        x ^= x >> 16; x *= 0x7feb352du;
        x ^= x >> 15; x *= 0x846ca68bu;
        x ^= x >> 16;
        return x;
    }

    //==============================================================================
    /** What one player wants. Every weight is in hundredths of a point - so 1200
        is worth twelve times what 100 is - and their sum is what a move is worth.
        The differences between two Styles are what make Black and White read as
        two players rather than one. */
    struct Style
    {
        const char* name = "balanced";

        int capture   = 1400;   // per enemy stone lifted by this move
        int save      = 1000;   // per own stone pulled back out of atari
        int atari     =  500;   // per enemy stone left on one liberty
        int connect   =  250;   // per own chain joined beyond the first
        int cut       =  300;   // per enemy chain split beyond the first
        int contact   =  120;   // per enemy stone this one touches
        int locality  =  500;   // for staying near the last move, falling off with distance
        int extension =  350;   // for a comfortable distance from its own stones
        int line      =  200;   // for the third and fourth lines, against the first
        int selfAtari = 1600;   // subtracted: one liberty left and nothing taken for it
    };

    /** Black: holds its stones together, takes the calm extension, and fights
        only when there is something to take. */
    inline Style territorial()
    {
        Style s;
        s.name      = "territorial";
        s.capture   = 1200;
        s.save      = 1100;
        s.atari     =  350;
        s.connect   =  350;
        s.cut       =  150;
        s.contact   =   40;
        s.locality  =  300;
        s.extension =  500;
        s.line      =  300;
        return s;
    }

    /** White: goes to the stones. Contact, cuts and ataris are worth more to it
        than shape is, so it answers Black's calm play by starting something. */
    inline Style fighting()
    {
        Style s;
        s.name      = "fighting";
        s.capture   = 1600;
        s.save      =  900;
        s.atari     =  700;
        s.connect   =  150;
        s.cut       =  500;
        s.contact   =  250;
        s.locality  =  700;
        s.extension =  150;
        s.line      =  120;
        return s;
    }

    //==============================================================================
    /** Which pair of players writes the record. It is part of what names a game:
        the two pairs play different games from the same seed, so a saved session
        has to remember which pair it was. */
    enum class Players : int
    {
        classic = 0,    // one look at the board: the first Kuro and Shiro
        reading = 1     // tactics and ladders read before they look
    };

    /** What a reading player wants: hundredths of a point, as in Style, over a
        longer list - because it can see more. */
    struct ReadingStyle
    {
        const char* name = "balanced";

        //  tactics
        int capture       = 1000;   // per enemy stone lifted
        int captureBonus  =  400;   // once, for lifting anything at all
        int save          =  900;   // per own stone brought out of atari
        int saveBonus     =  600;   // once, for saving anything at all
        int ladder        =  800;   // per enemy stone an atari catches in a ladder
        int atari         =  150;   // per enemy stone put in atari that can still run
        int press         =   80;   // per enemy stone left on two liberties
        int reinforce     =  200;   // per own stone taken from two liberties to three or more
        int selfAtari     = 1400;   // subtracted, per stone (up to six) left on one liberty for nothing
        int runIntoLadder =  600;   // subtracted, per stone run out of atari into a ladder that works
        int ladderable    =  400;   // subtracted, per stone left on two liberties in a ladder that works
        int vital         =  900;   // for the vital point of a small eye space, either side's
        int race          =  500;   // per step of urgency in a liberty race
        int eye           =  350;   // per eye made for itself, or taken from the other side

        //  shape
        int connect       =  300;   // per own chain joined beyond the first
        int cut           =  300;   // per enemy chain touched beyond the first
        int contact       =  100;   // per enemy stone touched
        int emptyTriangle =  200;   // subtracted, per empty triangle made

        //  place
        int locality      =  400;   // for staying near the last move, as in Style
        int extension     =  350;   // for a comfortable distance from its own stones, as in Style
        int line          =  250;   // for the third and fourth lines, as in Style
        int territory     =  400;   // subtracted: a quiet move inside its own secure area
        int invasion      =  400;   // subtracted: a quiet move inside the other side's
        int claim         =   40;   // per point within two steps that is nobody's yet
    };

    //  The two below started as a sketch and were settled by tools/GoAiTune.cpp:
    //  thousands of games played to the end, against the classic pair and
    //  against the sketch, changing one weight at a time by the same factor for
    //  both players - so each kept its temperament - and keeping a change only
    //  when it won by a clear margin. That was done once, offline; the numbers
    //  are fixed here, and nothing adjusts them while the plugin runs.

    /** Kuro, reading: still the calm one. Getting its own stones out of trouble
        matters most to it, then the third line and not wasting moves inside
        what is already its own; it will not throw stones into the other side's
        area without a tactical reason. */
    inline ReadingStyle readingTerritorial()
    {
        ReadingStyle s;
        s.name          = "territorial";
        s.capture       =  600;
        s.captureBonus  =  240;
        s.save          = 1500;
        s.saveBonus     = 1050;
        s.ladder        =  480;
        s.atari         =  150;
        s.press         =   75;
        s.reinforce     =  300;
        s.selfAtari     = 2250;
        s.runIntoLadder =  700;
        s.ladderable    =  500;
        s.vital         =  900;
        s.race          =  270;
        s.eye           =  350;
        s.connect       =  400;
        s.cut           =  150;
        s.contact       =   40;
        s.emptyTriangle =  250;
        s.locality      =  250;
        s.extension     =  300;
        s.line          =  563;
        s.territory     =  750;
        s.invasion      =  600;
        s.claim         =   50;
        return s;
    }

    /** Shiro, reading: still goes to the stones - ataris, cuts, contact and
        staying by the last move pay more - but now it knows which of its
        attacks will work. */
    inline ReadingStyle readingFighting()
    {
        ReadingStyle s;
        s.name          = "fighting";
        s.capture       =  720;
        s.captureBonus  =  300;
        s.save          = 1275;
        s.saveBonus     =  750;
        s.ladder        =  540;
        s.atari         =  375;
        s.press         =  175;
        s.reinforce     =  150;
        s.selfAtari     = 1950;
        s.runIntoLadder =  600;
        s.ladderable    =  350;
        s.vital         =  900;
        s.race          =  360;
        s.eye           =  350;
        s.connect       =  180;
        s.cut           =  450;
        s.contact       =  180;
        s.emptyTriangle =  150;
        s.locality      =  600;
        s.extension     =   90;
        s.line          =  281;
        s.territory     =  600;
        s.invasion      =  200;
        s.claim         =   30;
        return s;
    }

    //==============================================================================
    /** The book both players open from unless they are given an opening of their
        own, as (column, row) pairs, Black first. On the two small boards these
        are not invented: they are the first ten moves of two of the records in
        sgf/ - nine_dan_9x9_43610191.sgf on a 9x9, Blackie_BIBA_13x13_25655059.sgf
        on a 13x13 - so the motif every game starts from is real play rather than
        a pattern that merely looks like it.

        There is no 19x19 record there to take one from, so the full board opens
        on a textbook line instead: the four star points, then the commonest star
        point joseki - low approach, small knight's move, two-space extension -
        played out in the upper left corner and again in the lower right.

        An opening point that is somehow not legal is skipped and the players take
        over early. On an empty board a book line cannot do that, and a custom
        opening is checked before it is ever set; the generator simply does not
        depend on either. */
    inline const std::vector<std::pair<int, int>>& openingMoves (int size)
    {
        static const std::vector<std::pair<int, int>> book9
        {
            { 4, 4 }, { 4, 6 }, { 4, 2 }, { 6, 5 }, { 6, 4 },
            { 7, 4 }, { 5, 3 }, { 7, 3 }, { 5, 5 }, { 5, 6 }
        };

        static const std::vector<std::pair<int, int>> book13
        {
            { 9, 3 }, { 3, 9 }, { 9, 10 }, { 3, 3 }, { 2, 10 },
            { 2, 9 }, { 3, 10 }, { 4, 9 }, { 4, 10 }, { 5, 10 }
        };

        //  Q16 D4 Q4 D16, then F17 C14 J17 in the upper left and R6 O3 R9 in
        //  the lower right
        static const std::vector<std::pair<int, int>> book19
        {
            { 15, 3 }, { 3, 15 }, { 15, 15 }, { 3, 3 }, { 5, 2 },
            { 2, 5 }, { 8, 2 }, { 16, 13 }, { 13, 16 }, { 16, 10 }
        };

        return size == 19 ? book19 : (size == 13 ? book13 : book9);
    }

    /** How long an opening is, book or custom: ten moves, black first. */
    constexpr int openingLength = 10;

    //==============================================================================
    struct Settings
    {
        int size = 9;
        int moves = 60;                 // the whole game, the opening included

        /** An opening of one's own, as board indices in the order they were
            played. The colours are not stored because they are not free: an
            opening alternates, black first, the way a game does.

            Left empty, the two of them open on the book above. Set, they open
            on this instead - and nothing else about them changes, which is the
            point: the same players, a different motif. */
        std::array<int, (size_t) openingLength> opening {};
        bool hasOpening = false;

        /** Per cent. 0 always plays the best point it can see, 100 picks freely
            among the best few. It is the one knob between "the same game every
            time" and "a different middlegame every time". */
        int variation = 35;

        std::uint32_t seed = 1;

        /** Which pair plays. Classic unless asked for: every game named before
            the reading pair existed was named with the classic one. */
        Players players = Players::classic;

        Style black = territorial();
        Style white = fighting();

        ReadingStyle readingBlack = readingTerritorial();
        ReadingStyle readingWhite = readingFighting();
    };

    /** The point the opening asks for at this move, or -1 once it is over. */
    inline int openingPoint (const Settings& settings, int move) noexcept
    {
        if (move < 0 || move >= openingLength)
            return -1;

        if (settings.hasOpening)
            return settings.opening[(size_t) move];

        const auto& book = openingMoves (settings.size);
        const auto point = book[(size_t) move];
        return go::index (point.first, point.second, settings.size);
    }

    //==============================================================================
    namespace detail
    {
        /** How far a point is from the nearest edge: 0 on the first line. */
        inline int edgeDistance (int col, int row, int size) noexcept
        {
            return std::min (std::min (col, row), std::min (size - 1 - col, size - 1 - row));
        }

        //  The three place terms are shapes rather than amounts: each returns a
        //  percentage, which its Style weight is then paid at. So a player that
        //  cares about the lines twice as much moves the same way, harder.

        inline int lineValue (int e) noexcept
        {
            switch (e)
            {
                case 0:  return -120;       // the first line gives the point away
                case 1:  return   10;
                case 2:  return  100;       // third line
                case 3:  return   85;       // fourth
                default: return   50;
            }
        }

        /** A point this far from its nearest friend is an extension; nearer is
            heavy, further is loose and gets cut. */
        inline int extensionValue (int d) noexcept
        {
            switch (d)
            {
                case 1:  return  25;
                case 2:  return 100;
                case 3:  return  80;
                case 4:  return  35;
                default: return   5;        // also the "no friendly stone yet" case
            }
        }

        /** How much of the locality weight is still paid this far from the last
            move: exp(-0.55 d), as a percentage, tabulated. */
        inline int localityValue (int d) noexcept
        {
            static constexpr int falloff[13] { 100, 58, 33, 19, 11, 6, 4, 2, 1, 1, 0, 0, 0 };
            return falloff[(size_t) std::min (d, 12)];
        }

        /** The one rule a heuristic player has to be told outright, because
            every term above would happily break it: do not fill your own eye.
            Without it a self-play game dissolves - both sides take their own
            groups apart in the endgame and the board empties.

            A point counts as an eye when all four orthogonal neighbours are
            friendly and the diagonals are too, with one allowed to be missing
            away from the edge. */
        inline bool isOwnEye (const go::Board& board, int idx, go::Stone colour)
        {
            const int size = board.size();

            bool surrounded = true;

            go::forEachNeighbour (idx, size, [&] (int n)
            {
                if (board.at (n) != colour)
                    surrounded = false;
            });

            if (! surrounded)
                return false;

            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);
            int diagonals = 0, friendly = 0;

            for (int dc = -1; dc <= 1; dc += 2)
            {
                for (int dr = -1; dr <= 1; dr += 2)
                {
                    const int c = col + dc, r = row + dr;

                    if (c < 0 || c >= size || r < 0 || r >= size)
                        continue;

                    ++diagonals;

                    if (board.at (go::index (c, r, size)) == colour)
                        ++friendly;
                }
            }

            const int allowedMisses = (diagonals == 4 ? 1 : 0);
            return friendly >= diagonals - allowedMisses;
        }

        /** Chebyshev distance to the nearest stone of this colour, or the board
            size when there is none on it yet. */
        inline int distanceToNearest (const go::Board& board, int idx, go::Stone colour)
        {
            const int size = board.size();
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);
            int best = size;

            for (int i = 0, n = board.cellCount(); i < n; ++i)
            {
                if (board.at (i) != colour)
                    continue;

                const int d = std::max (std::abs (go::colOf (i, size) - col),
                                        std::abs (go::rowOf (i, size) - row));
                best = std::min (best, d);
            }

            return best;
        }

        /** What one legal move is worth to a player of this Style, in hundredths
            of a point. `before` is the board as it stands, `after` the same board
            with the move already played and its captures already taken. */
        inline int scoreMove (const go::Board& before, const go::Board& after, int idx,
                              go::Stone colour, int lastMove, const Style& style)
        {
            const int size = before.size();
            const go::Stone enemy = go::other (colour);

            int score = 0;

            //  ---- what the move took ---------------------------------------
            const int captured = after.lastCaptureCount();
            score += style.capture * captured;

            //  ---- the neighbours it arrives among ---------------------------
            //  Each chain is looked at once: scanning one stone of it marks the
            //  whole chain, so a move touching two stones of one group does not
            //  read as two groups.
            std::array<bool, go::maxCells> seen {};
            go::GroupScan scan;

            int ownChains = 0, enemyChains = 0, rescued = 0, touching = 0;

            go::forEachNeighbour (idx, size, [&] (int n)
            {
                const auto s = before.at (n);

                if (s == go::Stone::none)
                    return;

                if (s == enemy)
                    ++touching;

                if (seen[(size_t) n])
                    return;

                go::scanGroup (before.position(), n, size, scan);

                for (int i = 0; i < scan.count; ++i)
                    seen[(size_t) scan.stones[(size_t) i]] = true;

                if (s == colour)
                {
                    ++ownChains;

                    if (scan.liberties == 1)
                        rescued += scan.count;      // in atari before this move
                }
                else
                {
                    ++enemyChains;
                }
            });

            if (ownChains > 1)   score += style.connect * (ownChains - 1);
            if (enemyChains > 1) score += style.cut     * (enemyChains - 1);

            score += style.contact * touching;

            //  ---- the position it leaves behind -----------------------------
            go::scanGroup (after.position(), idx, size, scan);

            if (scan.liberties >= 2)
            {
                score += style.save * rescued;
            }
            else if (captured == 0)
            {
                //  one liberty and nothing to show for it: the chain is a gift.
                //  Scaled by what is being given away, so a lone stone thrown in
                //  is a smaller mistake than a whole group left hanging.
                score -= style.selfAtari * std::min (scan.count, 6);
            }

            //  enemy chains this move has left on their last liberty
            std::array<bool, go::maxCells> seenAfter {};
            int atariStones = 0;

            go::forEachNeighbour (idx, size, [&] (int n)
            {
                if (after.at (n) != enemy || seenAfter[(size_t) n])
                    return;

                go::scanGroup (after.position(), n, size, scan);

                for (int i = 0; i < scan.count; ++i)
                    seenAfter[(size_t) scan.stones[(size_t) i]] = true;

                if (scan.liberties == 1)
                    atariStones += scan.count;
            });

            score += style.atari * atariStones;

            //  ---- where it is -----------------------------------------------
            //  these three are paid at a percentage, so they divide back down
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);

            score += style.line * lineValue (edgeDistance (col, row, size)) / 100;
            score += style.extension * extensionValue (distanceToNearest (before, idx, colour)) / 100;

            if (lastMove >= 0)
            {
                const int d = std::max (std::abs (go::colOf (lastMove, size) - col),
                                        std::abs (go::rowOf (lastMove, size) - row));
                score += style.locality * localityValue (d) / 100;
            }

            return score;
        }

        struct Candidate
        {
            int idx = -1;
            int score = 0;
        };

        /** The order points are ranked in. The index breaks ties, so the order is
            total and one seed cannot land on two different moves. */
        inline bool better (const Candidate& a, const Candidate& b) noexcept
        {
            return a.score != b.score ? a.score > b.score : a.idx < b.idx;
        }

        /** How many of the best points are worth drawing between. Keeping this
            short is what stops a high variation setting from turning the players
            into noise: the choice widens, but only over moves that scored. */
        constexpr int shortlist = 12;

        /** The draw both pairs of players make once every point is scored: the
            best point at variation 0, otherwise a softmax over the shortlist. */
        inline int drawFromScored (std::array<Candidate, go::maxCells>& scored, int count,
                                   int variation, Rng& rng)
        {
            if (count == 0)
                return -1;

            const int top = std::min (count, shortlist);

            std::partial_sort (scored.begin(), scored.begin() + top, scored.begin() + count, better);

            //  variation 0 means what it says: the best point, every time, whatever
            //  the seed - so the whole run is one game repeating, which is a setting
            //  someone wanting a strict loop will ask for
            if (variation <= 0)
                return scored[0].idx;

            //  Otherwise a softmax over the shortlist, with the temperature read off
            //  the spread of the shortlist rather than fixed. Score units drift with
            //  the weights and with how crowded the board is; the distance between
            //  the best point and the twelfth does not. So variation keeps meaning
            //  the same thing - a little of it only unsettles near ties, all of it
            //  makes the shortlist close to a free choice - at every point in every
            //  game.
            const int spread = scored[0].score - scored[(size_t) (top - 1)].score;
            const auto tau = (std::uint32_t) std::max (1, std::min (variation, 100) * (50 + spread) / 100);

            std::array<std::uint32_t, (size_t) shortlist> weights {};
            std::uint32_t total = 0;

            for (int i = 0; i < top; ++i)
            {
                const auto behind = (std::uint32_t) (scored[0].score - scored[(size_t) i].score);
                weights[(size_t) i] = expNegFixed ((std::uint32_t) (((std::uint64_t) behind << 16) / tau));
                total += weights[(size_t) i];
            }

            if (total == 0)
                return scored[0].idx;       // everything but the best point rounded away

            auto pick = rng.below (total);

            for (int i = 0; i < top; ++i)
            {
                if (pick < weights[(size_t) i])
                    return scored[(size_t) i].idx;

                pick -= weights[(size_t) i];
            }

            return scored[0].idx;
        }
    }

    //==============================================================================
    /** The move this player would make, or -1 when it has nothing legal left
        that is not one of its own eyes. The board is not changed. */
    inline int chooseMove (const go::Board& board, go::Stone colour, int lastMove,
                           const Style& style, int variation, Rng& rng)
    {
        std::array<detail::Candidate, go::maxCells> scored {};
        int count = 0;

        for (int idx = 0, cells = board.cellCount(); idx < cells; ++idx)
        {
            if (board.at (idx) != go::Stone::none)
                continue;

            if (detail::isOwnEye (board, idx, colour))
                continue;

            //  the rules are the arbiter of legality here as everywhere else:
            //  no suicide, and ko applies, so a generated record is legal under
            //  the strictest reading of the switches the board offers
            go::Board trial = board;

            if (trial.play (idx, colour, false, true) != go::MoveResult::ok)
                continue;

            scored[(size_t) count++] = { idx, detail::scoreMove (board, trial, idx, colour, lastMove, style) };
        }

        return detail::drawFromScored (scored, count, variation, rng);
    }

    //==============================================================================
    /** Everything a reading player works in, set up once for a whole game rather
        than on every move. Around 30 KB, most of it the chain table. */
    struct ReadingWorkspace
    {
        tactics::Analysis analysis;
        tactics::Scratch scratch;
        tactics::Marks marks;

        std::array<int, go::maxCells> area {}, moyo {}, work {};
        std::array<std::int8_t, go::maxCells> distance {}, race {};
        std::array<go::Stone, go::maxCells> vital {};
        std::array<bool, go::maxCells> tactical {};
        std::array<std::int16_t, go::maxCells> slot {};
        std::array<detail::Candidate, go::maxCells> scored {};
    };

    namespace detail
    {
        /** How many of the best points get read out, over and above every atari
            and rescue on the board. */
        constexpr int readingShortlist = 24;

        /** Positions a ladder read may visit, and all the reads of one move
            together. Both are counts, so a read that runs out runs out on the
            same position everywhere. */
        constexpr int positionsPerRead = 160;
        constexpr int positionsPerMove = 2400;

        /** Empty triangles a move would make: three own stones bent round the
            corner of a two by two square whose fourth point is empty - the shape
            that spends a stone and gains neither a liberty nor any reach. */
        inline int emptyTriangles (const go::Board& board, int idx, go::Stone colour)
        {
            const int size = board.size();
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);
            int triangles = 0;

            for (int dc = -1; dc <= 1; dc += 2)
            {
                for (int dr = -1; dr <= 1; dr += 2)
                {
                    const int c = col + dc, r = row + dr;

                    if (c < 0 || c >= size || r < 0 || r >= size)
                        continue;

                    const go::Stone corner[3] { board.at (go::index (c, row, size)),
                                                board.at (go::index (col, r, size)),
                                                board.at (go::index (c, r, size)) };
                    int own = 0, empty = 0;

                    for (const auto s : corner)
                    {
                        if (s == colour)                ++own;
                        else if (s == go::Stone::none)  ++empty;
                    }

                    if (own == 2 && empty == 1)
                        ++triangles;
                }
            }

            return triangles;
        }

        /** What a stone here would stake out: the empty points within two steps
            that are nobody's secure area and not already leaning this player's
            way. `sign` is +1 for Black, -1 for White, as the maps count. */
        inline int claimedPoints (const go::Board& board, const ReadingWorkspace& ws, int idx, int sign)
        {
            const int size = board.size();
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);
            int claimed = 0;

            for (int dr = -2; dr <= 2; ++dr)
            {
                for (int dc = -2; dc <= 2; ++dc)
                {
                    if (std::abs (dr) + std::abs (dc) > 2)
                        continue;

                    const int c = col + dc, r = row + dr;

                    if (c < 0 || c >= size || r < 0 || r >= size)
                        continue;

                    const int q = go::index (c, r, size);

                    if (board.at (q) == go::Stone::none && ws.area[(size_t) q] == 0
                         && sign * ws.moyo[(size_t) q] <= 0)
                        ++claimed;
                }
            }

            return claimed;
        }

        /** The first look at a move: everything the chains and maps say about it,
            without reading anything out. */
        inline int readingScore (const go::Board& board, ReadingWorkspace& ws, int idx,
                                 go::Stone colour, int lastMove, const ReadingStyle& style,
                                 const tactics::MoveFacts& f)
        {
            const int size = board.size();
            const int sign = (colour == go::Stone::black ? 1 : -1);
            const bool vitalPoint = (ws.vital[(size_t) idx] != go::Stone::none);
            const int race = ws.race[(size_t) idx];

            //  the same point from the other side: what it would do for them.
            //  Most of what a point is worth depends on whether they want it too.
            const auto theirs = tactics::describeMove (board, ws.analysis, idx, go::other (colour), ws.marks);

            int score = 0;

            //  ---- tactics ---------------------------------------------------
            //  Taking stones that could run is urgent; taking stones that could
            //  not get out even if they tried is a move that can wait - they are
            //  already dead, and lifting them now gains a tempo for the other side.
            if (f.captured > 0)
            {
                const bool couldRun = theirs.legal && theirs.liberties >= 2;
                const int value = style.captureBonus + style.capture * std::min (f.captured, 12);
                score += couldRun ? value : value / 3;
            }

            if (f.rescued > 0)
                score += style.saveBonus + style.save * std::min (f.rescued, 12);

            score += style.atari     * std::min (f.threatened, 12);
            score += style.press     * std::min (f.pressed, 8);
            score += style.reinforce * std::min (f.reinforced, 8);
            score += style.race      * race;

            if (vitalPoint)
                score += style.vital;

            if (f.liberties == 1 && f.captured == 0 && ! vitalPoint)
            {
                int penalty = style.selfAtari * std::min (f.stones, 6);

                if (f.stones >= 6)
                    penalty += 3 * style.selfAtari;     // a whole group handed over
                else if (f.weakEnemy > 0)
                    penalty /= 3;                        // a throw-in against a chain short of liberties itself

                score -= penalty;
            }

            //  ---- shape -----------------------------------------------------
            //  A connection is only worth a move where the other side could
            //  really cut: a cutting stone left on one liberty is no cut.
            if (f.ownChains > 1)
            {
                const bool realCut = theirs.legal && theirs.liberties >= 2;
                const int value = style.connect * (f.ownChains - 1);
                score += realCut ? value : value / 4;
            }

            if (f.enemyChains > 1) score += style.cut * (f.enemyChains - 1);

            score += style.contact * f.touching;
            score -= style.emptyTriangle * emptyTriangles (board, idx, colour);

            int eyesMade = 0, eyesSpoiled = 0;
            tactics::eyeEffects (board, idx, colour, eyesMade, eyesSpoiled);
            score += style.eye * (eyesMade + eyesSpoiled);

            //  ---- where it is -----------------------------------------------
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);

            score += style.line * lineValue (edgeDistance (col, row, size)) / 100;
            score += style.extension * extensionValue (ws.distance[(size_t) idx]) / 100;

            if (lastMove >= 0)
            {
                const int d = std::max (std::abs (go::colOf (lastMove, size) - col),
                                        std::abs (go::rowOf (lastMove, size) - row));
                score += style.locality * localityValue (d) / 100;
            }

            //  a quiet move inside an area that is already decided does nothing
            //  for its own side, and inside the other side's it just dies there
            const bool urgent = f.captured > 0 || f.rescued > 0 || f.threatened > 0 || vitalPoint || race > 0;

            if (! urgent)
            {
                const int leaning = sign * ws.area[(size_t) idx];

                if (leaning > 0)      score -= style.territory;
                else if (leaning < 0) score -= style.invasion;
            }

            score += style.claim * claimedPoints (board, ws, idx, sign);

            return score;
        }

        /** The second look, for the few moves worth it: what the ladders say.
            `trial` is the board with the move already played. */
        inline int readingAdjustment (const go::Board& trial, ReadingWorkspace& ws, int idx,
                                      go::Stone colour, const ReadingStyle& style,
                                      const tactics::MoveFacts& f, tactics::LadderBudget& budget)
        {
            const go::Stone enemy = go::other (colour);
            int delta = 0;

            //  an atari that a ladder turns into a capture is worth the capture -
            //  as long as the stone giving it is not itself left on one liberty
            if (f.threatened > 0 && f.liberties >= 2)
            {
                for (int i = 0; i < f.atariCount; ++i)
                {
                    const int stone = f.atari[(size_t) i];

                    if (trial.at (stone) != enemy)
                        continue;

                    if (tactics::runnerDies (trial, stone, budget, ws.scratch))
                    {
                        const int stones = ws.analysis.chain (ws.analysis.chainAt[(size_t) stone]).size;
                        delta += (style.ladder - style.atari) * std::min (stones, 12);
                    }
                }
            }

            //  a chain left on two liberties that a ladder takes: if the move was
            //  a rescue, the rescue was worth nothing and the stones added to it
            //  are lost as well
            if (f.liberties == 2 && f.captured == 0 && (f.rescued > 0 || f.stones >= 2 || f.touching > 0)
                 && tactics::chaserWins (trial, idx, budget, ws.scratch))
            {
                if (f.rescued > 0)
                    delta -= style.saveBonus + style.save * std::min (f.rescued, 12)
                           + style.runIntoLadder * std::min (f.stones, 12);
                else
                    delta -= style.ladderable * std::min (f.stones, 12);
            }

            return delta;
        }
    }

    /** The move a reading player would make, or -1 when it has nothing legal
        left that is not one of its own eyes. The board is not changed; the
        workspace is scratch and can be handed from move to move. */
    inline int chooseReadingMove (const go::Board& board, go::Stone colour, int lastMove,
                                  const ReadingStyle& style, int variation, Rng& rng,
                                  ReadingWorkspace& ws)
    {
        const int size = board.size(), cells = board.cellCount();

        //  ---- once a move: the board as chains, areas, eye spaces and races ----
        tactics::analyse (board, ws.analysis, ws.scratch);
        tactics::influence (board, 5, 10, ws.moyo, ws.work);
        std::copy (ws.moyo.begin(), ws.moyo.begin() + cells, ws.area.begin());
        tactics::erode (size, 11, ws.area, ws.work);
        tactics::findVitalPoints (board, ws.scratch, ws.vital);
        tactics::findRaceMoves (ws.analysis, colour, ws.race);
        tactics::distanceTo (board, colour, ws.scratch, ws.distance);

        //  ---- every point, from the chains alone --------------------------------
        int count = 0;

        for (int idx = 0; idx < cells; ++idx)
        {
            ws.tactical[(size_t) idx] = false;

            if (board.at (idx) != go::Stone::none || detail::isOwnEye (board, idx, colour))
                continue;

            const auto facts = tactics::describeMove (board, ws.analysis, idx, colour, ws.marks);

            if (! facts.legal)
                continue;

            //  a single stone lifted might be the ko: the board's rules decide
            if (facts.maybeKo)
            {
                go::Board trial = board;

                if (trial.play (idx, colour, false, true) != go::MoveResult::ok)
                    continue;
            }

            ws.tactical[(size_t) idx] = (facts.rescued > 0 || facts.threatened > 0);
            ws.scored[(size_t) count++] = { idx, detail::readingScore (board, ws, idx, colour, lastMove, style, facts) };
        }

        if (count == 0)
            return -1;

        //  ---- the best of them, and every atari and rescue, read out ------------
        //  The best are taken in rank order and the rest in board order, never in
        //  whatever order a sort happened to leave them: the reading budget can
        //  run out part way, and where it runs out has to be the same everywhere.
        const int read = std::min (count, detail::readingShortlist);
        std::partial_sort (ws.scored.begin(), ws.scored.begin() + read, ws.scored.begin() + count, detail::better);

        int positions = 0;

        const auto readOut = [&] (detail::Candidate& candidate)
        {
            if (positions >= detail::positionsPerMove)
                return;

            const auto facts = tactics::describeMove (board, ws.analysis, candidate.idx, colour, ws.marks);

            if (facts.threatened == 0 && facts.liberties != 2)
                return;         //  nothing a ladder could change

            go::Board trial = board;
            trial.play (candidate.idx, colour, false, true);

            tactics::LadderBudget budget;
            budget.limit = std::min (detail::positionsPerRead, detail::positionsPerMove - positions);

            candidate.score += detail::readingAdjustment (trial, ws, candidate.idx, colour, style, facts, budget);
            positions += std::min (budget.positions, budget.limit);
        };

        for (int i = 0; i < read; ++i)
        {
            ws.tactical[(size_t) ws.scored[(size_t) i].idx] = false;
            readOut (ws.scored[(size_t) i]);
        }

        for (int i = read; i < count; ++i)
            ws.slot[(size_t) ws.scored[(size_t) i].idx] = (std::int16_t) i;

        for (int idx = 0; idx < cells; ++idx)
            if (ws.tactical[(size_t) idx])
                readOut (ws.scored[(size_t) ws.slot[(size_t) idx]]);

        return detail::drawFromScored (ws.scored, count, variation, rng);
    }

    //==============================================================================
    /** Plays a whole game out and hands it back in the shape the sequencer
        already knows how to replay. This is a message thread job: the audio
        thread only ever sees a finished record. */
    inline sgf::Game generate (const Settings& settings)
    {
        const bool reading = (settings.players == Players::reading);

        sgf::Game game;

        game.valid     = true;
        game.size      = go::isSupportedSize (settings.size) ? settings.size : 9;
        game.blackName = "Kuro";
        game.blackRank = reading ? settings.readingBlack.name : settings.black.name;
        game.whiteName = "Shiro";
        game.whiteRank = reading ? settings.readingWhite.name : settings.white.name;
        game.gameName  = "self-play";

        const int target = std::max (1, settings.moves);
        game.moves.reserve ((size_t) target);

        go::Board board { game.size };
        Rng rng { settings.seed };

        for (int i = 0; i < 8; ++i)     // xorshift takes a moment to leave a small seed behind
            rng.next();

        ReadingWorkspace workspace;
        go::Stone colour = go::Stone::black;

        for (int move = 0; move < target; ++move)
        {
            int played = -1;
            const int wanted = openingPoint (settings, move);

            if (wanted >= 0)
            {
                //  play() leaves the board untouched unless the move is legal,
                //  so a refused opening point costs nothing but the fall through
                if (board.play (wanted, colour, false, true) == go::MoveResult::ok)
                    played = wanted;
            }

            if (played < 0)
            {
                const bool black = (colour == go::Stone::black);

                const int idx = reading
                    ? chooseReadingMove (board, colour, board.lastMove(),
                                         black ? settings.readingBlack : settings.readingWhite,
                                         settings.variation, rng, workspace)
                    : chooseMove (board, colour, board.lastMove(),
                                  black ? settings.black : settings.white,
                                  settings.variation, rng);

                if (idx < 0)
                    break;          // nothing legal left: the game is as long as it gets

                board.play (idx, colour, false, true);
                played = idx;
            }

            game.moves.push_back ({ colour, played, false });
            colour = go::other (colour);
        }

        return game;
    }

    //==============================================================================
    /** Exchanges two records without touching the heap: every member swap is a
        pointer exchange, which is what makes handing a freshly generated game to
        the audio thread safe to do inside the lock. */
    inline void swapGames (sgf::Game& a, sgf::Game& b) noexcept
    {
        std::swap (a.valid, b.valid);
        std::swap (a.size, b.size);
        a.error.swap (b.error);
        a.blackName.swap (b.blackName);
        a.whiteName.swap (b.whiteName);
        a.blackRank.swap (b.blackRank);
        a.whiteRank.swap (b.whiteRank);
        a.result.swap (b.result);
        a.date.swap (b.date);
        a.gameName.swap (b.gameName);
        a.place.swap (b.place);
        a.komi.swap (b.komi);
        a.setup.swap (b.setup);
        a.moves.swap (b.moves);
    }
}
