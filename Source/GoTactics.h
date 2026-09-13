#pragma once

//  What a board says to a player that reads it: which stones hang together and
//  on how many liberties, what a move would take or save before it is played,
//  whether a chain in atari can be chased down a ladder, where the vital point
//  of a small eye space is, which liberties to fill in a race, and whose area a
//  point lies in. The reading players in GoAI.h are built on it.
//
//  ---- Leela -------------------------------------------------------------------
//  The list of facts comes from Leela, Gian-Carlo Pascutto's Go engine:
//
//      https://github.com/gcp/Leela    (MIT licence, (c) 2007-2020 Gian-Carlo Pascutto)
//
//  Leela steers its simulated games with exactly these - capture and saving
//  sizes, self-atari that is and is not a throw-in, losing ladders, liberty
//  races, the killable eye shapes - and maps territory with Bouzy's dilation and
//  erosion. Each of them is written again here from the idea, in integers over
//  go::Board. No Leela source and none of its data is copied in.
//
//  What Leela then does with those facts is left out, on purpose. It runs a
//  Monte Carlo tree search - thousands of simulated games for every move - and
//  weighs moves with pattern tables and neural networks trained on recorded
//  games. Nothing here searches further than a ladder, nothing here was trained,
//  and nothing learns: every answer below is plain, deterministic arithmetic on
//  the stones in front of it, the same on every machine.
//
//  Like GoBoard.h this is plain C++: no JUCE, no heap, no floats.

#include "GoBoard.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

namespace tactics
{
    //==============================================================================
    /** A set of points that empties in constant time: a point is in it while it
        carries the current stamp, and taking a new stamp clears them all. Every
        pass below that needs a "seen" array uses one of these rather than
        zeroing 361 entries to answer a question about five. */
    struct Marks
    {
        std::array<std::uint32_t, go::maxCells> at {};
        std::uint32_t stamp = 1;

        void clear() noexcept
        {
            if (++stamp == 0)
            {
                at.fill (0);
                stamp = 1;
            }
        }

        bool has (int idx) const noexcept { return at[(size_t) idx] == stamp; }

        /** Adds the point, and says whether it was not there already. */
        bool add (int idx) noexcept
        {
            auto& slot = at[(size_t) idx];

            if (slot == stamp)
                return false;

            slot = stamp;
            return true;
        }
    };

    /** Working space for everything below that walks the board. It is shared
        down a ladder rather than kept on every level of it. */
    struct Scratch
    {
        Marks seen, found;
        std::array<std::int16_t, go::maxCells> queue {};
    };

    //==============================================================================
    /** How many liberties of a chain are kept point by point. A chain with more
        simply has plenty: nothing a player asks needs to know where they are. */
    inline constexpr int keptLiberties = 6;

    struct Chain
    {
        go::Stone colour = go::Stone::none;
        int size = 0;
        int liberties = 0;                                  // the exact count
        int first = 0;                                      // where its stones start in Analysis::stones
        std::array<std::int16_t, keptLiberties> liberty {}; // the first keptLiberties of them
    };

    /** The board as chains. Built once a move, after which every candidate point
        is answered from it rather than by playing the move out. */
    struct Analysis
    {
        int size = 9;
        int chainCount = 0;
        std::array<std::int16_t, go::maxCells> chainAt {};  // -1 on an empty point
        std::array<std::int16_t, go::maxCells> stones {};   // chain by chain
        std::array<Chain, go::maxCells> chains {};

        const Chain& chain (int id) const noexcept { return chains[(size_t) id]; }
        int stone (const Chain& c, int i) const noexcept { return stones[(size_t) (c.first + i)]; }
    };

    /** Whether a point is one of the chain's liberties. Only exact for a chain
        whose liberties are all kept. */
    inline bool hasLiberty (const Chain& c, int idx) noexcept
    {
        const int kept = std::min (c.liberties, keptLiberties);

        for (int i = 0; i < kept; ++i)
            if (c.liberty[(size_t) i] == idx)
                return true;

        return false;
    }

