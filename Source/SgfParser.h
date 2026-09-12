#pragma once

//  A small SGF (FF[4]) reader, just enough to replay a real game record.
//
//  It follows the main line only: at every branch the first variation wins and
//  the alternatives are skipped, which is what game records from OGS, KGS and
//  Fox produce anyway (they nest every move in its own single-child subtree).
//  Property values are read with escape handling, so comments containing
//  brackets or parentheses cannot derail the scanner.
//
//  Like GoBoard.h this is plain C++ so it can be tested without a host.

#include <cctype>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <vector>

#include "GoBoard.h"

namespace sgf
{
    struct Placement
    {
        go::Stone colour = go::Stone::none;
        int index = -1;         // board index, or -1 for a pass
        bool isPass = false;
    };

    struct Game
    {
        bool valid = false;
        std::string error;

        int size = 19;
        std::string blackName, whiteName, blackRank, whiteRank;
        std::string result, date, gameName, place, komi;

        std::vector<Placement> setup;   // handicap / AB / AW stones, applied before move 1
        std::vector<Placement> moves;   // the main line, passes included

        int moveCount() const { return (int) moves.size(); }

        /** "FloMo (28k) vs Gruener123" - empty when the record carries no names. */
        std::string players() const
        {
            if (blackName.empty() && whiteName.empty())
                return {};

            auto withRank = [] (const std::string& name, const std::string& rank)
            {
                if (name.empty())            return std::string ("?");
                if (rank.empty() || rank == "?") return name;
                return name + " (" + rank + ")";
            };

            return withRank (blackName, blackRank) + " vs " + withRank (whiteName, whiteRank);
        }
    };

    namespace detail
    {
        inline bool isPropertyLetter (char c)
        {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        }

        /** Reads one [value], starting at the '['. Handles the SGF backslash escape. */
        inline std::string readValue (const std::string& text, size_t& i)
        {
            std::string value;
            ++i;                                    // step over '['

            while (i < text.size())
            {
                const char c = text[i];

                if (c == '\\')
                {
                    if (i + 1 < text.size())
                    {
                        value += text[i + 1];
                        i += 2;
                        continue;
                    }

                    ++i;
                    continue;
                }

                if (c == ']')
                {
                    ++i;
                    break;
                }

                value += c;
                ++i;
            }

            return value;
        }

        inline void skipWhitespace (const std::string& text, size_t& i)
        {
            while (i < text.size() && std::isspace ((unsigned char) text[i]) != 0)
                ++i;
        }

        /** SGF point: 'a' is the first column / row, counted from the top left. */
        inline int pointFromValue (const std::string& value, int size, bool& isPass)
        {
            isPass = false;

            if (value.size() < 2)                   // "" is a pass
            {
                isPass = true;
                return -1;
            }

            const int col = value[0] - 'a';
            const int row = value[1] - 'a';

            if (col < 0 || row < 0 || col >= size || row >= size)
            {
                isPass = true;                      // "tt" and friends
                return -1;
            }

            return go::index (col, row, size);
        }
    }

    /** Parses SGF text. Never throws: on failure the returned game has
        valid == false and error set. */
    inline Game parse (const std::string& text)
    {
        using namespace detail;

        Game game;

        //  ---- collect the main line as a flat list of nodes -------------------
        struct Property { std::string key; std::vector<std::string> values; };
        using Node = std::vector<Property>;

        std::vector<Node> nodes;
        std::vector<char> visitedChild;     // has this depth already taken its first child?
        int depth = 0, skipping = 0;
        size_t i = 0;

        while (i < text.size())
        {
            const char c = text[i];

            if (std::isspace ((unsigned char) c) != 0)
            {
                ++i;
                continue;
            }

            if (c == '(')
            {
                ++i;

                if (skipping > 0)
                {
                    ++skipping;
                    continue;
                }

                if (depth < (int) visitedChild.size() && visitedChild[(size_t) depth])
                {
                    skipping = 1;               // a sibling variation: not the main line
                    continue;
                }

                if ((int) visitedChild.size() <= depth)
                    visitedChild.resize ((size_t) depth + 1, 0);

                visitedChild[(size_t) depth] = 1;
                ++depth;

                if ((int) visitedChild.size() <= depth)
                    visitedChild.resize ((size_t) depth + 1, 0);

                visitedChild[(size_t) depth] = 0;
                continue;
            }

            if (c == ')')
            {
                ++i;

                if (skipping > 0)
                    --skipping;
                else if (depth > 0)
                    --depth;

                continue;
            }

            if (c == ';')
            {
                ++i;

                if (skipping == 0)
                    nodes.emplace_back();

                continue;
            }

            if (c == '[')                       // a stray value: consume it safely
            {
                readValue (text, i);
                continue;
            }

            if (isPropertyLetter (c))
            {
                std::string key;

                while (i < text.size() && isPropertyLetter (text[i]))
                    key += text[i++];

                std::vector<std::string> values;

                for (;;)
                {
                    skipWhitespace (text, i);

                    if (i >= text.size() || text[i] != '[')
                        break;

                    values.push_back (readValue (text, i));
                }

                if (skipping == 0 && ! nodes.empty())
                    nodes.back().push_back ({ key, std::move (values) });

                continue;
            }

            ++i;                                // anything else is noise
        }

        if (nodes.empty())
        {
            game.error = "no SGF nodes found";
            return game;
        }

        //  ---- read the root ---------------------------------------------------
        auto findFirst = [] (const Node& node, const char* key) -> const std::string*
        {
            for (const auto& property : node)
                if (property.key == key && ! property.values.empty())
                    return &property.values.front();

            return nullptr;
        };

        if (const auto* sz = findFirst (nodes.front(), "SZ"))
        {
            game.size = std::atoi (sz->c_str());     //  "13" or "13:13"

            if (sz->find (':') != std::string::npos)
            {
                const auto second = std::atoi (sz->substr (sz->find (':') + 1).c_str());

                if (second != game.size)
                {
                    game.error = "non square boards are not supported";
                    return game;
                }
            }
        }

        if (game.size < 2 || game.size > 52)
        {
            game.error = "board size " + std::to_string (game.size) + " is out of range";
            return game;
        }

        auto readInto = [&] (const char* key, std::string& target)
        {
            if (const auto* value = findFirst (nodes.front(), key))
                target = *value;
        };

        readInto ("PB", game.blackName);
        readInto ("PW", game.whiteName);
        readInto ("BR", game.blackRank);
        readInto ("WR", game.whiteRank);
        readInto ("RE", game.result);
        readInto ("DT", game.date);
        readInto ("GN", game.gameName);
        readInto ("PC", game.place);
        readInto ("KM", game.komi);

        //  ---- walk the nodes --------------------------------------------------
        for (const auto& node : nodes)
        {
            for (const auto& property : node)
            {
                const bool isSetup = (property.key == "AB" || property.key == "AW");
                const bool isMove  = (property.key == "B"  || property.key == "W");

                if (! isSetup && ! isMove)
                    continue;

                const auto colour = (property.key == "AB" || property.key == "B")
                                      ? go::Stone::black : go::Stone::white;

                for (const auto& value : property.values)
                {
                    Placement placement;
                    placement.colour = colour;
                    placement.index  = pointFromValue (value, game.size, placement.isPass);

                    if (isSetup)
                    {
                        if (! placement.isPass)
                            game.setup.push_back (placement);
                    }
                    else
                    {
                        game.moves.push_back (placement);
                    }
                }
            }
        }

        if (game.moves.empty() && game.setup.empty())
        {
            game.error = "the record contains no moves";
            return game;
        }

        game.valid = true;
        return game;
    }
}
