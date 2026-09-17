#pragma once

//  A third kind of player: one that reads by playing games out.
//
//  The reading players (GoAI.h) score every point with rules and pick one of the
//  best. They find good moves - the right one is nearly always among their top
//  three - but their rules cannot tell which of those is right, because that
//  depends on what happens several moves later. More of the same rules does not
//  help: a look-ahead judged by those rules was measured, and gained nothing.
//
//  This player asks the board instead. From the position, it plays a few
//  thousand quick games to the end - each move a random legal point that does
//  not fill its own eye - and counts who won. Moves that keep winning get
//  explored more deeply (a Monte Carlo tree search), and a move also learns from
//  every game in which the same side played it later on (RAVE, "all moves as
//  first"), which is what makes a few thousand games enough. At the top of the
//  tree the reading players' own scores say which points to try first.
//
//  Nothing here is trained and there is no model: random games, counted. Like
//  the rest of the engine it is integer only and deterministic - a position, a
//  seed and a playout budget name exactly one answer on every machine. The
//  work is split across a fixed number of independent trees whose counts are
//  added up, so how many cores run them changes how long it takes, never what
//  comes out.
//
//  Plain C++, no JUCE, no floats.

#include "GoAI.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <thread>
#include <vector>

namespace gosearch
{
    inline constexpr int maxSide   = go::maxSize + 2;       //  a one-point border all round
    inline constexpr int maxPadded = maxSide * maxSide;

    enum : std::int8_t { empty = 0, black = 1, white = 2, border = 3 };

    inline int opponent (int colour) noexcept { return 3 - colour; }

    //==============================================================================
    /** Scratch for flood fills, kept apart from the board so copying a board for
        every playout copies only the position. */
    struct Scratch
    {
        std::array<std::uint32_t, maxPadded> mark {};
        std::array<std::int16_t, maxPadded> stack {};
        std::uint32_t stamp = 0;

        std::uint32_t next() noexcept
        {
            stamp += 2;

            if (stamp >= 0xfffffff0u)
            {
                mark.fill (0);
                stamp = 2;
            }

            return stamp;
        }
    };

    /** A board built for playing games out fast: a border so neighbours need no
        bounds checks, a list of empty points to draw from, simple ko. */
    struct FastBoard
    {
        int n = 9, side = 11, cells = 81;
        std::array<std::int8_t, maxPadded> c {};
        std::array<std::int16_t, go::maxCells> empties {};
        std::array<std::int16_t, maxPadded> emptyAt {};
        int emptyCount = 0;
        int ko = -1;            //  the point the side to move may not take back
        int toMove = black;
        int passes = 0;
        int lastMove = -1, lastMove2 = -1;

        int pad (int idx) const noexcept      { return (idx / n + 1) * side + idx % n + 1; }
        int unpad (int p) const noexcept      { return (p / side - 1) * n + p % side - 1; }

        void init (const go::Board& b, go::Stone colourToMove)
        {
            n = b.size();
            side = n + 2;
            cells = n * n;
            c.fill (border);
            emptyCount = 0;

            for (int i = 0; i < cells; ++i)
            {
                const int p = pad (i);
                const auto s = b.at (i);
                c[(size_t) p] = s == go::Stone::black ? black : s == go::Stone::white ? white : empty;

                if (c[(size_t) p] == empty)
                    addEmpty (p);
            }

            toMove = colourToMove == go::Stone::black ? black : white;
            passes = 0;
            lastMove = b.lastMove() >= 0 ? pad (b.lastMove()) : -1;
            lastMove2 = -1;
            ko = -1;

            //  go::Board keeps no ko point, but it can be read back: a lone stone
            //  that just took exactly one stone and sits on one liberty makes
            //  that liberty the point that cannot be retaken
            if (lastMove >= 0 && b.lastCaptureCount() == 1)
            {
                Scratch s;
                int liberty = -1;

                if (chainSize (lastMove, s) == 1 && liberties (lastMove, 2, s, &liberty) == 1)
                    ko = liberty;
            }
        }

        void addEmpty (int p) noexcept
        {
            emptyAt[(size_t) p] = (std::int16_t) emptyCount;
            empties[(size_t) emptyCount++] = (std::int16_t) p;
        }