    inline void analyse (const go::Board& board, Analysis& a, Scratch& s)
    {
        const int size = board.size(), cells = board.cellCount();

        a.size = size;
        a.chainCount = 0;

        for (int i = 0; i < cells; ++i)
            a.chainAt[(size_t) i] = -1;

        int placed = 0;

        for (int i = 0; i < cells; ++i)
        {
            const auto colour = board.at (i);

            if (colour == go::Stone::none || a.chainAt[(size_t) i] >= 0)
                continue;

            const auto id = (std::int16_t) a.chainCount++;
            auto& chain = a.chains[(size_t) id];

            chain = {};
            chain.colour = colour;
            chain.first = placed;

            s.found.clear();
            a.chainAt[(size_t) i] = id;
            a.stones[(size_t) placed++] = (std::int16_t) i;

            //  the stone list doubles as the queue: the chain grows onto its end
            for (int q = chain.first; q < placed; ++q)
            {
                ++chain.size;

                go::forEachNeighbour (a.stones[(size_t) q], size, [&] (int n)
                {
                    const auto at = board.at (n);

                    if (at == colour)
                    {
                        if (a.chainAt[(size_t) n] < 0)
                        {
                            a.chainAt[(size_t) n] = id;
                            a.stones[(size_t) placed++] = (std::int16_t) n;
                        }
                    }
                    else if (at == go::Stone::none && s.found.add (n))
                    {
                        if (chain.liberties < keptLiberties)
                            chain.liberty[(size_t) chain.liberties] = (std::int16_t) n;

                        ++chain.liberties;
                    }
                });
            }
        }
    }

    //==============================================================================
    /** What one move would do, read off the chains without playing it. */
    struct MoveFacts
    {
        bool legal = false;
        bool maybeKo = false;           // lifts a single stone: only a real play can tell
        int captured = 0;               // enemy stones it lifts
        int liberties = 0;              // of the chain it makes; keptLiberties means plenty
        int stones = 1;                 // in that chain
        int ownChains = 0;              // own chains it joins
        int enemyChains = 0;            // enemy chains it touches
        int touching = 0;               // enemy stones it touches
        int rescued = 0;                // own stones on one liberty before it and not after
        int reinforced = 0;             // own stones taken from two liberties to three or more
        int threatened = 0;             // enemy stones it leaves on one liberty
        int pressed = 0;                // enemy stones it leaves on two
        int weakEnemy = 0;              // the biggest enemy chain beside it on two liberties or fewer
        int atariCount = 0;
        std::array<int, 4> atari {};    // a stone of each enemy chain it puts in atari
    };

