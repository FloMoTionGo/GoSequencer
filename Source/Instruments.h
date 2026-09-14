#pragma once

//  What a stone sounds like. Deliberately free of JUCE, like the rules engine,
//  so the step from a point on the board to the notes it sends can be tested on
//  its own (see tests/InstrumentTests.cpp).
//
//  A voice is one of the slots the Channels tab gives a channel to: black and
//  white, which Spiral routes by, and heads 1..9, which the multi head modes
//  route by. Each voice plays one instrument, and keeps a drum kit of up to nine
//  pads for when that instrument is Drums.
//
//      Note    every stone plays the Note - how the sequencer always sounded
//      Melody  the line a stone sits on picks a step of the scale: the middle
//              line plays the Note, each line up one step higher
//      Bass    the melody's step two octaves down, folded into one octave so
//              it never climbs out of the bass register
//      Chord   the melody's step with the third and fifth of the scale on top
//      Drums   a lane of the board - its row, its column or its ring - picks
//              one of the kit's pads

#include <array>

#include "GoBoard.h"

namespace inst
{
    //  saved sessions store these as choice indices, so new ones go on the end
    enum class Instrument { note, melody, bass, chord, drums };
    inline constexpr int instrumentCount = 5;

    enum class Scale { major, minor, dorian, pentatonic, minorPentatonic, hirajoshi, yo, chromatic };
    inline constexpr int scaleCount = 8;

    /** How the board is cut into lanes for a drum kit. Rows count from line 1,
        the bottom one, as the board's own coordinates do; columns from A on the
        left; rings from the edge in to the middle. */
    enum class Lanes { rows, columns, rings };
    inline constexpr int laneModeCount = 3;

    inline constexpr int maxPads  = 9;
    inline constexpr int maxNotes = 3;      //  a chord

    //  the voices: the two colours, then one per playhead
    inline constexpr int black = 0, white = 1, firstHead = 2;
    inline constexpr int voiceCount = firstHead + go::maxRings;     //  11

    /** A kit out of the box, on the General MIDI drum map: kick, snare, closed
        hat, open hat, clap, low tom, high tom, rim and crash - pad 1 first. */
    inline constexpr std::array<int, maxPads> defaultKit { 36, 38, 42, 46, 39, 45, 48, 37, 49 };

    /** A short name for a General MIDI drum note, or nullptr outside the map. */
    inline const char* drumName (int note) noexcept
    {
        static constexpr const char* const names[] =
        {
            "Kick 2", "Kick", "Rim", "Snare", "Clap", "Snare 2", "Tom 1", "Hat",             //  35..42
            "Tom 2", "Pedal hat", "Tom 3", "Open hat", "Tom 4", "Tom 5", "Crash", "Tom 6",   //  43..50
            "Ride", "China", "Ride bell", "Tambourine", "Splash", "Cowbell", "Crash 2",       //  51..57
            "Vibraslap", "Ride 2", "Bongo hi", "Bongo lo", "Mute conga", "Conga hi",         //  58..63
            "Conga lo", "Timbale hi", "Timbale lo", "Agogo hi", "Agogo lo", "Cabasa",        //  64..69
            "Maracas", "Whistle", "Whistle 2", "Guiro", "Guiro 2", "Claves", "Block hi",     //  70..76
            "Block lo", "Mute cuica", "Cuica", "Mute tri", "Triangle"                        //  77..81
        };

        return (note >= 35 && note <= 81) ? names[note - 35] : nullptr;
    }

    //==============================================================================
    struct ScaleShape
    {
        std::array<int, 12> steps {};
        int length = 12;
    };