        void removeEmpty (int p) noexcept
        {
            const int i = emptyAt[(size_t) p];
            const int last = empties[(size_t) (--emptyCount)];
            empties[(size_t) i] = (std::int16_t) last;
            emptyAt[(size_t) last] = (std::int16_t) i;
        }

        /** Liberties of the chain at p, counted up to `enough`. */
        int liberties (int p, int enough, Scratch& s, int* aLiberty = nullptr) const noexcept
        {
            const int colour = c[(size_t) p];
            const auto stones = s.next(), libs = stones + 1;
            int top = 0, count = 0;

            s.stack[(size_t) top++] = (std::int16_t) p;
            s.mark[(size_t) p] = stones;

            while (top > 0)
            {
                const int q = s.stack[(size_t) --top];

                for (const int d : { 1, -1, side, -side })
                {
                    const int r = q + d;
                    const int at = c[(size_t) r];

                    if (at == empty)
                    {
                        if (s.mark[(size_t) r] != libs)
                        {
                            s.mark[(size_t) r] = libs;

                            if (aLiberty != nullptr)
                                *aLiberty = r;

                            if (++count >= enough)
                                return count;
                        }
                    }
                    else if (at == colour && s.mark[(size_t) r] != stones)
                    {
                        s.mark[(size_t) r] = stones;
                        s.stack[(size_t) top++] = (std::int16_t) r;
                    }
                }
            }

            return count;
        }

        int chainSize (int p, Scratch& s) const noexcept
        {
            const int colour = c[(size_t) p];
            const auto stones = s.next();
            int top = 0, count = 0;

            s.stack[(size_t) top++] = (std::int16_t) p;
            s.mark[(size_t) p] = stones;

            while (top > 0)
            {
                const int q = s.stack[(size_t) --top];
                ++count;

                for (const int d : { 1, -1, side, -side })
                {
                    const int r = q + d;

                    if (c[(size_t) r] == colour && s.mark[(size_t) r] != stones)
                    {
                        s.mark[(size_t) r] = stones;
                        s.stack[(size_t) top++] = (std::int16_t) r;
                    }
                }
            }

            return count;
        }

        int removeChain (int p, Scratch& s) noexcept
        {
            const int colour = c[(size_t) p];
            int top = 0, count = 0;

            s.stack[(size_t) top++] = (std::int16_t) p;
            c[(size_t) p] = empty;

            while (top > 0)
            {
                const int q = s.stack[(size_t) --top];
                addEmpty (q);
                ++count;

                for (const int d : { 1, -1, side, -side })
                {
                    const int r = q + d;

                    if (c[(size_t) r] == colour)
                    {
                        c[(size_t) r] = empty;
                        s.stack[(size_t) top++] = (std::int16_t) r;
                    }
                }
            }

            return count;
        }

        /** A point every neighbour of which is `colour` (or the edge), with at most
            one diagonal of the other colour - none on the edge. Filling one is
            never worth it, so playouts do not. */
        bool isEye (int p, int colour) const noexcept
        {
            for (const int d : { 1, -1, side, -side })
            {
                const int at = c[(size_t) (p + d)];

                if (at != colour && at != border)
                    return false;
            }

            int enemy = 0, edge = 0;

            for (const int d : { side + 1, side - 1, -side + 1, -side - 1 })
            {
                const int at = c[(size_t) (p + d)];

                if (at == border)                ++edge;
                else if (at == opponent (colour)) ++enemy;
            }

            return edge > 0 ? enemy == 0 : enemy <= 1;
        }

        /** Whether the side to move may play at p, without playing it: an empty
            neighbour, a capture, or a chain of its own with a liberty to spare. */
        bool isLegal (int p, Scratch& s) const noexcept
        {
            if (c[(size_t) p] != empty || p == ko)
                return false;

            const int colour = toMove, enemy = opponent (colour);

            for (const int d : { 1, -1, side, -side })
            {
                const int q = p + d;
                const int at = c[(size_t) q];

                if (at == empty)
                    return true;

                if (at == enemy && liberties (q, 2, s) == 1)
                    return true;

                if (at == colour && liberties (q, 2, s) >= 2)
                    return true;
            }

            return false;
        }