    inline MoveFacts describeMove (const go::Board& board, const Analysis& a, int idx,
                                   go::Stone colour, Marks& marks)
    {
        MoveFacts f;

        if (board.at (idx) != go::Stone::none)
            return f;

        const int size = a.size;
        std::array<int, 4> own {}, foe {}, lifted {};
        int liftedCount = 0;

        go::forEachNeighbour (idx, size, [&] (int n)
        {
            const int id = a.chainAt[(size_t) n];

            if (id < 0)
                return;

            const bool mine = (a.chain (id).colour == colour);

            if (! mine)
                ++f.touching;

            auto& list  = mine ? own : foe;
            auto& count = mine ? f.ownChains : f.enemyChains;

            for (int i = 0; i < count; ++i)
                if (list[(size_t) i] == id)
                    return;

            list[(size_t) count++] = id;
        });

        for (int i = 0; i < f.enemyChains; ++i)
        {
            const int id = foe[(size_t) i];
            const auto& chain = a.chain (id);

            if (chain.liberties <= 2)
                f.weakEnemy = std::max (f.weakEnemy, chain.size);

            if (chain.liberties == 1)
            {
                lifted[(size_t) liftedCount++] = id;
                f.captured += chain.size;
            }
            else if (chain.liberties == 2)
            {
                f.threatened += chain.size;
                f.atari[(size_t) f.atariCount++] = a.stone (chain, 0);
            }
            else if (chain.liberties == 3)
            {
                f.pressed += chain.size;
            }
        }

        f.maybeKo = (f.captured == 1);

        const auto isLifted = [&] (int n)
        {
            const int id = a.chainAt[(size_t) n];

            for (int i = 0; i < liftedCount; ++i)
                if (lifted[(size_t) i] == id)
                    return true;

            return false;
        };

        //  The new chain's liberties: the point's own empty neighbours, the other
        //  liberties of every chain it joins, and every lifted stone beside any
        //  of it. The point itself is marked first, so it never counts.
        marks.clear();
        marks.add (idx);

        int libs = 0;
        bool plenty = false;
        const auto addLiberty = [&] (int n) { if (marks.add (n)) ++libs; };

        go::forEachNeighbour (idx, size, [&] (int n)
        {
            if (board.at (n) == go::Stone::none || (liftedCount > 0 && isLifted (n)))
                addLiberty (n);
        });

        for (int i = 0; i < f.ownChains; ++i)
        {
            const auto& chain = a.chain (own[(size_t) i]);
            f.stones += chain.size;

            if (chain.liberties > keptLiberties)
                plenty = true;
            else
                for (int l = 0; l < chain.liberties; ++l)
                    addLiberty (chain.liberty[(size_t) l]);

            if (liftedCount > 0)
                for (int k = 0; k < chain.size; ++k)
                    go::forEachNeighbour (a.stone (chain, k), size, [&] (int n)
                    {
                        if (isLifted (n))
                            addLiberty (n);
                    });
        }

        f.liberties = plenty ? keptLiberties : std::min (libs, keptLiberties);

        //  no suicide: a move is legal if it takes something or keeps a liberty
        f.legal = (f.captured > 0 || f.liberties > 0);

        if (! f.legal)
            return f;

        for (int i = 0; i < f.ownChains; ++i)
        {
            const auto& chain = a.chain (own[(size_t) i]);

            if (chain.liberties == 1 && f.liberties >= 2)
                f.rescued += chain.size;

            if (chain.liberties == 2 && f.liberties >= 3)
                f.reinforced += chain.size;
        }

        //  a capture also frees own chains in atari that touch the lifted stones
        //  without being joined by this move
        if (liftedCount > 0)
        {
            std::array<int, 16> freed {};
            int freedCount = 0;

            for (int i = 0; i < liftedCount; ++i)
            {
                const auto& chain = a.chain (lifted[(size_t) i]);

                for (int k = 0; k < chain.size; ++k)
                    go::forEachNeighbour (a.stone (chain, k), size, [&] (int n)
                    {
                        const int id = a.chainAt[(size_t) n];

                        if (id < 0 || a.chain (id).colour != colour || a.chain (id).liberties != 1)
                            return;

                        for (int j = 0; j < f.ownChains; ++j)
                            if (own[(size_t) j] == id)
                                return;

                        for (int j = 0; j < freedCount; ++j)
                            if (freed[(size_t) j] == id)
                                return;

                        if (freedCount < (int) freed.size())
                        {
                            freed[(size_t) freedCount++] = id;
                            f.rescued += a.chain (id).size;
                        }
                    });
            }
        }

        return f;
    }

    //==============================================================================
    /** Counts the liberties of the chain at `stone`, stopping once there are
        `limit` of them. The first two found are written to `found`, if given. */
    inline int countLiberties (const go::Board& board, int stone, int limit, Scratch& s,
                               int* found = nullptr)
    {
        const auto colour = board.at (stone);

        if (colour == go::Stone::none)
            return 0;

        const int size = board.size();

        s.seen.clear();
        s.found.clear();
        s.seen.add (stone);

        int top = 0, count = 0;
        s.queue[(size_t) top++] = (std::int16_t) stone;

        while (top > 0 && count < limit)
        {
            const int idx = s.queue[(size_t) --top];

            go::forEachNeighbour (idx, size, [&] (int n)
            {
                if (count >= limit)
                    return;

                const auto at = board.at (n);

                if (at == go::Stone::none)
                {
                    if (s.found.add (n))
                    {
                        if (found != nullptr && count < 2)
                            found[count] = n;

                        ++count;
                    }
                }
                else if (at == colour && s.seen.add (n))
                {
                    s.queue[(size_t) top++] = (std::int16_t) n;
                }
            });
        }

        return count;
    }