    inline constexpr ScaleShape shapeOf (Scale scale) noexcept
    {
        switch (scale)
        {
            case Scale::major:           return { { 0, 2, 4, 5, 7, 9, 11 }, 7 };
            case Scale::minor:           return { { 0, 2, 3, 5, 7, 8, 10 }, 7 };
            case Scale::dorian:          return { { 0, 2, 3, 5, 7, 9, 10 }, 7 };
            case Scale::pentatonic:      return { { 0, 2, 4, 7, 9 }, 5 };
            case Scale::minorPentatonic: return { { 0, 3, 5, 7, 10 }, 5 };
            case Scale::hirajoshi:       return { { 0, 2, 3, 7, 8 }, 5 };
            case Scale::yo:              return { { 0, 2, 5, 7, 9 }, 5 };
            case Scale::chromatic:       break;
        }

        return { { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 };
    }

    //  division that rounds down rather than towards zero, so the steps below
    //  the root carry on into the octave below instead of mirroring the ones above
    inline constexpr int floorDiv (int a, int b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    inline constexpr int floorMod (int a, int b) noexcept { return a - floorDiv (a, b) * b; }

    /** Semitones from the root to step `degree` of the scale, counted from 0
        and running on into the octaves above and below. */
    inline constexpr int semitonesFor (Scale scale, int degree) noexcept
    {
        const auto shape = shapeOf (scale);
        const int octave = floorDiv (degree, shape.length);

        return 12 * octave + shape.steps[(size_t) (degree - octave * shape.length)];
    }

    /** How many lines above the board's middle line idx sits: 0 on tengen's
        line, +4 on a 9x9's top line and -4 on its bottom one. */
    inline constexpr int linesAboveMiddle (int idx, int size) noexcept
    {
        return (size - 1) / 2 - go::rowOf (idx, size);
    }

    inline constexpr int laneCount (Lanes lanes, int size) noexcept
    {
        return lanes == Lanes::rings ? (size + 1) / 2 : size;      //  tengen is the innermost ring here
    }

    inline constexpr int laneOf (Lanes lanes, int idx, int size) noexcept
    {
        const int col = go::colOf (idx, size), row = go::rowOf (idx, size);

        switch (lanes)
        {
            case Lanes::columns: return col;
            case Lanes::rings:
            {
                const int fromSide = col < size - 1 - col ? col : size - 1 - col;
                const int fromEnd  = row < size - 1 - row ? row : size - 1 - row;
                return fromSide < fromEnd ? fromSide : fromEnd;
            }
            case Lanes::rows:    break;
        }

        return size - 1 - row;
    }

    /** Which of the kit's first `pads` pads the stone on idx plays, from 0.
        While the board has no more lanes than the kit has pads, lane n plays
        pad n and the pads past the last lane rest; once it has more, the lanes
        are shared out in even bands, the first band on pad 1. */
    inline constexpr int padFor (Lanes lanes, int idx, int size, int pads) noexcept
    {
        const int kit   = pads < 1 ? 1 : (pads > maxPads ? maxPads : pads);
        const int count = laneCount (lanes, size);
        const int lane  = laneOf (lanes, idx, size);

        return count <= kit ? lane : lane * kit / count;
    }

    //==============================================================================
    /** Everything a voice is set to that decides its notes. */
    struct Voicing
    {
        Instrument instrument = Instrument::note;
        Scale scale = Scale::minorPentatonic;
        Lanes lanes = Lanes::rows;
        int root = 60;              //  the Note parameter
        int transpose = 0;          //  the head's Spread, in semitones; drums ignore it
        int pads = maxPads;
        std::array<int, maxPads> kit = defaultKit;
    };

    struct Notes
    {
        std::array<int, maxNotes> pitch {};
        int count = 0;

        /** Adds a pitch kept inside 0..127, unless the clamp has made it one
            that is already there. */
        void add (int p) noexcept
        {
            p = p < 0 ? 0 : (p > 127 ? 127 : p);

            for (int i = 0; i < count; ++i)
                if (pitch[(size_t) i] == p)
                    return;

            if (count < maxNotes)
                pitch[(size_t) count++] = p;
        }
    };

    /** The notes the stone on idx sends through a voice set up this way. */
    inline Notes notesFor (const Voicing& voicing, int idx, int size) noexcept
    {
        Notes notes;

        const int base = voicing.root + voicing.transpose;
        const int step = linesAboveMiddle (idx, size);

        switch (voicing.instrument)
        {
            case Instrument::melody:
                notes.add (base + semitonesFor (voicing.scale, step));
                break;

            case Instrument::bass:
                notes.add (base - 24 + semitonesFor (voicing.scale, floorMod (step, shapeOf (voicing.scale).length)));
                break;

            case Instrument::chord:
                notes.add (base + semitonesFor (voicing.scale, step));

                if (voicing.scale == Scale::chromatic)
                {
                    //  every step is a semitone here, so two steps up would be a
                    //  cluster: a major triad stands in for the scale's own
                    notes.add (base + step + 4);
                    notes.add (base + step + 7);
                }
                else
                {
                    notes.add (base + semitonesFor (voicing.scale, step + 2));
                    notes.add (base + semitonesFor (voicing.scale, step + 4));
                }
                break;

            case Instrument::drums:
                notes.add (voicing.kit[(size_t) padFor (voicing.lanes, idx, size, voicing.pads)]);
                break;

            case Instrument::note:
                notes.add (base);
                break;
        }

        return notes;
    }
}