        /** Whether playing p would leave the side to move's chain of two or more
            stones on a single liberty without taking anything - handing it over. */
        bool isSelfAtari (int p, Scratch& s) noexcept
        {
            const int colour = toMove, enemy = opponent (colour);
            c[(size_t) p] = (std::int8_t) colour;

            bool result = false;
            bool captures = false;

            for (const int d : { 1, -1, side, -side })
                if (c[(size_t) (p + d)] == enemy && liberties (p + d, 1, s) == 0)
                    captures = true;

            if (! captures && liberties (p, 2, s) <= 1)
                result = chainSize (p, s) >= 2;

            c[(size_t) p] = empty;
            return result;
        }

        /** What a player who is paying attention does about the last move: take a
            chain it left in atari, or pull out one of its own that it put there.
            Returns a padded point, or -1 when there is nothing urgent. */
        int urgentMove (Scratch& s, goai::Rng& rng) noexcept
        {
            if (lastMove < 0)
                return -1;

            const int colour = toMove, enemy = opponent (colour);
            std::array<int, 8> found {};
            int count = 0;

            const auto consider = [&] (int q)
            {
                int liberty = -1;

                if (c[(size_t) q] == enemy && liberties (q, 2, s, &liberty) == 1)
                {
                    if (isLegal (liberty, s) && count < (int) found.size())
                        found[(size_t) count++] = liberty;          //  take it
                }
                else if (c[(size_t) q] == colour && liberties (q, 2, s, &liberty) == 1)
                {
                    //  run, if running gains a liberty
                    if (isLegal (liberty, s))
                    {
                        c[(size_t) liberty] = (std::int8_t) colour;
                        const bool escapes = liberties (liberty, 2, s) >= 2;
                        c[(size_t) liberty] = empty;

                        if (escapes && count < (int) found.size())
                            found[(size_t) count++] = liberty;
                    }
                }
            };

            consider (lastMove);

            for (const int d : { 1, -1, side, -side })
                if (c[(size_t) (lastMove + d)] == colour || c[(size_t) (lastMove + d)] == enemy)
                    consider (lastMove + d);

            return count == 0 ? -1 : found[(size_t) rng.below ((std::uint32_t) count)];
        }

        /** Plays for the side to move. Leaves the board untouched and returns false
            when the point is taken, is the ko point, or would be suicide. */
        bool play (int p, Scratch& s) noexcept
        {
            if (p < 0)
            {
                ++passes;
                ko = -1;
                lastMove2 = lastMove;
                lastMove = -1;
                toMove = opponent (toMove);
                return true;
            }

            if (c[(size_t) p] != empty || p == ko)
                return false;

            const int colour = toMove, enemy = opponent (colour);
            c[(size_t) p] = (std::int8_t) colour;

            int captured = 0, capturedAt = -1;

            for (const int d : { 1, -1, side, -side })
            {
                const int q = p + d;

                if (c[(size_t) q] == enemy && liberties (q, 1, s) == 0)
                {
                    captured += removeChain (q, s);
                    capturedAt = q;
                }
            }

            if (captured == 0 && liberties (p, 1, s) == 0)
            {
                c[(size_t) p] = empty;
                return false;
            }

            removeEmpty (p);

            ko = -1;

            if (captured == 1)
            {
                int liberty = -1;

                if (chainSize (p, s) == 1 && liberties (p, 2, s, &liberty) == 1 && liberty == capturedAt)
                    ko = capturedAt;
            }

            passes = 0;
            lastMove2 = lastMove;
            lastMove = p;
            toMove = enemy;
            return true;
        }

        /** Black's area less White's, doubled, less komi (doubled): above zero
            Black wins. Empty points count for a colour when every neighbour is
            that colour - after a game played out, that is every eye. */
        int blackMargin2 (int komi2) const noexcept
        {
            int b = 0, w = 0;

            for (int row = 1; row <= n; ++row)
            {
                for (int col = 1; col <= n; ++col)
                {
                    const int p = row * side + col;
                    const int at = c[(size_t) p];

                    if (at == black) { ++b; continue; }
                    if (at == white) { ++w; continue; }

                    int touches = 0;

                    for (const int d : { 1, -1, side, -side })
                    {
                        const int x = c[(size_t) (p + d)];

                        if (x == black || x == white)
                            touches |= x;
                    }

                    if (touches == black)      ++b;
                    else if (touches == white) ++w;
                }
            }

            return 2 * (b - w) - komi2;
        }
    };