    /** Up to `max` enemy stones touching the chain at `stone`, one per point. */
    inline int enemyNeighbours (const go::Board& board, int stone, Scratch& s,
                                std::int16_t* out, int max)
    {
        const auto colour = board.at (stone);
        const int size = board.size();

        s.seen.clear();
        s.found.clear();
        s.seen.add (stone);

        int top = 0, count = 0;
        s.queue[(size_t) top++] = (std::int16_t) stone;

        while (top > 0)
        {
            const int idx = s.queue[(size_t) --top];

            go::forEachNeighbour (idx, size, [&] (int n)
            {
                const auto at = board.at (n);

                if (at == colour)
                {
                    if (s.seen.add (n))
                        s.queue[(size_t) top++] = (std::int16_t) n;
                }
                else if (at != go::Stone::none && count < max && s.found.add (n))
                {
                    out[count++] = (std::int16_t) n;
                }
            });
        }

        return count;
    }

    //==============================================================================
    /** Reading a ladder. A chain on one liberty runs; the chaser answers every run
        with another atari; the chain lives if it ever reaches three liberties or
        takes a chasing stone. At every step both ataris are tried, because the
        chaser gets to pick the one that works.

        A budget of positions keeps a branching chase from getting expensive, and
        a chase the budget runs out on counts as no ladder - the cautious answer
        from either side: the runner does not give up stones it might save, and
        the chaser does not count stones it might not get. */
    struct LadderBudget
    {
        int positions = 0;
        int limit = 160;
    };

    /** Deep enough for a ladder corner to corner on a 19x19, and a bound on how
        many boards a chase keeps on the stack. */
    inline constexpr int maxLadderDepth = 2 * go::maxSize + 4;

    inline bool chaserWins (const go::Board& board, int runner, LadderBudget& budget,
                            Scratch& s, int depth);

    /** The chain at `runner` is on one liberty and has the move: is it caught? */
    inline bool runnerDies (const go::Board& board, int runner, LadderBudget& budget,
                            Scratch& s, int depth = 0)
    {
        if (++budget.positions > budget.limit || depth > maxLadderDepth)
            return false;

        const auto colour = board.at (runner);
        int escape[2] { -1, -1 };

        if (countLiberties (board, runner, 2, s, escape) != 1)
            return false;

        //  a chasing stone that is itself in atari is a capture, and out
        std::array<std::int16_t, 48> chasers {};
        const int chaserCount = enemyNeighbours (board, runner, s, chasers.data(), (int) chasers.size());

        for (int i = 0; i < chaserCount; ++i)
            if (countLiberties (board, chasers[(size_t) i], 2, s) == 1)
                return false;

        go::Board next = board;

        if (next.play (escape[0], colour, false, true) != go::MoveResult::ok)
            return true;

        const int libs = countLiberties (next, escape[0], 3, s);

        if (libs >= 3) return false;
        if (libs <= 1) return true;

        return chaserWins (next, escape[0], budget, s, depth + 1);
    }

    /** The chain at `runner` has two liberties and the other side has the move:
        does an atari on either of them start a chase that ends in a capture? */
    inline bool chaserWins (const go::Board& board, int runner, LadderBudget& budget,
                            Scratch& s, int depth = 0)
    {
        if (++budget.positions > budget.limit || depth > maxLadderDepth)
            return false;

        const auto chaser = go::other (board.at (runner));
        int libs[2] { -1, -1 };
        const int count = countLiberties (board, runner, 3, s, libs);

        if (count != 2)
            return count == 1;

        for (int i = 0; i < 2; ++i)
        {
            go::Board next = board;

            if (next.play (libs[i], chaser, false, true) != go::MoveResult::ok)
                continue;

            //  a chasing stone left on one liberty is simply taken
            if (countLiberties (next, libs[i], 2, s) < 2)
                continue;

            if (runnerDies (next, runner, budget, s, depth + 1))
                return true;
        }

        return false;
    }

