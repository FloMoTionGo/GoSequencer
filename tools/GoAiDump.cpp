//  Plays the two self-play players outside the plugin and writes what they did.
//
//  It exists for two reasons: the records it writes are ordinary .sgf files, so
//  a run the sequencer produced can be dropped back onto the plugin (or onto any
//  Go viewer) and studied, and the JSON it writes is what the mockup animations
//  in docs/mockups are drawn from - so those show real output rather than an
//  illustration of it.
//
//  Builds without JUCE:
//      g++ -std=c++17 -I../Source GoAiDump.cpp -o goaidump
//
//      goaidump --games 6 --seed 1 --moves 60 --variation 35 --out out/ --json out/ai-games.json

#include "GoAI.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    std::string sgfPoint (int idx, int size)
    {
        const char col = (char) ('a' + go::colOf (idx, size));
        const char row = (char) ('a' + go::rowOf (idx, size));
        return std::string { col, row };
    }

    std::string toSgf (const sgf::Game& game, std::uint32_t seed, int gameNumber)
    {
        std::string out = "(;FF[4]GM[1]CA[UTF-8]";
        out += "SZ[" + std::to_string (game.size) + "]";
        out += "PB[" + game.blackName + "]BR[" + game.blackRank + "]";
        out += "PW[" + game.whiteName + "]WR[" + game.whiteRank + "]";
        out += "GN[Go Sequencer self-play, game " + std::to_string (gameNumber)
             + ", seed " + std::to_string (seed) + "]";
        out += "\n";

        for (size_t i = 0; i < game.moves.size(); ++i)
        {
            const auto& m = game.moves[i];
            out += ";";
            out += (m.colour == go::Stone::black ? "B[" : "W[");
            out += (m.isPass ? "" : sgfPoint (m.index, game.size));
            out += "]";

            if ((i + 1) % 10 == 0)
                out += "\n";
        }

        out += ")\n";
        return out;
    }

    /** The board after every move, as one "012" string per position - the same
        shape go::Board::toString() uses, so the animation can step through it
        without knowing the rules. */
    std::vector<std::string> positions (const sgf::Game& game)
    {
        std::vector<std::string> frames;
        go::Board board { game.size };

        frames.push_back (board.toString());

        for (const auto& m : game.moves)
        {
            if (! m.isPass)
                board.play (m.index, m.colour, true, false);

            frames.push_back (board.toString());
        }

        return frames;
    }

    int intArg (int argc, char** argv, const char* name, int fallback)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::strcmp (argv[i], name) == 0)
                return std::atoi (argv[i + 1]);

        return fallback;
    }

    std::string stringArg (int argc, char** argv, const char* name, const std::string& fallback)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::strcmp (argv[i], name) == 0)
                return argv[i + 1];

        return fallback;
    }
}

int main (int argc, char** argv)
{
    goai::Settings settings;
    settings.size          = intArg    (argc, argv, "--size", 9);
    settings.moves         = intArg    (argc, argv, "--moves", 60);
    settings.variation     = intArg    (argc, argv, "--variation", 35);

    const int games        = intArg    (argc, argv, "--games", 6);
    const auto base        = (std::uint32_t) intArg (argc, argv, "--seed", 1);
    const auto outDir      = stringArg (argc, argv, "--out", "");
    const auto jsonPath    = stringArg (argc, argv, "--json", "");

    std::string json = "{\n";
    json += "  \"size\": " + std::to_string (settings.size) + ",\n";
    json += "  \"opening\": " + std::to_string (goai::openingLength) + ",\n";
    json += "  \"variation\": " + std::to_string (settings.variation) + ",\n";
    json += "  \"baseSeed\": " + std::to_string (base) + ",\n";
    json += "  \"games\": [\n";

    for (int g = 0; g < games; ++g)
    {
        settings.seed = goai::gameSeed (base, g);

        const auto game = goai::generate (settings);
        const auto frames = positions (game);

        int captures = 0;
        {
            go::Board board { game.size };

            for (const auto& m : game.moves)
                if (! m.isPass)
                {
                    board.play (m.index, m.colour, true, false);
                    captures += board.lastCaptureCount();
                }
        }

        std::printf ("game %d  seed %u  %d moves  %d stones standing  %d captured\n",
                     g + 1, settings.seed, (int) game.moves.size(),
                     (int) std::count_if (frames.back().begin(), frames.back().end(),
                                          [] (char c) { return c != '0'; }),
                     captures);

        if (! outDir.empty())
        {
            const auto path = outDir + "/selfplay-" + std::to_string (g + 1) + ".sgf";
            std::ofstream file (path);

            if (file)
                file << toSgf (game, settings.seed, g + 1);
            else
                std::cerr << "could not write " << path << "\n";
        }

        json += "    { \"seed\": " + std::to_string (settings.seed);
        json += ", \"captures\": " + std::to_string (captures);
        json += ", \"moves\": [";

        for (size_t i = 0; i < game.moves.size(); ++i)
        {
            json += (i ? ", " : "");
            json += std::to_string (game.moves[i].index);
        }

        json += "], \"frames\": [";

        for (size_t i = 0; i < frames.size(); ++i)
            json += (i ? ", \"" : "\"") + frames[i] + "\"";

        json += "] }";
        json += (g + 1 < games ? ",\n" : "\n");
    }

    json += "  ]\n}\n";

    if (! jsonPath.empty())
    {
        std::ofstream file (jsonPath);

        if (file)
            file << json;
        else
            std::cerr << "could not write " << jsonPath << "\n";
    }

    return 0;
}