    //==============================================================================
    struct Config
    {
        int playouts = 4000;        //  games played out per move, over all trees
        int trees = 4;              //  independent trees; fixed, so the answer is too
        int komi2 = 15;             //  7.5, doubled
        int raveBias = 400;         //  how long RAVE's opinion outweighs a move's own games
        int explore = 45000;        //  16.16: how much a prior is worth against a result
        int expandAfter = 0;        //  games through a leaf before it grows children; 0 = by board size
        bool readingPriors = true;  //  order the top of the tree by the reading players' scores
        bool heavyPlayouts = true;  //  playouts take ataris and avoid self-atari instead of playing blind
    };

    /** A move the search considered, with what it found. */
    struct Choice
    {
        int idx = -1;               //  board index, -1 a pass
        int visits = 0;
        int winPermille = 0;        //  of the games through it, won by the side that played it
    };

    namespace detail
    {
        inline std::uint64_t isqrt (std::uint64_t x) noexcept
        {
            std::uint64_t r = 0, bit = 1ull << 62;

            while (bit > x)
                bit >>= 2;

            while (bit != 0)
            {
                if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
                else              { r >>= 1; }

                bit >>= 2;
            }

            return r;
        }

        struct Node
        {
            std::int32_t move = -1;         //  padded point, -1 a pass
            std::int32_t firstChild = -1;
            std::int32_t childCount = 0;
            std::uint32_t visits = 0, wins = 0;
            std::uint32_t raveVisits = 0, raveWins = 0;
            std::uint32_t prior = 0;        //  16.16, the children of a node sum to about 1
        };

        struct Tree
        {
            std::vector<Node> nodes;
            Scratch scratch;
            std::array<std::int32_t, maxPadded> firstPly {};    //  AMAF: when a point was first played, -1 never
            std::array<std::int8_t, maxPadded> firstColour {};
            std::array<std::int32_t, 1024> path {};
        };

        /** Children for the side to move at `node`: every legal point that is not
            its own eye, and a pass once few of those are left. */
        inline void expand (Tree& t, int node, const FastBoard& b, const Config& cfg,
                            const std::vector<std::uint32_t>* rootPriors)
        {
            const int first = (int) t.nodes.size();
            const int colour = b.toMove;
            int count = 0;

            for (int i = 0; i < b.emptyCount; ++i)
            {
                const int p = b.empties[(size_t) i];

                if (b.isEye (p, colour) || ! b.isLegal (p, t.scratch))
                    continue;

                Node child;
                child.move = p;
                t.nodes.push_back (child);
                ++count;
            }

            if (count <= b.cells / 10)
            {
                Node pass;
                pass.move = -1;
                t.nodes.push_back (pass);
                ++count;
            }

            //  the order points are tried in: the reading players' scores at the
            //  root, and near the last two moves further down
            std::uint64_t total = 0;

            for (int i = 0; i < count; ++i)
            {
                auto& child = t.nodes[(size_t) (first + i)];
                std::uint32_t weight;

                if (rootPriors != nullptr)
                {
                    weight = child.move >= 0 ? (*rootPriors)[(size_t) b.unpad (child.move)] : 64;
                }
                else
                {
                    weight = 256;

                    if (child.move >= 0)
                    {
                        for (const int last : { b.lastMove, b.lastMove2 })
                        {
                            if (last < 0)
                                continue;

                            const int dr = std::abs (child.move / b.side - last / b.side);
                            const int dc = std::abs (child.move % b.side - last % b.side);

                            if (dr <= 1 && dc <= 1)
                                weight += 512;
                            else if (dr <= 2 && dc <= 2)
                                weight += 128;
                        }
                    }
                    else
                    {
                        weight = 64;
                    }
                }

                child.prior = weight;
                total += weight;
            }

            for (int i = 0; i < count; ++i)
            {
                auto& child = t.nodes[(size_t) (first + i)];
                child.prior = (std::uint32_t) (((std::uint64_t) child.prior << 16) / std::max<std::uint64_t> (1, total));
            }

            auto& parent = t.nodes[(size_t) node];
            parent.firstChild = first;
            parent.childCount = count;
            (void) cfg;
        }

