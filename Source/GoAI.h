#pragma once

//  Two Go players that write a game record.
//
//  They are heuristic, not searching: every legal point is scored by a handful
//  of terms a beginner would recognise - take stones, save your own, cut,
//  connect, stay near the fight, keep off the first line - and one of the best
//  few is drawn at random. There is no reading, no playout and no net, which is
//  the point: a whole game is a few hundred microseconds of plain arithmetic,
//  so the plugin can keep a record ready without ever leaving the machine.
//
//  What makes two players out of one function is the weights. Black and White
//  hold a different Style, so they want different points from the same board -
//  the way two people with different habits do - and the games that come out
//  have a shape rather than a spread.
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
//  different game from there on, and a saved session would come back wrong.
//
//  Like GoBoard.h and SgfParser.h this is plain C++: no JUCE, so it can be
//  tested without a host.

#include "GoBoard.h"
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
    /** The book both players open from unless they are given an opening of their
        own, as (column, row) pairs, Black first. These are not invented: they are
        the first ten moves of two of the records in sgf/ -
        nine_dan_9x9_43610191.sgf on a 9x9, Blackie_BIBA_13x13_25655059.sgf on a
        13x13 - so the motif every game starts from is real play rather than a
        pattern that merely looks like it.

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

        return size == 13 ? book13 : book9;
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

        Style black = territorial();
        Style white = fighting();
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

        /** How many of the best points are worth drawing between. Keeping this
            short is what stops a high variation setting from turning the players
            into noise: the choice widens, but only over moves that scored. */
        constexpr int shortlist = 12;
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

        if (count == 0)
            return -1;

        const int top = std::min (count, detail::shortlist);

        std::partial_sort (scored.begin(), scored.begin() + top, scored.begin() + count,
                           [] (const detail::Candidate& a, const detail::Candidate& b)
                           {
                               //  the index breaks ties, so the order is total and
                               //  one seed cannot land on two different moves
                               return a.score != b.score ? a.score > b.score : a.idx < b.idx;
                           });

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

        std::array<std::uint32_t, (size_t) detail::shortlist> weights {};
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

    //==============================================================================
    /** Plays a whole game out and hands it back in the shape the sequencer
        already knows how to replay. This is a message thread job: the audio
        thread only ever sees a finished record. */
    inline sgf::Game generate (const Settings& settings)
    {
        sgf::Game game;

        game.valid     = true;
        game.size      = go::isSupportedSize (settings.size) ? settings.size : 9;
        game.blackName = "Kuro";
        game.blackRank = settings.black.name;
        game.whiteName = "Shiro";
        game.whiteRank = settings.white.name;
        game.gameName  = "self-play";

        const int target = std::max (1, settings.moves);
        game.moves.reserve ((size_t) target);

        go::Board board { game.size };
        Rng rng { settings.seed };

        for (int i = 0; i < 8; ++i)     // xorshift takes a moment to leave a small seed behind
            rng.next();

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
                const auto& style = (colour == go::Stone::black ? settings.black : settings.white);
                const int idx = chooseMove (board, colour, board.lastMove(), style, settings.variation, rng);

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
