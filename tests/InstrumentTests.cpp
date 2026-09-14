//  Standalone checks for the instruments: scales, lanes, drum pads and the
//  notes a stone sends. Builds without JUCE:
//      g++ -std=c++17 -I../Source InstrumentTests.cpp -o instrument_tests

#include "Instruments.h"

#include <cstdio>
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

    using inst::Instrument;
    using inst::Lanes;
    using inst::Scale;

    int at (int col, int row, int size) { return go::index (col, row, size); }

    void testScales()
    {
        std::printf ("scales\n");

        const int major[] = { 0, 2, 4, 5, 7, 9, 11, 12, 14 };
        bool majorRight = true;

        for (int d = 0; d < 9; ++d)
            majorRight = majorRight && inst::semitonesFor (Scale::major, d) == major[d];

        check (majorRight, "major climbs 0 2 4 5 7 9 11 and on into the next octave");
        check (inst::semitonesFor (Scale::major, -1) == -1, "one step under a major root is its seventh, an octave down");
        check (inst::semitonesFor (Scale::minorPentatonic, -1) == -2, "one step under a minor pentatonic root is -2");
        check (inst::semitonesFor (Scale::minorPentatonic, -5) == -12, "five steps down a pentatonic is an octave");
        check (inst::semitonesFor (Scale::chromatic, 13) == 13, "chromatic steps are semitones");
        check (inst::semitonesFor (Scale::hirajoshi, 4) == 8 && inst::semitonesFor (Scale::yo, 2) == 5,
               "hirajoshi and yo have their own steps");

        bool rising = true;

        for (int s = 0; s < inst::scaleCount; ++s)
            for (int d = -30; d < 30; ++d)
                rising = rising && inst::semitonesFor ((Scale) s, d + 1) > inst::semitonesFor ((Scale) s, d);

        check (rising, "every scale rises with every step, above and below the root");
    }

    void testLines()
    {
        std::printf ("lines and lanes\n");

        check (inst::linesAboveMiddle (at (0, 0, 9), 9) == 4, "a 9x9's top line is 4 above the middle");
        check (inst::linesAboveMiddle (at (4, 4, 9), 9) == 0, "tengen's line is the middle");
        check (inst::linesAboveMiddle (at (8, 8, 9), 9) == -4, "a 9x9's bottom line is 4 below");
        check (inst::linesAboveMiddle (at (0, 0, 19), 19) == 9, "a 19x19's top line is 9 above");

        check (inst::laneOf (Lanes::rows, at (3, 8, 9), 9) == 0, "rows count from line 1 at the bottom");
        check (inst::laneOf (Lanes::rows, at (3, 0, 9), 9) == 8, "the top line is the last row");
        check (inst::laneOf (Lanes::columns, at (0, 5, 13), 13) == 0, "columns count from A");
        check (inst::laneOf (Lanes::columns, at (12, 5, 13), 13) == 12, "the right edge is the last column");
        check (inst::laneOf (Lanes::rings, at (0, 3, 9), 9) == 0 && inst::laneOf (Lanes::rings, at (8, 5, 9), 9) == 0,
               "both sides of the edge are ring 0");
        check (inst::laneOf (Lanes::rings, at (4, 4, 9), 9) == 4, "tengen is a 9x9's innermost ring");
        check (inst::laneOf (Lanes::rings, at (9, 9, 19), 19) == 9, "and a 19x19's");
        check (inst::laneCount (Lanes::rings, 9) == 5 && inst::laneCount (Lanes::rows, 13) == 13, "lane counts");
    }

    void testPads()
    {
        std::printf ("drum pads\n");

        bool oneEach = true;

        for (int row = 0; row < 9; ++row)
            oneEach = oneEach && inst::padFor (Lanes::rows, at (2, row, 9), 9, 9) == 8 - row;

        check (oneEach, "nine pads on a 9x9: every line plays its own, line 1 on pad 1");

        check (inst::padFor (Lanes::rings, at (4, 4, 9), 9, 9) == 4, "five rings, nine pads: the rings take pads 1 to 5");
        check (inst::padFor (Lanes::rings, at (4, 4, 9), 9, 3) == 2, "five rings, three pads: the middle is on the last");
        check (inst::padFor (Lanes::rows, at (0, 18, 19), 19, 9) == 0 && inst::padFor (Lanes::rows, at (0, 0, 19), 19, 9) == 8,
               "nineteen lines, nine pads: bottom band on pad 1, top band on pad 9");

        bool single = true;

        for (int i = 0; i < 19 * 19; ++i)
            single = single && inst::padFor (Lanes::columns, i, 19, 1) == 0;

        check (single, "a one pad kit plays that pad everywhere");

        bool inRange = true, everyPadUsed = true;

        for (int size : go::supportedSizes)
        {
            for (int lanes = 0; lanes < inst::laneModeCount; ++lanes)
            {
                for (int pads = 1; pads <= inst::maxPads; ++pads)
                {
                    std::array<bool, inst::maxPads> used {};

                    for (int i = 0; i < size * size; ++i)
                    {
                        const int pad = inst::padFor ((Lanes) lanes, i, size, pads);
                        inRange = inRange && pad >= 0 && pad < pads;

                        if (pad >= 0 && pad < inst::maxPads)
                            used[(size_t) pad] = true;
                    }

                    //  a kit no bigger than the lanes has every pad reached
                    if (pads <= inst::laneCount ((Lanes) lanes, size))
                        for (int p = 0; p < pads; ++p)
                            everyPadUsed = everyPadUsed && used[(size_t) p];

                    //  out of range values are clamped rather than trusted
                    inRange = inRange && inst::padFor ((Lanes) lanes, 0, size, 0) == 0
                                      && inst::padFor ((Lanes) lanes, size * size - 1, size, 99) < inst::maxPads;
                }
            }
        }

        check (inRange, "every point on every board picks a pad inside the kit");
        check (everyPadUsed, "no pad is left out while the board has lanes for it");
    }

    void testNotes()
    {
        std::printf ("notes\n");

        inst::Voicing v;
        v.root = 60;

        const int tengen = at (4, 4, 9), top = at (0, 0, 9), bottom = at (3, 8, 9);

        auto one = [&v] (int idx, int size)
        {
            const auto notes = inst::notesFor (v, idx, size);
            return notes.count == 1 ? notes.pitch[0] : -1;
        };

        v.instrument = Instrument::note;
        v.transpose = 3;
        check (one (top, 9) == 63 && one (bottom, 9) == 63, "Note: every point plays the note, plus the head's spread");

        v.instrument = Instrument::melody;
        v.transpose = 0;
        v.scale = Scale::minorPentatonic;
        check (one (tengen, 9) == 60, "Melody: the middle line plays the note");
        check (one (top, 9) == 70 && one (bottom, 9) == 51, "Melody: the top line is four pentatonic steps up, the bottom four down");

        v.scale = Scale::major;
        check (one (top, 9) == 67, "Melody in major: four steps up is the fifth");

        v.instrument = Instrument::bass;
        check (one (tengen, 9) == 36, "Bass: two octaves under the note on the middle line");

        bool folded = true;

        for (int row = 0; row < 19; ++row)
        {
            const int p = one (at (0, row, 19), 19);
            folded = folded && p >= 36 && p < 48;
        }

        check (folded, "Bass: every line of a 19x19 stays inside one octave");

        v.instrument = Instrument::chord;
        v.scale = Scale::major;

        {
            const auto triad = inst::notesFor (v, tengen, 9);
            check (triad.count == 3 && triad.pitch[0] == 60 && triad.pitch[1] == 64 && triad.pitch[2] == 67,
                   "Chord in major on the middle line: C E G");

            const auto above = inst::notesFor (v, at (0, 3, 9), 9);
            check (above.count == 3 && above.pitch[0] == 62 && above.pitch[1] == 65 && above.pitch[2] == 69,
                   "one line up: D F A, still in the scale");
        }

        v.scale = Scale::chromatic;

        {
            const auto triad = inst::notesFor (v, at (0, 3, 9), 9);
            check (triad.count == 3 && triad.pitch[0] == 61 && triad.pitch[1] == 65 && triad.pitch[2] == 68,
                   "Chord in chromatic: a major triad on the line's semitone");
        }

        v.root = 127;
        v.scale = Scale::major;

        {
            const auto clamped = inst::notesFor (v, top, 9);
            check (clamped.count == 1 && clamped.pitch[0] == 127, "a chord pushed past 127 collapses into one note, not three copies");
        }

        v.root = 60;
        v.instrument = Instrument::drums;
        v.transpose = 7;
        v.lanes = Lanes::rows;
        v.pads = 9;

        check (one (bottom, 9) == 36, "Drums: line 1 plays pad 1, the kick");
        check (one (top, 9) == 49, "Drums: line 9 plays pad 9, the crash");

        v.kit[1] = 40;
        check (one (at (0, 7, 9), 9) == 40, "Drums: a pad plays the note it is set to, spread or not");

        v.pads = 2;
        check (one (top, 9) == 40 && one (bottom, 9) == 36, "Drums: a two pad kit splits the board in half");
    }

    void testKitDefaults()
    {
        std::printf ("kit\n");

        check (inst::defaultKit[0] == 36 && inst::defaultKit[1] == 38 && inst::defaultKit[2] == 42,
               "the default kit starts kick, snare, hat");
        check (std::string (inst::drumName (36)) == "Kick" && std::string (inst::drumName (81)) == "Triangle",
               "drum names cover the General MIDI map");
        check (inst::drumName (34) == nullptr && inst::drumName (82) == nullptr, "and nothing outside it");
        check (inst::voiceCount == 11, "black, white and nine heads");
    }
}

int main()
{
    testScales();
    testLines();
    testPads();
    testNotes();
    testKitDefaults();

    std::printf ("\n%s\n", failures == 0 ? "all checks passed" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