        inline int select (const Tree& t, const Node& parent, const Config& cfg) noexcept
        {
            const std::uint64_t sqrtParent = isqrt ((std::uint64_t) parent.visits + 1);
            std::int64_t bestScore = -1;
            int best = parent.firstChild;

            for (int i = 0; i < parent.childCount; ++i)
            {
                const auto& child = t.nodes[(size_t) (parent.firstChild + i)];

                //  its own games, and every game in which this side played the point
                //  later on; the latter matter less the more of the former there are
                const std::uint64_t q = child.visits > 0 ? ((std::uint64_t) child.wins << 16) / child.visits : 32768;
                const std::uint64_t qr = child.raveVisits > 0 ? ((std::uint64_t) child.raveWins << 16) / child.raveVisits : q;

                const std::uint64_t rv = child.raveVisits, v = child.visits;
                const std::uint64_t beta = rv == 0 ? 0 : (rv << 16) / (rv + v + v * rv / (std::uint64_t) cfg.raveBias);
                const std::uint64_t value = (q * (65536 - beta) + qr * beta) >> 16;

                const std::uint64_t bonus = ((std::uint64_t) cfg.explore * child.prior >> 16) * sqrtParent / (v + 1);
                const std::int64_t score = (std::int64_t) (value + bonus);

                if (score > bestScore)      //  ties keep the earlier child
                {
                    bestScore = score;
                    best = parent.firstChild + i;
                }
            }

            return best;
        }

        /** One random game from here: a legal point that is not the mover's own
            eye, drawn at random, until both sides have nothing left. */
        inline void playOut (FastBoard& b, Tree& t, goai::Rng& rng, int& ply, bool heavy)
        {
            const int cap = b.cells * 3;

            const auto record = [&t, &ply] (int p, int colour)
            {
                if (t.firstPly[(size_t) p] < 0)
                {
                    t.firstPly[(size_t) p] = ply;
                    t.firstColour[(size_t) p] = (std::int8_t) colour;
                }
            };

            for (int moves = 0; b.passes < 2 && moves < cap; ++moves)
            {
                const int colour = b.toMove;
                int played = -1;

                if (heavy)
                {
                    const int urgent = b.urgentMove (t.scratch, rng);

                    if (urgent >= 0 && b.play (urgent, t.scratch))
                        played = urgent;
                }

                int k = b.emptyCount, fallback = -1;

                while (played < 0 && k > 0)
                {
                    const int i = (int) rng.below ((std::uint32_t) k);
                    const int p = b.empties[(size_t) i];

                    if (! b.isEye (p, colour))
                    {
                        if (heavy && b.isSelfAtari (p, t.scratch))
                        {
                            if (fallback < 0)
                                fallback = p;
                        }
                        else if (b.play (p, t.scratch))
                        {
                            played = p;
                            break;
                        }
                    }

                    //  not playable now: set it aside for this move
                    --k;
                    std::swap (b.empties[(size_t) i], b.empties[(size_t) k]);
                    b.emptyAt[(size_t) b.empties[(size_t) i]] = (std::int16_t) i;
                    b.emptyAt[(size_t) b.empties[(size_t) k]] = (std::int16_t) k;
                }

                //  nothing but self-atari left: better that than passing with the
                //  game undecided
                if (played < 0 && fallback >= 0 && b.play (fallback, t.scratch))
                    played = fallback;

                if (played >= 0)
                    record (played, colour);
                else
                    b.play (-1, t.scratch);

                ++ply;
            }
        }