    //==============================================================================
    /** Whether a chain on three liberties or fewer can get more with one move: by
        taking a neighbour that is in atari, or by playing a liberty that brings
        at least two new ones - its own empty neighbours, or the liberties of a
        chain it joins. One new liberty for the one filled is no gain. */
    inline bool canGainLiberties (const Analysis& a, int id)
    {
        const auto& c = a.chain (id);
        const int size = a.size;

        for (int k = 0; k < c.size; ++k)
        {
            bool capture = false;

            go::forEachNeighbour (a.stone (c, k), size, [&] (int n)
            {
                const int e = a.chainAt[(size_t) n];

                if (e >= 0 && a.chain (e).colour != c.colour && a.chain (e).liberties == 1)
                    capture = true;
            });

            if (capture)
                return true;
        }

        for (int l = 0; l < std::min (c.liberties, keptLiberties); ++l)
        {
            const int lib = c.liberty[(size_t) l];

            std::array<int, 32> fresh {};
            int freshCount = 0;
            bool plenty = false;

            const auto addFresh = [&] (int p)
            {
                if (p == lib || hasLiberty (c, p))
                    return;

                for (int j = 0; j < freshCount; ++j)
                    if (fresh[(size_t) j] == p)
                        return;

                if (freshCount < (int) fresh.size())
                    fresh[(size_t) freshCount++] = p;
            };

            go::forEachNeighbour (lib, size, [&] (int n)
            {
                const int d = a.chainAt[(size_t) n];

                if (d < 0)
                {
                    addFresh (n);
                }
                else if (d != id && a.chain (d).colour == c.colour)
                {
                    const auto& other = a.chain (d);

                    if (other.liberties > keptLiberties)
                        plenty = true;
                    else
                        for (int j = 0; j < other.liberties; ++j)
                            addFresh (other.liberty[(size_t) j]);
                }
            });

            if (plenty || freshCount >= 2)
                return true;
        }

        return false;
    }

    /** Points worth playing in a liberty race for `colour`. A race is an own
        chain on two or three liberties that cannot get more, beside an enemy
        chain no better off that cannot either: then the move is to fill the
        enemy's outside liberties, since filling a shared one costs both sides
        alike. race[p] is 2 in a race on two liberties, 1 on three, else 0. */
    inline void findRaceMoves (const Analysis& a, go::Stone colour,
                               std::array<std::int8_t, go::maxCells>& race)
    {
        const int size = a.size, cells = size * size;

        for (int i = 0; i < cells; ++i)
            race[(size_t) i] = 0;

        for (int id = 0; id < a.chainCount; ++id)
        {
            const auto& own = a.chain (id);

            if (own.colour != colour || own.liberties < 2 || own.liberties > 3)
                continue;

            if (canGainLiberties (a, id))
                continue;

            std::array<int, 16> met {};
            int metCount = 0;

            for (int k = 0; k < own.size; ++k)
            {
                go::forEachNeighbour (a.stone (own, k), size, [&] (int n)
                {
                    const int e = a.chainAt[(size_t) n];

                    if (e < 0 || a.chain (e).colour == colour)
                        return;

                    for (int j = 0; j < metCount; ++j)
                        if (met[(size_t) j] == e)
                            return;

                    if (metCount == (int) met.size())
                        return;

                    met[(size_t) metCount++] = e;

                    const auto& foe = a.chain (e);

                    //  one liberty is a capture, handled as one; more than ours
                    //  is a race already lost
                    if (foe.liberties < 2 || foe.liberties > own.liberties || canGainLiberties (a, e))
                        return;

                    const auto strength = (std::int8_t) (own.liberties == 2 ? 2 : 1);

                    for (int j = 0; j < foe.liberties; ++j)
                    {
                        const int lib = foe.liberty[(size_t) j];

                        if (! hasLiberty (own, lib))
                            race[(size_t) lib] = std::max (race[(size_t) lib], strength);
                    }
                });
            }
        }
    }

