# Go Sequencer

A MIDI step sequencer that reads a **Go (Baduk) board** as its pattern. Play
stones the way you would on a real board — spiral, quadrant, or ring by
ring — and the sequencer turns their position, colour, and lifespan into
notes. Load a real game record (SGF) and watch the pattern rewrite itself as
the game unfolds.

It's a [JUCE](https://juce.com/) plugin, built as a **VST3**,
shaped as a MIDI instrument: it emits notes rather than making sound itself,
so you route its MIDI output to a synth or sampler in your DAW.

For a complete, tutorial-style walkthrough of every control and behaviour,
see **[how-to-use-it.md](how-to-use-it.md)**. This README is the quick
overview.

## Table of contents

- [How it works](#how-it-works)
- [Playhead modes](#playhead-modes)
- [Stone lifespan](#stone-lifespan)
- [Controls](#controls)
- [Loading a game record (SGF)](#loading-a-game-record-sgf)
- [AI self-play](#ai-self-play)
- [Using it in Ableton Live](#using-it-in-ableton-live)
- [Building it](#building-it)
  - [Requirements](#requirements)
  - [Windows (MSVC + CMake)](#windows-msvc--cmake)
  - [macOS / Linux](#macos--linux)
  - [Running the rules-engine tests](#running-the-rules-engine-tests)
- [Project layout](#project-layout)

## How it works

The board is a real Go board — 9×9 or 13×13 — and clicking on it plays a
stone under real Go rules: captures, suicide, and the ko rule all apply (the
last two can be switched off). Nothing about the *sound* depends on
understanding Go, though; you can just click points and listen.

One or more **playheads** step around the board on the sequencer's clock.
Whenever a playhead lands on a point holding a stone, that stone fires a MIDI
note — its column and row pick the pitch region, its colour (black/white) or
its playhead picks the MIDI channel, and how long it's been on the board
decides whether it's still allowed to sound at all.

## Playhead modes

Set with the **Mode** control:

| Mode | Playheads | Path |
|---|---|---|
| **Spiral** | 1 | Clockwise from the top-left corner, winding inward to *tengen* (centre). Black and white are routed on separate channels. |
| **Quads out** / **Quads in** | 4 | One playhead per quadrant, each spiralling around that quadrant's star point (the 3-3 point on 9×9). The four blocks share the middle row/column, so all four heads meet in the centre at once. "Out" winds outward from the star point; "in" winds inward toward it. |
| **Polyrhythm** | 4 (9×9) or 6 (13×13) | One playhead per concentric ring around the board (tengen itself isn't a ring). All heads share the same step clock, but the rings have different lengths — 32/24/16/8 points on a 9×9 — so they drift in and out of phase and only realign every 96 steps (480 on 13×13). Every ring has its own MIDI channel and its own pitch transpose, and they all sound together: the board plays as a chord, not a line. |

## Stone lifespan

A stone doesn't sound forever. **Stone Life** sets how long it keeps firing
once played, counted either in:

- **Steps** of the sequencer clock, or
- **Placements** — stones laid down after it (regardless of tempo).

Once a stone's life runs out it stays on the board — it still blocks the
point and still lives or dies by capture rules — but playheads pass over it
in silence, and the editor draws it faded. Setting **Stone Life** to its
maximum makes stones hold forever ("hold").

Age is per-stone, not global: each stone remembers the count it landed on.
Changing the lifespan slider while the sequencer runs re-evaluates every
stone against the age it's already reached (some go silent, some come back)
rather than resetting the whole board's clock.

## Controls

| Control | What it does |
|---|---|
| **Board** | 9×9 or 13×13. Changing it clears the board. |
| **Mode** | Spiral / Polyrhythm / Quads out / Quads in — see above. |
| **Place** | Alternate / Black / White — who a click plays next. |
| **Rate** | Step clock rate, synced to host tempo (1/1 down to 1/32T). |
| **Free Run** + **Free Tempo** | Run the step clock at its own BPM instead of following the host transport. |
| **Note** | Base pitch; board position offsets from here. |
| **Gate** | Note length as a percentage of one step. |
| **Black/White Channel** | MIDI channel per colour (Spiral mode). |
| **Head 1–6 Channel** | MIDI channel per playhead (multi-head modes). |
| **Black/White Velocity** | Fixed velocity per colour. |
| **Spread** | Semitone transpose per ring (Polyrhythm). |
| **Stone Life** / **Life Counts** | See [Stone lifespan](#stone-lifespan). |
| **AI Self-Play** | Two built-in players write the record instead of loading one — see below. |
| **Game Length** / **Variation** / **Seed** | How long a generated game runs, how far the players stray from their best move, and which run of games you get. |
| **From board** / **Use book** | Take the ten stones you played as the opening every game starts from, or go back to the built-in one. |
| **Ko Rule** | Forbid immediately recapturing the previous position. |
| **Self Capture** | Allow suicide moves (a group played with zero liberties is removed instead of refused). |

**Clicking the board:** left-click an empty point to place a stone; left-click
(or right-click / Shift-click / Alt-click) an occupied point to lift it — this
"eraser" click ignores Go legality, it's just for editing the pattern.

## Loading a game record (SGF)

Drag an `.sgf` file onto the plugin, or use the **Load** button, to replay a
real game onto the board:

- The moves play at their own speed (set by **Game Rate**: 1/4 note up to
  "one lap"), independent of the sequencer's step clock — both run at once,
  so the pattern is continuously rewritten by the game as it plays.
- **Run** / **Loop** start and repeat playback; **◀ ▶** step one move at a
  time; **Unload** clears the record and leaves the board as it stood.
- A sample game is included at
  [`sgf/89706031-145-Gruener123-FloMo.sgf`](sgf/89706031-145-Gruener123-FloMo.sgf)
  to try this with.
- **Wave Replay** is a second pacing option: playback never pauses, but
  every **Wave Gap** moves after a move first landed, whatever's currently
  on that point gets its own lifespan reset — staggered per stone rather
  than a synchronized pulse. With **Loop** on, the record wraps without
  clearing the board, so the wave keeps running until you stop. See
  [how-to-use-it.md](how-to-use-it.md#wave-replay) for the full mechanics.

## AI self-play

Turn on **AI self-play** and the plugin writes the record itself: two players
take the board, game after game, with no file to load and nothing to connect
to. It is the same machinery as an SGF underneath, so **Move Rate**, **Run**,
**Loop**, the position slider and the step buttons all keep working exactly as
they did.

![Six self-play games, one opening](docs/mockups/self-play-games.png)

Every game opens on the **same ten moves** and diverges from the eleventh. That
is the point of it here: the sequencer reads position as pitch, so a fixed
opening is a fixed motif, and the sixty moves after it are a variation on it
that never repeats. Out of the box that opening is not invented either — it is
the first ten moves of [`sgf/nine_dan_9x9_43610191.sgf`](sgf/) on a 9×9 and of
[`sgf/Blackie_BIBA_13x13_25655059.sgf`](sgf/) on a 13×13.

**Or play your own.** Clear the board, click out ten stones with **Place** on
*Alternate*, and press **From board**: those ten become the opening of every
game from then on. **Use book** puts the built-in one back. The opening is a
sequence, not a position — the order decides what gets captured and what is
legal — so it is the order you clicked in that is taken, not the shape left
standing; lifting a stone takes it back out. Ten moves alternating from Black,
legal from an empty board, or the button tells you which one is the problem.
It is saved with the session, and it belongs to the board it was played on: a
9×9 opening doesn't apply to a 13×13, which falls back to the book.

![One run, three games](docs/mockups/self-play.gif)

**The two players.** Black (*Kuro*) plays territorially — connects, extends,
takes the third and fourth lines, fights when there is something to take. White
(*Shiro*) fights — contact, cuts and ataris are worth more to it than shape is.
They are heuristics, not a search or a net: every legal point is scored on
captures, saving its own stones from atari, cutting, connecting, staying near
the last move and keeping off the first line, and one of the best twelve is
drawn. A whole game takes well under a millisecond, on the message thread, one
game ahead of the one playing — so the swap at the end of a game costs the
audio thread nothing.

**The three settings.**

- **Game Length** — 12 to 160 moves. The ten book moves are part of it.
- **Variation** — 0% plays the best point it can see every time, so the run is
  one game repeating. 100% picks freely among the best twelve. The default 35%
  keeps the play recognisable and the games different.
- **Seed** — names the run. The same seed plays the same games in the same
  order, on any machine, and a saved session comes back on the game it was
  left on (it is regenerated, not stored).

All three are read when a game is *written*, so changing one lands on the next
game rather than cutting the current one short. To restart a run immediately,
switch AI self-play off and on.

Loading an .sgf or pressing **Unload** hands the board back and switches
self-play off. **Wave Replay** holds a run on one game while it is on — the
wave is rippling stones the next game would not have played.

**Exporting the games.** `GoAiDump` plays the same two players outside the
plugin and writes ordinary `.sgf` files, which load straight back into it (or
into any Go viewer):

```powershell
cmake --build build --config Release --target GoAiDump
.\build\Release\GoAiDump.exe --games 6 --seed 1 --out sgf\selfplay
```

## Using it in Ableton Live

Go Sequencer is a MIDI instrument, so Live treats it like any other note
generator:

1. Drop **Go Sequencer** onto a MIDI track and put a synth or sampler
   (Live's own instruments work fine) on a **second** track.
2. On the instrument track, set **MIDI From** to *Go Sequencer's track* →
   *Go Sequencer*, and arm/monitor it (or set the track's monitor to *In*).
3. Press play. Clicking stones on the Go Sequencer's board now plays notes
   through the instrument track.

The same routing works in any host that lets you pipe one track's MIDI
output into another (Cubase, Studio One, Reaper, Bitwig, …).

## Building it

### Requirements

- **CMake** ≥ 3.22
- A C++17 toolchain — MSVC (Visual Studio 2022) on Windows, Xcode on macOS,
  or GCC/Clang on Linux
- A [JUCE](https://github.com/juce-framework/JUCE) checkout (JUCE itself is
  **not vendored** in this repo)

### Windows (MSVC + CMake)

```powershell
# 1. Clone JUCE next to this project (default expected location is
#    ../.toolchains/JUCE relative to this folder)
git clone --depth 1 https://github.com/juce-framework/JUCE.git ..\.toolchains\JUCE

# 2. Configure, build, and run the rules tests in one go
.\build.ps1

# ...or also install the built VST3 into your VST3 folder:
.\build.ps1 -Install
```

[`build.ps1`](build.ps1) wraps the raw CMake calls below and prints the
path to the built VST3 when it's done. Useful flags:
`-Config Debug`, `-Clean` (wipe `build/` first), `-Install` (copy the .vst3
into `%CommonProgramFiles%\VST3` or `%LOCALAPPDATA%\Programs\Common\VST3`).

Equivalent by hand:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target GoSequencer_VST3
```

Build output lands under `build/GoSequencer_artefacts/Release/`:

- `VST3/Go Sequencer.vst3` — copy to `C:\Program Files\Common Files\VST3\`
  (or your DAW's VST3 folder) to make it visible to your host.

If your JUCE checkout lives somewhere else, point CMake at it instead of
editing the source:

```powershell
cmake -B build -DJUCE_PATH="C:\path\to\JUCE" -G "Visual Studio 17 2022" -A x64
```

### macOS / Linux

```bash
git clone --depth 1 https://github.com/juce-framework/JUCE.git ../.toolchains/JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GoSequencer_VST3 -j
```

On macOS the VST3 lands in `build/GoSequencer_artefacts/Release/VST3/`; copy
it to `~/Library/Audio/Plug-Ins/VST3/`.

### Running the rules-engine tests

The Go rules (captures, suicide, ko, scoring) live in a header
([`Source/GoBoard.h`](Source/GoBoard.h)) that's plain C++ with no JUCE
dependency, so it can be unit-tested without loading a plugin host. The test
replays the included sample SGF game through the rules engine and checks the
result.

```powershell
cmake --build build --target GoRulesTests --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Project layout

```
GoSequencer/
├── CMakeLists.txt          # Build configuration (JUCE plugin + rules tests)
├── Source/
│   ├── PluginProcessor.*   # Audio/MIDI engine: playheads, clock, params
│   ├── PluginEditor.*      # Plugin UI (sliders, combo boxes, board view)
│   ├── BoardComponent.*    # The clickable Go board widget
│   ├── GoBoard.h           # Standalone Go/Baduk rules engine (no JUCE)
│   ├── GoAI.h              # The two self-play players (no JUCE, no floats)
│   └── SgfParser.h         # Minimal SGF (game record) reader
├── sgf/                    # Sample game records for trying SGF playback
├── tools/
│   └── GoAiDump.cpp        # Plays the two players outside the plugin, writes .sgf
└── tests/
    └── GoRulesTests.cpp    # Rules and self-play tests, run via ctest
```