        inline void runTree (Tree& t, const FastBoard& root, const Config& cfg, int playouts, std::uint32_t seed,
                             const std::vector<std::uint32_t>* rootPriors)
        {
            goai::Rng rng { seed };

            for (int i = 0; i < 8; ++i)
                rng.next();

            //  a leaf grows children once enough games have gone through it that the
            //  children are worth their memory: a 19x19 node has up to 361 of them
            const int expandAfter = cfg.expandAfter > 0 ? cfg.expandAfter
                                  : root.n <= 9 ? 2 : root.n <= 13 ? 4 : 8;

            t.nodes.clear();
            t.nodes.reserve ((size_t) std::min (playouts / expandAfter + 1, 1 << 20) * (size_t) root.emptyCount + 1);
            t.nodes.push_back (Node {});
            expand (t, 0, root, cfg, rootPriors);

            for (int g = 0; g < playouts; ++g)
            {
                FastBoard b = root;
                t.firstPly.fill (-1);

                int depth = 0, node = 0, ply = 0;
                t.path[(size_t) depth++] = 0;

                //  down the tree
                while (t.nodes[(size_t) node].childCount > 0 && b.passes < 2 && depth < (int) t.path.size() - 1)
                {
                    node = select (t, t.nodes[(size_t) node], cfg);
                    const int move = t.nodes[(size_t) node].move;
                    const int colour = b.toMove;

                    b.play (move, t.scratch);       //  legal: it was when the child was made, and the path is the same

                    if (move >= 0 && t.firstPly[(size_t) move] < 0)
                    {
                        t.firstPly[(size_t) move] = ply;
                        t.firstColour[(size_t) move] = (std::int8_t) colour;
                    }

                    ++ply;
                    t.path[(size_t) depth++] = node;

                    if (t.nodes[(size_t) node].childCount == 0 && b.passes < 2
                        && t.nodes[(size_t) node].visits + 1 >= (std::uint32_t) expandAfter)
                    {
                        expand (t, node, b, cfg, nullptr);
                        break;
                    }
                }

                const int treePly = ply;
                playOut (b, t, rng, ply, cfg.heavyPlayouts);

                const int winner = b.blackMargin2 (cfg.komi2) > 0 ? black : white;

                //  back up: each node counts the games won by the side that moved into
                //  it; its children learn from every point their side played later
                for (int d = depth - 1; d >= 0; --d)
                {
                    const int id = t.path[(size_t) d];
                    auto& n = t.nodes[(size_t) id];

                    //  at depth d the side to move is root's side on even d
                    const int moverIntoNode = (d % 2 == 1) ? root.toMove : opponent (root.toMove);
                    const int toMoveHere = (d % 2 == 0) ? root.toMove : opponent (root.toMove);

                    ++n.visits;

                    if (d > 0 && winner == moverIntoNode)
                        ++n.wins;

                    for (int i = 0; i < n.childCount; ++i)
                    {
                        auto& child = t.nodes[(size_t) (n.firstChild + i)];

                        if (child.move < 0)
                            continue;

                        const int fp = t.firstPly[(size_t) child.move];

                        if (fp >= d && t.firstColour[(size_t) child.move] == toMoveHere)
                        {
                            ++child.raveVisits;

                            if (winner == toMoveHere)
                                ++child.raveWins;
                        }
                    }
                }

                (void) treePly;
            }
        }
    }

    //==============================================================================
    /** Everything a search needs set aside, so the heap is touched once a game
        rather than once a move. */
    struct Workspace
    {
        std::vector<detail::Tree> trees;
        goai::ReadingWorkspace reading;
        std::vector<std::uint32_t> priors;
    };

    /** The reading players' scores as an order to try points in: the best of them
        weighs most, and nothing weighs nothing. */
    inline void readingPriors (const go::Board& board, go::Stone colour, const goai::ReadingStyle& style,
                               Workspace& ws)
    {
        const int cells = board.cellCount();
        ws.priors.assign ((size_t) cells, 16);

        const int count = goai::scoreReadingMoves (board, colour, board.lastMove(), style, ws.reading);

        if (count == 0)
            return;

        int best = ws.reading.scored[0].score;

        for (int i = 1; i < count; ++i)
            best = std::max (best, ws.reading.scored[(size_t) i].score);

        for (int i = 0; i < count; ++i)
        {
            const auto& c = ws.reading.scored[(size_t) i];

            //  e^-(behind / 4 points), scaled to 4096 for the best
            const auto behind = (std::uint32_t) std::min (best - c.score, 100000);
            const auto w = goai::expNegFixed ((std::uint32_t) (((std::uint64_t) behind << 16) / 400)) >> 4;
            ws.priors[(size_t) c.idx] = 16 + w;
        }
    }