    //==============================================================================
    /** The vital point of every small eye space on the board, marked with the
        colour that surrounds it; every other point is left as none.

        An eye space here is an empty area of three to six points touched by one
        colour only. Of those shapes some are dead as they stand whoever moves,
        and some live if their owner plays one point first and die if the other
        side plays it: the straight and bent three, the pyramid four, the bulky
        and crossed five and the rabbity six. The point is the same for both
        sides, which is why it is worth a move to either - the owner makes two
        eyes there, the other side makes the space one. The shapes are read off
        how many empty neighbours each point of the space has. */
    inline void findVitalPoints (const go::Board& board, Scratch& s,
                                 std::array<go::Stone, go::maxCells>& vital)
    {
        const int size = board.size(), cells = board.cellCount();

        for (int i = 0; i < cells; ++i)
            vital[(size_t) i] = go::Stone::none;

        s.seen.clear();

        for (int start = 0; start < cells; ++start)
        {
            if (board.at (start) != go::Stone::none || ! s.seen.add (start))
                continue;

            int count = 0, head = 0, touching = 0;
            s.queue[(size_t) count++] = (std::int16_t) start;

            while (head < count)
            {
                go::forEachNeighbour (s.queue[(size_t) head++], size, [&] (int n)
                {
                    const auto at = board.at (n);

                    if (at == go::Stone::none)
                    {
                        if (s.seen.add (n))
                            s.queue[(size_t) count++] = (std::int16_t) n;
                    }
                    else
                    {
                        touching |= (int) at;
                    }
                });
            }

            if (count < 3 || count > 6 || (touching != (int) go::Stone::black && touching != (int) go::Stone::white))
                continue;

            std::array<int, 6> open {};
            std::array<int, 5> withOpen {};

            for (int i = 0; i < count; ++i)
            {
                int n = 0;

                go::forEachNeighbour (s.queue[(size_t) i], size, [&] (int m)
                {
                    if (board.at (m) == go::Stone::none)
                        ++n;
                });

                open[(size_t) i] = n;
                ++withOpen[(size_t) n];
            }

            const auto pointWith = [&] (int n)
            {
                for (int i = 0; i < count; ++i)
                    if (open[(size_t) i] == n)
                        return (int) s.queue[(size_t) i];

                return -1;
            };

            int point = -1;

            switch (count)
            {
                case 3:     point = pointWith (2); break;                               // straight or bent three
                case 4:     if (withOpen[3] == 1) point = pointWith (3); break;        // pyramid four
                case 5:     if (withOpen[4] == 1) point = pointWith (4);               // crossed five
                            else if (withOpen[1] == 1 && withOpen[3] == 1) point = pointWith (3);  // bulky five
                            break;
                case 6:     if (withOpen[1] == 2 && withOpen[4] == 1) point = pointWith (4); break; // rabbity six
                default:    break;
            }

            if (point >= 0)
                vital[(size_t) point] = (go::Stone) touching;
        }
    }

    //==============================================================================
    /** Wears every signed point of an influence map down by each neighbour that
        does not share its sign, `erosions` times; a point never crosses zero. */
    inline void erode (int size, int erosions,
                       std::array<int, go::maxCells>& map, std::array<int, go::maxCells>& work)
    {
        const int cells = size * size;

        for (int e = 0; e < erosions; ++e)
        {
            for (int i = 0; i < cells; ++i)
            {
                const int v = map[(size_t) i];
                int against = 0;

                go::forEachNeighbour (i, size, [&] (int n)
                {
                    if ((v > 0 && map[(size_t) n] <= 0) || (v < 0 && map[(size_t) n] >= 0))
                        ++against;
                });

                work[(size_t) i] = (v > 0 ? std::max (0, v - against)
                                          : (v < 0 ? std::min (0, v + against) : 0));
            }

            std::copy (work.begin(), work.begin() + cells, map.begin());
        }
    }

    /** Bouzy's influence map. Stones start at +128 for Black and -128 for White;
        each dilation lets a point grow towards the side all its signed neighbours
        agree on, and each erosion wears a point down by every neighbour that does
        not share its sign. Five dilations and twenty-one erosions leave only
        what is securely someone's (the area map); five and ten leave the wider
        frameworks each side is building (the moyo map) - so the area map is the
        moyo map eroded eleven more times. Zero is nobody's. */
    inline void influence (const go::Board& board, int dilations, int erosions,
                           std::array<int, go::maxCells>& map, std::array<int, go::maxCells>& work)
    {
        const int size = board.size(), cells = board.cellCount();

        for (int i = 0; i < cells; ++i)
        {
            const auto s = board.at (i);
            map[(size_t) i] = (s == go::Stone::black ? 128 : (s == go::Stone::white ? -128 : 0));
        }

        for (int d = 0; d < dilations; ++d)
        {
            for (int i = 0; i < cells; ++i)
            {
                const int v = map[(size_t) i];
                int up = 0, down = 0;

                go::forEachNeighbour (i, size, [&] (int n)
                {
                    if (map[(size_t) n] > 0)      ++up;
                    else if (map[(size_t) n] < 0) ++down;
                });

                int next = v;

                if (v >= 0 && down == 0) next += up;
                if (v <= 0 && up == 0)   next -= down;

                work[(size_t) i] = next;
            }

            std::copy (work.begin(), work.begin() + cells, map.begin());
        }

        erode (size, erosions, map, work);
    }

