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

> **Credit — Leela.** The *reading* self-play players are built on the tactical
> ideas of [**Leela**](https://github.com/gcp/Leela), the Go engine by
> Gian-Carlo Pascutto (MIT licence). Despite the "AI" on the tab, they are plain
> algorithms with fixed weights: no neural network, no machine-learning model,
> no training data, and nothing that learns while the plugin runs. See
> [Where the reading players come from](#where-the-reading-players-come-from-leela).
>
> **Credit — KataGo.** The reading players' **8×8** weights were settled offline
> with [**KataGo**](https://github.com/lightvector/KataGo), the Go program by
> David J. Wu (MIT licence), as the judge of their moves. KataGo *is* a
> neural-network program, but it only marked moves on the development machine:
> it is not in the plugin, never plays in it, and what came out is a set of
> fixed numbers. See [The 8×8 weights (KataGo)](#the-88-weights-katago).
>
> **Credit — Monte Carlo tree search.** The *search* players play games out to
> choose a move, using published algorithms: Monte Carlo tree search (Rémi
> Coulom, *Crazy Stone*, 2006), UCT (Levente Kocsis and Csaba Szepesvári, 2006)
> and RAVE with capture-and-escape playouts (Sylvain Gelly, David Silver and the
> *MoGo* team, 2006–2007) — the same family of search
> [Leela](https://github.com/gcp/Leela) uses. They are written from the papers'
> ideas; no program's code is copied. Still no neural network, no model and no
> training: random games, counted. See [The search players](#the-search-players).

## Table of contents

- [How it works](#how-it-works)
- [Playhead modes](#playhead-modes)
- [Stone lifespan](#stone-lifespan)
- [Controls](#controls)
- [Loading a game record (SGF)](#loading-a-game-record-sgf)
- [AI self-play](#ai-self-play)
- [Where the reading players come from (Leela)](#where-the-reading-players-come-from-leela)
  - [The 8×8 weights (KataGo)](#the-88-weights-katago)
- [The search players](#the-search-players)
- [Playing against the AI](#playing-against-the-ai)
- [Using it in Ableton Live](#using-it-in-ableton-live)
  - [One track per channel: the MIDI out port (needs loopMIDI)](#one-track-per-channel-the-midi-out-port-needs-loopmidi)
- [Launchpad X](#launchpad-x)
- [Building it](#building-it)
  - [Requirements](#requirements)
  - [Windows (MSVC + CMake)](#windows-msvc--cmake)
  - [macOS / Linux](#macos--linux)
  - [Running the rules-engine tests](#running-the-rules-engine-tests)
- [Project layout](#project-layout)

## How it works

The board is a real Go board — 9×9, 13×13, 19×19 or 8×8 — and clicking on it plays a
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
| **Quads out** / **Quads in** | 4 | One playhead per quadrant, each spiralling around that quadrant's star point (the 3-3 point on 9×9, 4-4 on 13×13). On a 19×19 the blocks are 10×10 — an even side, with no single centre — so each spiral winds around the four points just inside the 4-4 star point instead. The four blocks share the middle row/column, so all four heads meet in the centre at once — except on an 8×8, whose four 4×4 blocks share no middle and tile the board. "Out" winds outward from the middle of the block; "in" winds inward toward it. |
| **Polyrhythm** | 4 (8×8 or 9×9), 6 (13×13) or 9 (19×19) | One playhead per concentric ring around the board (tengen itself isn't a ring; an 8×8 has none, so its rings cover every point). All heads share the same step clock, but the rings have different lengths — 32/24/16/8 points on a 9×9 — so they drift in and out of phase and only realign every 96 steps (420 on 8×8, 480 on 13×13, 20,160 on 19×19). Every ring has its own MIDI channel and its own pitch transpose, and they all sound together: the board plays as a chord, not a line. |

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
| **Board** | 9×9, 13×13, 19×19 or 8×8 (the size of a Launchpad X's grid). Changing it clears the board. |
| **Mode** | Spiral / Polyrhythm / Quads out / Quads in — see above. |
| **Place** | Alternate / Black / White — who a click plays next. |
| **Rate** | Step clock rate, synced to host tempo (1/1 down to 1/32T). |
| **Free Run** + **Free Tempo** | Run the step clock at its own BPM instead of following the host transport. |
| **Note** | Base pitch; board position offsets from here. |
| **Gate** | Note length as a percentage of one step. |
| **Black/White Channel** | MIDI channel per colour (Spiral mode). |
| **Head 1–9 Channel** | MIDI channel per playhead (multi-head modes). |
| **MIDI out port** | Also sends every note to a MIDI port of your system, channels intact, so each channel can go to its own track in Live. *Off* by default; needs [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) on Windows — see [below](#one-track-per-channel-the-midi-out-port-needs-loopmidi). |
| **Black/White Velocity** | Fixed velocity per colour. |
| **Spread** | Semitone transpose per ring (Polyrhythm). |
| **Stone Life** / **Life Counts** | See [Stone lifespan](#stone-lifespan). |
| **AI Self-Play** | Two built-in players write the record instead of loading one — see below. |
| **Players** | *Reading* (the default) or *Classic* — which generation of the two players writes the games. |
| **Game Length** / **Variation** / **Seed** | How long a generated game runs, how far the players stray from their best move, and which run of games you get. |
| **From board** / **Use book** | Take the ten stones you played as the opening every game starts from, or go back to the built-in one. |
| **Play against** / **They play** | The players answer your moves one at a time instead of writing whole games; choose their colour. **Pass** and **New game** go with it. |
| **Launchpad in** / **out** | Drive a Launchpad X, whose pads show and play the board. **Find Launchpad** picks both ports — see [Launchpad X](#launchpad-x). |
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
- **Run** / **Loop** start and repeat playback; **‹ ›** step one move at a
  time; **Unload** clears the record and leaves the board as it stood.
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
[`sgf/Blackie_BIBA_13x13_25655059.sgf`](sgf/) on a 13×13. There's no 19×19
record to take one from, so the full board opens on a textbook line instead:
the four star points, then the commonest star-point joseki (low approach, small
knight's move, two-space extension) in the upper left and again in the lower
right.

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

**Three kinds of players.** Black is *Kuro* and White is *Shiro* in all of
them, and the rule-based two keep their temperaments: Kuro plays territorially —
keeps its stones safe and connected, takes the third line, fights when there is
something to take — while Shiro fights: ataris, cuts and contact are worth more
to it than shape. **Players** on the AI tab picks who writes the games:

- **Search** — by far the strongest. For every move they play thousands of quick
  games out to the end and choose the move that keeps winning, guided by the
  reading players' judgement of where to look first. A game takes a few seconds
  to work out, in the background — see [The search players](#the-search-players).

- **Reading** (the default for a new instance) — before choosing, they read the
  board as chains of stones: what a move captures or saves, which self-atari is
  a blunder and which a throw-in, whether an atari starts a ladder that catches
  the stones and whether running out of atari runs into one, the vital points
  of small eye spaces, which liberties to fill in a race, which eyes a move
  makes or spoils, and whose area a point already lies in. These ideas come from
  the Go engine [**Leela**](https://github.com/gcp/Leela) — see
  [below](#where-the-reading-players-come-from-leela).
- **Classic** — the original pair: every legal point scored on captures, saving
  stones from atari, cutting, connecting, staying near the last move and keeping
  off the first line, with nothing read ahead.

Neither generation is AI in the machine-learning sense: both are algorithms
with fixed weights, and one of the best twelve points is drawn with the seeded
random number generator. Played out to the end against each other (komi 7.5,
area scoring, variation 35, on seeds the weights were never tuned on), the
reading pair are clearly the stronger players:

| | 9×9, 1000 games each | 13×13, 300 games each |
|---|---|---|
| classic Kuro vs classic Shiro | Black wins 38%, by −5.3 on average | Black wins 38%, −9.0 |
| **reading** Kuro vs classic Shiro | Black wins 58%, +6.8 | Black wins 83%, +32.5 |
| classic Kuro vs **reading** Shiro | Black wins 15%, −26.6 | Black wins 2%, −48.2 |

What you *hear* changes less than how well they play. Over sixty moves both write
records of the same shape — about as many stones left standing, an average jump
of about three points from one move to the next — but the reading pair lose
fewer stones, rarely touch the first line on the larger boards, and wander over
a few more points.

Both generations write their games on the message thread, one game ahead of the
one playing, so the swap at the end of a game costs the audio thread nothing.
Measured with an optimised build, a default 60-move game takes about 1 ms
(classic) or 2 ms (reading) on a 9×9 and about 7 ms on a 19×19; the longest,
160 moves on a 19×19, about 19 ms or 25 ms.

**The four settings.**

- **Players** — *Reading* or *Classic*, as above.
- **Game Length** — 12 to 160 moves. The ten book moves are part of it.
- **Variation** — 0% plays the best point it can see every time, so the run is
  one game repeating. 100% picks freely among the best twelve. The default 35%
  keeps the play recognisable and the games different.
- **Seed** — names the run. The same seed plays the same games in the same
  order, on any machine, and a saved session comes back on the game it was
  left on (it is regenerated, not stored). The players are part of that name:
  the two generations play different games from one seed, and a session saved
  before there was a choice comes back with the classic pair.

All four are read when a game is *written*, so changing one lands on the next
game rather than cutting the current one short. To restart a run immediately,
switch AI self-play off and on.

Loading an .sgf or pressing **Unload** hands the board back and switches
self-play off. **Wave Replay** holds a run on one game while it is on — the
wave is rippling stones the next game would not have played.

**Exporting the games.** `GoAiDump` plays the same players outside the plugin
and writes ordinary `.sgf` files, which load straight back into it (or into any
Go viewer). It plays the classic pair unless `--players reading` says otherwise:

```powershell
cmake --build build --config Release --target GoAiDump
.\build\Release\GoAiDump.exe --games 6 --seed 1 --players reading --out sgf\selfplay
```

## Where the reading players come from (Leela)

The reading players owe their knowledge of Go tactics to
**[Leela](https://github.com/gcp/Leela)**, the open-source Go engine by
Gian-Carlo Pascutto (MIT licence, © 2007–2020), whose code later became the
starting point of Leela Zero. Leela's source was studied for this; none of it
was copied. Each idea taken from it was written again from scratch — in whole
numbers, on the plugin's own rules engine — in
[`Source/GoTactics.h`](Source/GoTactics.h) and [`Source/GoAI.h`](Source/GoAI.h).

**What was taken from Leela** — the facts its move generator looks at when it
decides which moves are worth trying:

- how many stones a move captures, and how many of its own it saves from atari —
  and that a rescue which runs into a working ladder saves nothing;
- self-atari, told apart from a throw-in against a group that is short of
  liberties itself, and from handing over a whole group;
- ladders, read out both for the side giving atari and for the side running;
- liberty races (semeai): which liberties to fill when neither side can gain any;
- the vital points of the eye shapes that can be killed — the straight and bent
  three, the pyramid four, the bulky and crossed five, the rabbity six;
- Bouzy's influence map (five dilations, twenty-one erosions), which Leela uses
  to tell whose area a point is.

**What was left out** — what actually makes Leela a strong program: its Monte
Carlo tree search (thousands of simulated games for every move), its pattern
tables learned from game records, and its neural networks trained on recorded
games. They take far more work than a sequencer should spend between two notes,
and they rest on floating point and learned data that could not keep the
promise that a seed names the same game on every machine.

**No AI, no training.** The "AI" on the tab means that the plugin plays both
sides of a game by itself. It does not mean artificial intelligence in the
machine-learning sense, and neither pair of players uses any. They are
algorithms: each legal point gets a score from rules like the ones above, the
reading pair also read ladders out move by move, and one of the best points is
drawn with the seeded random number generator. There is no neural network, no
model and no training data, and nothing learns — not while you play, not
between sessions. The weights are ordinary numbers in
[`Source/GoAI.h`](Source/GoAI.h). The reading pair's were set by hand and then
adjusted once, offline, by playing thousands of test games between versions and
keeping the changes that won (`tools/GoAiTune.cpp`, a local tool like
`GoAiDump`); after that they were fixed, which is also what keeps every seed
reproducible. The 8×8 has its own reading weights, adjusted the same one-at-a-time
way but with KataGo judging the moves — described next.

### The 8×8 weights (KataGo)

An 8×8 game is a close fight over a few dozen points, and the weights tuned on
9×9 and 13×13 were not made for it. So the reading pair got a second set for the
8×8 only, settled offline with [**KataGo**](https://github.com/lightvector/KataGo)
as the judge.

**How.** Kuro and Shiro played a few hundred 8×8 games (half from the book,
half from a few random stones). For every position in them, KataGo scored every
move that could have been played there, once: how many points each gives away
against the best of them. A set of weights is then worth the points its players
are *expected* to give away: in each position, the exact chance of each move
under the plugin's own draw at variation 35, times that move's cost. One weight
at a time was scaled — the same factor for Kuro and Shiro, never below a quarter
or above four times its 9×9 value, so both temperaments survive — and a change
was kept while that number fell. Three rounds, each on positions the previous
round's players actually reached (about 92,000 positions and 2.4 million KataGo
evaluations in all), with every fourth game held back to check the gains carry
over. The tools are `tools/GoAiKata8.cpp` and `tools/katago8/tune8.py`, local
like `GoAiTune`.

**What changed.** Rescuing stones from atari and not being left in a working
ladder count for far more on an 8×8; staying near the last move, extending and
touching the other side's stones count for far less.

**How much better.** Played out on fresh seeds and scored by KataGo move by move
(variation 35, 2000 games each):

| 8×8 | points given away per move | Black wins |
|---|---|---|
| classic Kuro vs classic Shiro | 6.68 | 38% |
| reading, 9×9 weights | 5.71 | 40% |
| **reading, 8×8 weights** | **5.46** | 47% |

Head to head, the tuned players beat the ones they started from, from either colour
(2000 games each, fresh seeds, komi 7.5, area scoring):

| 8×8 | Black wins | Black's margin |
|---|---|---|
| 9×9-weight Kuro vs 9×9-weight Shiro | 42.8% | −2.3 |
| **8×8** Kuro vs 9×9-weight Shiro | 48.5% | +0.3 |
| 9×9-weight Kuro vs **8×8** Shiro | 37.7% | −5.5 |

The gain is real but modest — these are still simple players, and KataGo would
beat either set by the whole board.

**What KataGo is not doing.** KataGo is a neural network program, and it was
used only as a marker, on the development machine, before release. It is not
shipped with the plugin, not loaded by it and never asked anything while you
play. It did not write the weights either: the search over them is plain
arithmetic. The 8×8 players are the same integer scoring rules as on every other
board, with different fixed numbers in [`Source/GoAI.h`](Source/GoAI.h) — no
network, no model, nothing that learns — and a seed still names the same game on
every machine.

## The search players

The reading players find good moves — KataGo's best move is nearly always among
their top three — but they cannot tell which of those is right, because that
depends on what happens several moves later. Reading further with the same rules
does not help; that was measured. The search players ask the board instead.

**How.** From the position, they play a few thousand fast games to the end. In
each, a move is a random legal point that does not fill the player's own eye —
except that a group left in atari by the last move is captured or rescued, and a
move that would hand over a group of its own is avoided. They count who won.
Moves that keep winning are explored more deeply (Monte Carlo tree search), and
every move also learns from the games in which the same side played it later on
(RAVE), which is what makes a few thousand games enough. At the top of the tree
the reading players' scores decide which points to try first. **Variation** then
draws among the moves the search played most, weighted by how often.

**Still no AI in the machine-learning sense.** There is no neural network, no
model and no training data, and nothing learns: every answer is random games,
counted. It stays integer-only and deterministic — a position, a seed and a
playout *count* name one answer on every machine and with either compiler (the
tests pin it) — so a saved session still brings its games back from the seed.
The work is split over four independent trees on four threads and their counts
added up, so the number of cores changes how long it takes, never the result.

**Time.** A search takes seconds, so it runs on a background thread and never
blocks the host. Budgets are counts sized on the development laptop (Intel
i7-11850H) for about **5 seconds per self-play game** and about **1 second per
answer** in a match:

- switching self-play on, or loading a session with a run, shows an empty board
  and "the search players are thinking" until the game is ready — 1 to 7
  seconds depending on board and game length; the next game is worked out while
  this one plays;
- in a match it stays their move ("their move - thinking") for about a second,
  and the board and the pads refuse a stone until the answer lands;
- the audio thread is untouched: it only ever swaps in finished games.

**How much stronger.** Against the reading players, at variation 35, from both
colours, played to the end (komi 7.5, area scoring), with the budgets the plugin
uses:

| Board | Self-play budget (≈5 s a game) | Match budget (≈1 s a move) |
|---|---|---|
| 8×8 | wins 40 of 40, by +40.7 | — |
| 9×9 | wins 40 of 40, by +49.0 | wins 20 of 20, by +58.9 |
| 13×13 | wins 20 of 20, by +69.4 | — |
| 19×19 | wins 12 of 12, by +86.3 | wins 8 of 8, by +151.0 |

Judged move by move by KataGo on the 8×8 positions held out from the weight
tuning, the points a player gives away per move fall from 3.75 (reading players'
best move) to 2.99 with the self-play budget and 2.81 with the match budget. That
is a large step for these players and still far from strong play: no rank has
been measured, so none is claimed.

## Playing against the AI

Switch on **Play against** on the AI tab and the same players answer your
moves one at a time instead of writing whole games; **They play** picks their
colour. Their answer lands as soon as you place a stone. **Pass** and **New
game** do what they say, and two passes in a row end the game. Stones can't be
lifted during a game, and a game in progress — whose move it is included — is
saved with the session. More in
[how-to-use-it.md](how-to-use-it.md#playing-against-the-ai).

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

### One track per channel: the MIDI out port (needs loopMIDI)

The routing above can't split the channels apart in Live. **Live puts every
note a plugin sends onto channel 1** before another track receives it, so the
playheads (or black and white) can't go to separate tracks, or to the separate
parts of a multitimbral instrument like SINE Player or Kontakt. The channels
leave Go Sequencer correctly; Live drops them.

A MIDI *port* keeps them, and Live can filter a port by channel. So Go
Sequencer can send its notes to a port as well as to the host:

1. **Requirement for this feature only:** install and start
   **[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)**
   (Windows, free) and create a port, e.g. `GoSeq`. Nothing else in the plugin
   needs it.
2. In Live's *Preferences → Link, Tempo & MIDI*, turn **Track** on for the
   `GoSeq` **input**.
3. On Go Sequencer's **Channels** tab, set **MIDI out port** to `GoSeq`.
4. For each channel, make a MIDI track with **MIDI From** → `GoSeq` →
   **Ch. N** and Monitor **In**. Put the instrument on that track, or set
   **MIDI To** to a multitimbral instrument's track and pick the channel.

Freeze and Export don't capture the port (they render offline), so record the
tracks into clips first. The full walkthrough is in
[how-to-use-it.md](how-to-use-it.md#one-track-per-channel-the-midi-out-port-needs-loopmidi).

## Launchpad X

A Novation **Launchpad X** can be the board. On an **8×8** board — the size of
its grid — the pads show the stones and the playheads, a press plays a stone,
and the buttons round the edge run the sequencer: step rate, the loaded game,
run and free run, playing against the AI, pass, loop and more.

There's nothing to set up on the Launchpad: no Custom Mode, no Novation
Components. Go Sequencer puts it into Programmer Mode itself and hands it back
when it lets go.

1. In Live's *Settings → Link, Tempo & MIDI*, set the Launchpad X control
   surface to *None* and switch Track, Sync and Remote off for the Launchpad's
   ports, so Live leaves the device to Go Sequencer.
2. On Go Sequencer's **Pads** tab, press **Find Launchpad**. On Windows it picks
   `MIDIIN2` / `MIDIOUT2 (LPX MIDI)` — the plain `LPX MIDI` listed beside them is
   the DAW port, which the pads don't use. The board becomes 8×8 — straight away
   if it's empty, otherwise when you press **Use 8 x 8**.

The button map and the rest are in
[how-to-use-it.md](how-to-use-it.md#11-the-launchpad-x-the-pads-tab).

## Building it

### Requirements

- **CMake** ≥ 3.22
- A C++17 toolchain — MSVC (Visual Studio 2022) on Windows, Xcode on macOS,
  or GCC/Clang on Linux
- A [JUCE](https://github.com/juce-framework/JUCE) checkout (JUCE itself is
  **not vendored** in this repo)

Running the plugin needs nothing else, except
[loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html) on Windows if
you use the optional **MIDI out port** (see
[above](#one-track-per-channel-the-midi-out-port-needs-loopmidi)).

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

- `VST3/GoSequencer.vst3` — copy to `C:\Program Files\Common Files\VST3\`
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
dependency, so it can be unit-tested without loading a plugin host.

```powershell
cmake --build build --target GoRulesTests --config Release
ctest --test-dir build -C Release --output-on-failure
```

## Project layout

```
GoSequencer/
├── CMakeLists.txt          # Build configuration (JUCE plugin as VST3 + Standalone, rules tests)
├── Source/
│   ├── PluginProcessor.*   # Audio/MIDI engine: playheads, clock, params
│   ├── PluginEditor.*      # Plugin UI (sliders, combo boxes, board view)
│   ├── BoardComponent.*    # The clickable Go board widget
│   ├── MidiPortOut.*       # The optional MIDI out port: notes to a system port, channels intact
│   ├── LaunchpadSurface.*  # The Launchpad X: its pads show and play the board
│   ├── LaunchpadMap.h      # Which pad is which point (no JUCE)
│   ├── GoBoard.h           # Standalone Go/Baduk rules engine (no JUCE)
│   ├── GoAI.h              # The self-play players, classic and reading (no JUCE, no floats)
│   ├── GoTactics.h         # What the reading players read: chains, ladders, eye shapes, areas
│   └── SgfParser.h         # Minimal SGF (game record) reader
├── claude_instructions/    # Architecture notes for AI coding agents, and the Launchpad X protocol
├── sgf/                    # Sample game records for trying SGF playback
├── tools/
│   └── GoAiDump.cpp        # Plays the two players outside the plugin, writes .sgf
└── tests/
    └── GoRulesTests.cpp    # Rules, self-play and pad-mapping tests, run via ctest
```