    /** Every move the search tried from this position, most played first. The
        board is not changed. */
    inline std::vector<Choice> analyse (const go::Board& board, go::Stone colour, const goai::ReadingStyle& style,
                                        const Config& cfg, std::uint32_t seed, Workspace& ws)
    {
        FastBoard root;
        root.init (board, colour);

        const bool priors = cfg.readingPriors;

        if (priors)
            readingPriors (board, colour, style, ws);

        const int trees = std::max (1, cfg.trees);
        ws.trees.resize ((size_t) trees);

        const auto work = [&] (int tree)
        {
            detail::runTree (ws.trees[(size_t) tree], root, cfg, std::max (1, cfg.playouts / trees),
                             goai::gameSeed (seed, tree), priors ? &ws.priors : nullptr);
        };

        {
            std::vector<std::thread> pool;

            for (int tree = 1; tree < trees; ++tree)
                pool.emplace_back (work, tree);

            work (0);

            for (auto& thread : pool)
                thread.join();
        }

        //  the trees agree on their root children - the same position, the same
        //  order - so their counts add up child by child
        std::vector<Choice> choices;
        const auto& first = ws.trees[0].nodes[0];

        for (int i = 0; i < first.childCount; ++i)
        {
            Choice choice;
            const int move = ws.trees[0].nodes[(size_t) (first.firstChild + i)].move;
            choice.idx = move >= 0 ? root.unpad (move) : -1;

            std::uint64_t visits = 0, wins = 0;

            for (const auto& tree : ws.trees)
            {
                const auto& child = tree.nodes[(size_t) (tree.nodes[0].firstChild + i)];
                visits += child.visits;
                wins += child.wins;
            }

            choice.visits = (int) visits;
            choice.winPermille = visits > 0 ? (int) (wins * 1000 / visits) : 0;
            choices.push_back (choice);
        }

        std::stable_sort (choices.begin(), choices.end(), [] (const Choice& a, const Choice& b)
        {
            return a.visits > b.visits;
        });

        return choices;
    }

    /** The move to play from the analysis: the most played at variation 0; above
        it, a draw weighted by games among the moves played nearly as often. */
    inline int pick (const std::vector<Choice>& choices, int variation, goai::Rng& rng)
    {
        if (choices.empty())
            return -1;

        if (variation <= 0)
            return choices[0].idx;

        //  variation 100 lets in moves with a third of the best one's games
        const std::uint64_t floor = (std::uint64_t) choices[0].visits * (100 - std::min (variation, 100) * 2 / 3) / 100;
        std::uint64_t total = 0;
        size_t count = 0;

        for (const auto& c : choices)
        {
            if ((std::uint64_t) c.visits < floor || c.visits == 0 || count >= (size_t) goai::detail::shortlist)
                break;

            total += (std::uint64_t) c.visits * c.visits;
            ++count;
        }

        if (total == 0)
            return choices[0].idx;

        auto roll = ((std::uint64_t) rng.next() << 32 | rng.next()) % total;

        for (size_t i = 0; i < count; ++i)
        {
            const std::uint64_t w = (std::uint64_t) choices[i].visits * choices[i].visits;

            if (roll < w)
                return choices[i].idx;

            roll -= w;
        }

        return choices[0].idx;
    }

    //==============================================================================
    //  Budgets. They are counts, never times: "think for half a second" would
    //  play another game on a faster or busier machine. The counts are chosen so
    //  that, on the laptop this was built on (an i7-11850H, four trees on four
    //  cores), a whole self-play game is ready in about five seconds and an
    //  answer in a match in about one - well inside the fifteen seconds a
    //  session may take to load.

    /** Microseconds one playout takes on that laptop, per board size, heavy
        playouts, measured with tools/GoSearchBench.cpp - and a margin for the
        plugin's compiler being a little slower than the bench's. */
    inline int microsecondsPerPlayout (int size) noexcept
    {
        return size <= 8 ? 20 : size <= 9 ? 24 : size <= 13 ? 66 : 140;
    }