    /** Chebyshev distance from every point to the nearest stone of `colour`, or
        the board size where there is none - the same answer the classic players
        get by scanning the board for every point, got in one pass. */
    inline void distanceTo (const go::Board& board, go::Stone colour, Scratch& s,
                            std::array<std::int8_t, go::maxCells>& out)
    {
        const int size = board.size(), cells = board.cellCount();
        int count = 0, head = 0;

        for (int i = 0; i < cells; ++i)
        {
            if (board.at (i) == colour)
            {
                out[(size_t) i] = 0;
                s.queue[(size_t) count++] = (std::int16_t) i;
            }
            else
            {
                out[(size_t) i] = -1;
            }
        }

        while (head < count)
        {
            const int idx = s.queue[(size_t) head++];
            const int col = go::colOf (idx, size), row = go::rowOf (idx, size);

            for (int dr = -1; dr <= 1; ++dr)
            {
                for (int dc = -1; dc <= 1; ++dc)
                {
                    const int c = col + dc, r = row + dr;

                    if (c < 0 || c >= size || r < 0 || r >= size)
                        continue;

                    const int n = go::index (c, r, size);

                    if (out[(size_t) n] < 0)
                    {
                        out[(size_t) n] = (std::int8_t) (out[(size_t) idx] + 1);
                        s.queue[(size_t) count++] = (std::int16_t) n;
                    }
                }
            }
        }

        for (int i = 0; i < cells; ++i)
            if (out[(size_t) i] < 0)
                out[(size_t) i] = (std::int8_t) size;
    }

    //==============================================================================
    /** Whether an empty point is an eye of `colour`: all four orthogonal
        neighbours that colour, and the diagonals as well, bar one away from the
        edge. `placed`, when not -1, is read as holding `placedColour` - the board
        one stone on, without playing it. */
    inline bool isEye (const go::Board& board, int point, go::Stone colour,
                       int placed = -1, go::Stone placedColour = go::Stone::none)
    {
        const int size = board.size();
        const auto at = [&] (int n) { return n == placed ? placedColour : board.at (n); };

        if (at (point) != go::Stone::none)
            return false;

        bool surrounded = true;

        go::forEachNeighbour (point, size, [&] (int n)
        {
            if (at (n) != colour)
                surrounded = false;
        });

        if (! surrounded)
            return false;

        const int col = go::colOf (point, size), row = go::rowOf (point, size);
        int diagonals = 0, friendly = 0;

        for (int dc = -1; dc <= 1; dc += 2)
        {
            for (int dr = -1; dr <= 1; dr += 2)
            {
                const int c = col + dc, r = row + dr;

                if (c < 0 || c >= size || r < 0 || r >= size)
                    continue;

                ++diagonals;

                if (at (go::index (c, r, size)) == colour)
                    ++friendly;
            }
        }

        return friendly >= diagonals - (diagonals == 4 ? 1 : 0);
    }

    /** What a stone of `colour` on `idx` does to eyes: `made` counts empty
        neighbours it turns into its own eyes, `spoiled` counts eyes it takes from
        the other side - an empty neighbour that side would have made an eye of
        by playing here itself, or an eye on a diagonal the stone turns false. */
    inline void eyeEffects (const go::Board& board, int idx, go::Stone colour, int& made, int& spoiled)
    {
        const int size = board.size();
        const auto enemy = go::other (colour);

        made = 0;
        spoiled = 0;

        go::forEachNeighbour (idx, size, [&] (int q)
        {
            if (board.at (q) != go::Stone::none)
                return;

            if (isEye (board, q, colour, idx, colour))
                ++made;

            if (isEye (board, q, enemy, idx, enemy))
                ++spoiled;
        });

        const int col = go::colOf (idx, size), row = go::rowOf (idx, size);

        for (int dc = -1; dc <= 1; dc += 2)
        {
            for (int dr = -1; dr <= 1; dr += 2)
            {
                const int c = col + dc, r = row + dr;

                if (c < 0 || c >= size || r < 0 || r >= size)
                    continue;

                const int q = go::index (c, r, size);

                if (board.at (q) == go::Stone::none && isEye (board, q, enemy)
                     && ! isEye (board, q, enemy, idx, colour))
                    ++spoiled;
            }
        }
    }
}