    /** Playouts for each move of a self-play game of `moves` moves: about five
        seconds for the whole game, never fewer than a few hundred a move. */
    inline int playoutsForGame (int size, int moves) noexcept
    {
        const int thinking = std::max (1, moves - goai::openingLength);
        const int perMoveMicros = 5000000 / thinking;
        return std::clamp (perMoveMicros / microsecondsPerPlayout (size), 300, 20000);
    }

    /** Playouts for one answer in a match: about a second. */
    inline int playoutsForReply (int size) noexcept
    {
        return std::clamp (1000000 / microsecondsPerPlayout (size), 1000, 50000);
    }

    /** A whole self-play game, the shape goai::generate writes: the same opening,
        the same seeds, the search player on both sides. Passes are recorded; two
        in a row end the game. `keepGoing` is asked between moves, so a game
        nobody wants any more stops being worked on. */
    template <typename KeepGoing>
    inline sgf::Game generate (const goai::Settings& settings, KeepGoing&& keepGoing, Workspace& ws)
    {
        sgf::Game game;

        game.valid     = true;
        game.size      = go::isSupportedSize (settings.size) ? settings.size : 9;
        game.blackName = "Kuro";
        game.blackRank = "search";
        game.whiteName = "Shiro";
        game.whiteRank = "search";
        game.gameName  = "self-play";

        const int target = std::max (1, settings.moves);
        game.moves.reserve ((size_t) target);

        Config cfg;
        cfg.playouts = playoutsForGame (game.size, target);

        go::Board board { game.size };
        goai::Rng rng { settings.seed };

        for (int i = 0; i < 8; ++i)
            rng.next();

        go::Stone colour = go::Stone::black;
        int passes = 0;

        for (int move = 0; move < target && passes < 2; ++move)
        {
            if (! keepGoing())
                return {};

            int played = -1;
            const int wanted = goai::openingPoint (settings, move);

            if (wanted >= 0 && board.play (wanted, colour, false, true) == go::MoveResult::ok)
            {
                played = wanted;
            }
            else
            {
                const bool black = colour == go::Stone::black;
                const auto choices = analyse (board, colour, black ? settings.readingBlack : settings.readingWhite,
                                              cfg, goai::gameSeed (settings.seed, move), ws);
                const int idx = pick (choices, settings.variation, rng);

                //  the search's board keeps simple ko and go::Board the position
                //  before; where they disagree, the next move down the list
                if (idx >= 0 && board.play (idx, colour, false, true) == go::MoveResult::ok)
                {
                    played = idx;
                }
                else if (idx >= 0)
                {
                    for (const auto& c : choices)
                    {
                        if (c.idx >= 0 && board.play (c.idx, colour, false, true) == go::MoveResult::ok)
                        {
                            played = c.idx;
                            break;
                        }
                    }
                }
            }

            if (played >= 0)
            {
                game.moves.push_back ({ colour, played, false });
                passes = 0;
            }
            else
            {
                game.moves.push_back ({ colour, -1, true });
                ++passes;
            }

            colour = go::other (colour);
        }

        return game;
    }

    /** The answer in a match: the move to play, -1 to pass. */
    inline int reply (const go::Board& board, go::Stone colour, const goai::Settings& settings,
                      std::uint32_t seed, Workspace& ws, std::vector<Choice>* considered = nullptr)
    {
        Config cfg;
        cfg.playouts = playoutsForReply (board.size());

        goai::Rng rng { seed };

        for (int i = 0; i < 8; ++i)
            rng.next();

        const bool black = colour == go::Stone::black;
        auto choices = analyse (board, colour, black ? settings.readingBlack : settings.readingWhite, cfg, seed, ws);
        const int idx = pick (choices, settings.variation, rng);

        if (considered != nullptr)
            *considered = choices;

        if (idx < 0)
            return -1;          //  the search chose to pass

        //  the search's board keeps simple ko and go::Board the position before;
        //  where they disagree, the next move down the list
        go::Board trial = board;

        if (trial.play (idx, colour, false, true) == go::MoveResult::ok)
            return idx;

        for (const auto& c : choices)
        {
            trial = board;

            if (c.idx >= 0 && trial.play (c.idx, colour, false, true) == go::MoveResult::ok)
                return c.idx;
        }

        return -1;
    }
}
