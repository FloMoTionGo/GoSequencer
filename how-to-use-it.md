# Go Sequencer — Complete Instructions

A tutorial-style walkthrough of every control and behaviour in the Go
Sequencer plugin, plus how to build it from source. For a quick overview and
a one-page control table, see [README.md](README.md) — this document goes
deeper into *how* and *why* each feature behaves the way it does.

> The screenshots below are mockups generated with
> [`docs/mockups/generate.py`](docs/mockups/generate.py) rather than captured
> from a running build, and they still show the plugin's earlier dark,
> single-column layout. The editor is now light and tabbed — the board on the
> left, the controls split across the **Sequencer**, **Board**, **Channels**,
> **Game** and **AI** tabs on the right — so read them for what each control
> does, not for where it sits on screen.

## Table of contents

1. [First launch](#1-first-launch)
2. [The board and the header](#2-the-board-and-the-header)
3. [Placing and lifting stones](#3-placing-and-lifting-stones)
4. [The Sequencer and Board tabs](#4-the-sequencer-and-board-tabs)
   - [Step rate, Note, Gate, Free Tempo](#step-rate-note-gate-free-tempo)
   - [Mode, Spread, Stone Life, Life Counts](#mode-spread-stone-life-life-counts)
   - [Board size, Place](#board-size-place)
   - [Ko rule, Self capture, Free run, Show path, Clear board](#ko-rule-self-capture-free-run-show-path-clear-board)
5. [The Channels tab](#5-the-channels-tab)
6. [The Game and AI tabs](#6-the-game-and-ai-tabs)
   - [AI self-play](#ai-self-play)
   - [Wave Replay](#wave-replay)
7. [Playhead modes explained in depth](#7-playhead-modes-explained-in-depth)
8. [Stone lifespan in depth](#8-stone-lifespan-in-depth)
9. [Go rules reference](#9-go-rules-reference)
10. [Routing MIDI out (Ableton Live and others)](#10-routing-midi-out-ableton-live-and-others)
11. [Saving and recalling sessions](#11-saving-and-recalling-sessions)
12. [Building it from source](#12-building-it-from-source)
    - [Requirements](#requirements)
    - [Windows](#windows)
    - [macOS / Linux](#macos--linux)
    - [Running the rules-engine tests](#running-the-rules-engine-tests)
    - [Troubleshooting the build](#troubleshooting-the-build)
13. [Installing the built plugin](#13-installing-the-built-plugin)
14. [Project layout reference](#14-project-layout-reference)

---

## 1. First launch

Go Sequencer is a **MIDI instrument** plugin (VST3). It produces no audio
of its own — it emits MIDI notes, so it needs a host that routes those
notes into a synth/sampler track feeding something that makes sound.

When the window opens you'll see two columns.

On the left, the **Go board** itself — the large clickable area, as big as
the window's height allows — with the two dropdowns that decide what a click
on it does underneath: **Board** (its size) and **Place** (who plays next).
They stay there whichever tab is open.

On the right, top to bottom:

- A **header**: the plugin name, a small stone swatch showing the colour the
  *next* click will place, and under it a status line (step count, capture
  tally, transport state).
- A row of five **tabs**, and under it the controls of the open one:
  - **Sequencer** — step rate, note, gate, playhead mode, stone life, free
    run and spread.
  - **Board** — the rule switches, Show path, Clear board, and a reminder of
    how to interact with the board.
  - **Channels** — both velocities and every MIDI channel assignment.
  - **Game** — loading, running and scrubbing an SGF record, and Wave Replay.
  - **AI** — self-play and the opening it starts from.

Only one tab is shown at a time and every tab takes the same space, so
switching never resizes the window; the tab you left open is saved with the
session. The window is resizable (drag the corner), and the board grows with
it.

## 2. The board and the header

The board draws every point as a light grid intersection. Placed stones
render as flat black or white discs. A stone whose lifespan has run out
(see [§8](#8-stone-lifespan-in-depth)) is drawn **faded** — it's still there
for the rules, just silent.

**Header status line** (under the plugin name, updates live):

```
step 14/81  ·  captured  black 3  white 1  ·  stopped
```

- `step N/total` — where the (first) playhead currently sits in its cycle.
- An extra field appears in multi-head modes: `4 quadrants` or `N rings`.
- `captured black X white Y` — how many stones of each colour have been
  lifted by capture since the board was last cleared.
- `move N/total` — appears once a game record is loaded, showing position
  in the SGF.
- `running` / `stopped` — whether the step clock is currently advancing
  (see [Free run](#ko-rule-self-capture-free-run-show-path-clear-board)
  below for what makes it run).

Whenever you take an action that needs feedback — a captured/illegal move,
a loaded file, a cleared board — a short message replaces the status line
in the header for about 4–5 seconds, in the accent colour.

## 3. Placing and lifting stones

| Action | Result |
|---|---|
| **Left-click** an empty point | Plays a stone there, if it's a legal Go move (see [§9](#9-go-rules-reference)). Who it plays (black/white) is set by **Place**. |
| **Left-click** an occupied point | Lifts that stone. This is the sequencer's eraser — it ignores Go legality entirely, it's just pattern editing. |
| **Right-click**, or **Shift-click**, or **Alt-click** any point | Also lifts a stone if one is there (an alternate gesture for the same eraser, useful if your right-click is bound elsewhere). |
| Attempt an illegal move (suicide, ko, occupied point) | Nothing is placed; the point **flashes red** briefly and an explanation appears in the header (e.g. *"ko: that would repeat the previous position"*). |

![Placing, lifting, and illegal moves](docs/mockups/board-interaction.png)

A stone you place is timestamped internally the moment it lands (see
[§8](#8-stone-lifespan-in-depth)), so its lifespan always counts from when
*it* was placed, not from when the transport started.

## 4. The Sequencer and Board tabs

![The sequencer controls, in the earlier single-column layout](docs/mockups/sequencer-panel.png)

The **Sequencer** tab holds the clock and the pitch: step rate, note, gate,
mode, stone life and life counts, free run and free tempo, and spread. The
**Board** tab holds the rules and the board's own switches: Ko rule, Self
capture, Show path and Clear board. **Board** size and **Place** are not on a
tab at all — they sit under the board, since they decide what a click on it
does.

### Step rate, Note, Gate, Free Tempo

- **Step rate** — how often the clock advances, as a musical division
  synced to host tempo: `1/1, 1/2, 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T,
  1/32` (default `1/16`). Triplet values are marked `T`.
- **Note** — the base MIDI pitch (shown as a note name, e.g. `C3`). Every
  fired stone's actual pitch is offset from this base by its board
  position (and, in Polyrhythm mode, by that ring's **Spread**).
- **Gate** — note length as a percentage of one step (5%–100%). Short gates
  give a plucky, staccato feel; near 100% lets notes overlap into the next
  step.
- **Free Tempo** — the BPM used when **Free run** is on (20–300 BPM). It's
  ignored while the sequencer is following the host's tempo.

### Mode, Spread, Stone Life, Life Counts

- **Mode** — `Spiral`, `Polyrhythm`, `Quads out`, `Quads in`. Fully
  explained in [§7](#7-playhead-modes-explained-in-depth).
- **Spread** — semitone transpose applied per playhead (−12 to +12). Only
  meaningful once there's more than one playhead, so it's greyed out in
  Spiral mode.
- **Stone Life** — how many steps/placements a stone keeps sounding after
  it's played (1–128; the top value shows as `hold`, meaning stones never
  expire). See [§8](#8-stone-lifespan-in-depth).
- **Life Counts** — whether Stone Life is measured in `Steps` of the
  sequencer clock or `Placements` (stones laid down since).

### Board size, Place

These two sit under the board, not on a tab.

- **Board** — `9 x 9`, `13 x 13` or `19 x 19`. **Changing this clears the
  board** — the sizes address different points, so nothing to carry over.
  The full 19×19 board draws all nine star points and runs its coordinates
  out to `T`; a one-lap Spiral on it is 361 steps, so it suits the faster
  step rates or Polyrhythm/Quads.
- **Place** — who a left-click on an empty point plays next: `Alternate`
  (black/white swap each move, as in a real game), `Black` (always plays
  black), or `White` (always plays white). The header swatch always shows
  the colour that's about to be placed.

### Ko rule, Self capture, Free run, Show path, Clear board

- **Ko rule** (default **on**) — forbids immediately recreating the board
  position that existed right before the previous move (the standard Go
  ko rule, preventing an infinite capture/recapture loop). Turn it off to
  allow ko recaptures freely.
- **Self capture** (default **off**) — when off, playing a stone (or
  group) with zero liberties is refused as *suicide*. Turn it **on** to
  allow it: the stone/group you just played is immediately removed as a
  self-capture instead of being refused. Useful if you want looser,
  non-regulation pattern-editing rules.
- **Free run** (default **off**) — runs the step clock at its own tempo
  (**Free Tempo**, above) instead of following the host's transport and
  tempo. With Free run **off**, the sequencer only advances while the host
  transport is actually playing; pressing stop halts the clock, sends
  all-notes-off, and resets every playhead back to its starting corner
  next time it runs. With Free run **on**, the clock runs continuously
  regardless of the host transport — handy for auditioning the board
  without pressing play in your DAW.
- **Show path** — toggles a faint line on the board tracing each
  playhead's route (the spiral / ring / quadrant path it's following).
  Good for understanding a mode before you commit stones to it.
- **Clear board** — lifts every stone and resets capture counts. This does
  *not* unload a loaded game record; use **Unload** on the Game tab for
  that.

## 5. The Channels tab

Everything that decides how loud a note goes out, and on which channel:

- **Black Velocity** / **White Velocity** — fixed MIDI velocity (1–127)
  sent for every note of that colour, in every mode. Doesn't depend on how
  hard you "click" — this is a step sequencer, not a performance
  controller.
- **Black Channel** / **White Channel** (1–16) — used only in **Spiral**
  mode, where routing is by stone colour.
- **Head 1** … **Head 9** (1–16) — used only in
  **Polyrhythm** / **Quads** modes, one slider per playhead. Quads always
  uses heads 1–4, as does Polyrhythm on a 9×9; a 13×13's Polyrhythm uses
  heads 1–6, and a 19×19's uses all 9. They start out on channels 1–9.

Whichever set doesn't apply to the current **Mode**/**Board** combination is
greyed out (not hidden) so you can see and pre-set values you're not
currently using.

Every channel is assigned **outright** — nothing is derived from another
slider. That means two heads (or black and white) can deliberately share a
channel, or the whole board can sit on channel 1, with no side effects.

## 6. The Game and AI tabs

![The game record controls, in the earlier single-column layout](docs/mockups/game-record-panel.png)

The **Game** tab lets you replay a real Go game's moves onto the board over
time, independent of (and simultaneously with) the step sequencer's own
clock — so the pattern keeps getting rewritten as the game plays.

1. **Load SGF…** opens a file picker filtered to `*.sgf`. Alternatively,
   **drag and drop** an `.sgf` file anywhere onto the plugin window,
   whichever tab is open — the window is outlined while a file is dragged
   over it.
2. Once loaded, the game's **title** and **detail** (players, result, etc.,
   as much as the SGF file provides) appear at the top of the tab.
   A sample file is included at
   [`sgf/89706031-145-Gruener123-FloMo.sgf`](sgf/89706031-145-Gruener123-FloMo.sgf).
3. **Move rate** sets how fast recorded moves are played back, independent
   of the sequencer's own **Step rate**: `1/4, 1/2, 1 bar, 2 bars, 4 bars,
   8 bars`, or `one lap` (the move rate automatically matches however long
   one full pass of the current playhead mode takes).
4. **Run game** starts/stops automatic playback of the record at that rate.
5. **Loop** replays the game from move 0 once it reaches the end (only
   relevant while **Run game** is on).
6. The **position slider** ("move N / total") shows and lets you scrub to
   any point in the game directly — dragging it rebuilds the board from
   move 0 up to that point instantly.
7. **‹** / **›** step exactly one move backward/forward.
8. **Unload** clears the loaded record (the board itself is left as it
   stood — it does *not* clear stones, unlike **Clear board**).

While a game is running, its moves are placed onto the board using the
*same* rules engine as manual clicks (captures, etc. all apply), so
captures from the real game show up in the header's capture tally too.

### AI self-play

Instead of loading a record, the plugin can write one. Turn on **AI self-play**
on the **AI** tab and two players take the board — game after game, for as long
as **Loop** is on, with no file and nothing to connect to. Everything on the
**Game** tab keeps working unchanged: a generated game *is* a record, so
**Move Rate**, **Run game**, **Loop**, the position slider and **‹** / **›**
all behave exactly as they do for an `.sgf`.

![One run of self-play games](docs/mockups/self-play.gif)

**The same opening, every time.** Every game plays the same ten opening moves
and diverges from the eleventh. That is deliberate, and it is the musical point
of the feature: the sequencer turns position into pitch, so a fixed opening is a
fixed motif — you hear the same figure at the start of every game, and then a
variation on it that never repeats. On the two small boards the built-in one is
real play, not a made-up pattern: the first ten moves of
`sgf/nine_dan_9x9_43610191.sgf` on a 9×9, and of
`sgf/Blackie_BIBA_13x13_25655059.sgf` on a 13×13. There is no 19×19 record to
take one from, so the full board opens on a textbook line: Q16, D4, Q4, D16 —
the four star points — then the commonest star-point joseki twice over (low
approach, small knight's move, two-space extension): F17, C14, J17 in the upper
left, R6, O3, R9 in the lower right.

**Playing your own opening.** The two buttons under the three settings on the
**AI** tab set where those ten moves come from:

1. Press **Clear board** (on the **Board** tab), and set **Place** — under the
   board — to *Alternate*.
2. Click out your ten stones — your joseki, a shape you like the sound of,
   anything legal.
3. Press **From board**. The line under the buttons changes from *the book
   line* to *your ten moves*, and every game of every run from then on starts
   with them.
4. **Use book** puts the built-in opening back.

Two things are worth knowing about how that works. An opening is a **sequence,
not a position**: the order the stones went down in decides what gets captured
and what is legal, and the board itself keeps no order. So what gets taken is
the order you clicked in, not the shape left standing — and lifting a stone
takes it back out of that sequence again. And the ten have to be a real opening:
alternating, Black first, and legal one after another from an empty board. If
they are not, the button says which move is the problem rather than quietly
taking something else (the usual cause is **Place** being left on *Black*).

Like the three sliders, a new opening is read when the next game is written, so
it lands on the next game rather than cutting the current one short.

The opening is saved with the session, and it belongs to the board size it was
played on — the same points mean something else on a 13×13. Changing size
therefore falls back to the built-in book for that board, and the line under
the buttons says so.

**The two players** are heuristics rather than a search or a neural net. Each
scores every legal point on a handful of things a beginner would recognise —
stones captured, own stones saved from atari, enemy groups put in atari, cuts,
connections, distance from the last move, distance from its own nearest stone,
which line it sits on — and then draws one of the best twelve. What makes them
two players is the weighting:

| | Black — *Kuro*, territorial | White — *Shiro*, fighting |
|---|---|---|
| wants | connection, calm extensions, the third and fourth lines | contact, cuts, ataris, whatever is happening right now |
| fights | when there is something to take | as a matter of course |

One rule overrides all of it: **neither player will fill its own eye.** Without
that, two heuristic players take their own groups apart in the endgame and the
board empties out — there is a test for exactly this in
[`tests/GoRulesTests.cpp`](tests/GoRulesTests.cpp).

**The three settings** sit with the switch on the **AI** tab:

- **Game length** — 12 to 160 moves, the ten book moves included. 60 is the
  default: long enough for a middlegame fight, short enough that the opening
  comes round again.
- **Variation** — how far the players stray from the best point they can see.
  At **0%** they never stray, so the seed stops mattering and the run becomes
  one game repeating — a strict loop. At **100%** they pick freely among their
  best twelve, which gets loose and takes fewer stones. **35%**, the default,
  keeps the play recognisable and makes every game different by around move 11.
- **Seed** — names the run. The same seed plays the same games in the same
  order, on any machine and in any host.

All three are read when a game is *written*, which happens one game ahead of
the one you are hearing. So changing any of them lands on the next game rather
than cutting the current one short. To start a fresh run immediately, switch
**AI self-play** off and on again — that always begins at game 1.

**How it shares the board.** Loading an `.sgf`, or pressing **Unload**, hands
the board back and switches self-play off, so the switch never sits on while
something else is playing. Changing the board size restarts the run at game 1
on the new board — the points mean something else now. **Wave Replay** holds a
run on one game for as long as it is on: the wave is rippling stones that the
next game would not have played, and swapping the record underneath it would
cut every echo head off at once.

**Saving.** A generated record is not written into the session — the seed, the
game number and your opening if you set one are, and the game is played again
from them when the session opens. It comes back identical, down to the move you left it on. (This is why
there is not a single floating-point number in
[`Source/GoAI.h`](Source/GoAI.h): float arithmetic differs a little between
compilers, and a single near-tie falling the other way would be a different
game from that move on.)

**Getting the games out.** The same two players can be run outside the plugin,
where they write ordinary `.sgf` files you can load back into it, study in a Go
viewer, or keep:

```powershell
cmake --build build --config Release --target GoAiDump
.\build\Release\GoAiDump.exe --games 6 --seed 1 --moves 60 --variation 35 --out sgf\selfplay
```

![Six games from one opening](docs/mockups/self-play-games.png)

### Wave Replay

**Wave Replay** is a second way to pace the same game record. Turn it on
and playback never pauses — moves still land one at a time on **Move
Rate**, straight through to the end — but every **Wave Gap** moves *after*
a move first landed, whatever currently occupies that same point gets its
own individual lifespan reset, as if it had just been placed. A move born
at tick 5 gets refreshed at tick 5 + Wave Gap, again at 5 + 2×Wave Gap, and
so on (up to 16 refreshes deep). Because different moves are born on
different ticks, their resets land on different ticks too — the effect
ripples across the board one stone at a time rather than the whole board
flashing back to life together.

Three things follow from that:

- **Stone Life is kept below Wave Gap.** If a stone could already outlive
  a whole Wave Gap on its own, its reset would be a no-op — the wave
  wouldn't be audible. So while Wave Replay is on, raising **Stone Life**
  past **Wave Gap − 1** pulls it back down automatically, and **Wave Gap**
  can't go below 2 (Stone Life's own minimum is 1, so it always needs
  room underneath).
- **Loop wraps the record without clearing the board.** Each refresh level
  is really a replay head: level *k* is the head that set off *k* × Wave
  Gap moves ago, and since the stone it would play is already standing,
  playing it can only mean refreshing it. Wiping the board at the loop
  point would cut all sixteen of those heads off at once, so with Wave
  Replay on the wrap leaves the stones where they are and the refresh
  levels wrap with the record — at move 1 of a new pass, level 1 reaches
  back into the tail of the previous one. The wave runs unbroken until you
  stop the transport.

  Two side effects worth knowing. The record's own head starts landing on
  points it already owns, and those moves become refreshes too rather than
  placements. And because captured stones *are* lifted, those points get
  genuinely replayed next pass — so the board keeps some churn for a while,
  then settles. Once it's full you're hearing the finished shape ripple
  rather than a game replaying, which is the point of leaving it standing.

With Wave Replay **off**, Loop behaves as it always has: the board is wiped
and the record replays from move 0.

## 7. Playhead modes explained in depth

Set with **Mode**. A "playhead" is an invisible marker stepping through
board points on the sequencer clock; landing on an occupied point fires
that stone's note.

### Spiral (1 playhead)

Starts at the top-left corner and spirals clockwise, winding inward until
it reaches *tengen* (the centre point), then wraps back to the start. This
is the simplest mode — one voice, tracing the whole board once per cycle.
Routing is **by stone colour** (Black Channel / White Channel), which is
why the Channels tab shows those two sliders active here.

![Spiral mode: one playhead winding from a corner to tengen](docs/mockups/spiral-mode.png)

### Quads out / Quads in (4 playheads)

The board is split into four quadrant blocks, each centred on that
quadrant's star point (the 3-3 point on a 9×9, 4-4 on a 13×13). The four
blocks are sized so they exactly meet along the board's middle row and
column — together they cover every point, the shared middle cross twice
(tengen four times).

On a **19×19** the blocks are 10×10 (100 steps each). An even-sided block has
no single centre point, so each spiral winds in to the square of four points
in the middle of its corner — the 5-5 to 6-6 points — and the 4-4 star point
sits on the ring just outside that.

- **Quads out**: each playhead starts at the middle of its block (the star
  point, on 9×9 and 13×13) and spirals *outward* toward the block's edges.
- **Quads in**: each playhead starts at the block's outer edge and spirals
  *inward* toward the middle.

All four heads share the same step clock, so they move in lock-step —
useful for symmetric, four-voice patterns that stay rhythmically aligned.
Routing is **by playhead** (Head 1–4 Channel).

![Quads mode: one playhead per quadrant, meeting at the centre](docs/mockups/quads-mode.png)

### Polyrhythm (4, 6 or 9 playheads)

One playhead per **concentric ring** of the board (tengen is excluded — it
has nowhere to rotate to). A 9×9 board has 4 rings (32, 24, 16, 8 points
around); a 13×13 has 6 rings (48, 40, 32, 24, 16, 8 points); a 19×19 has 9
rings (72, 64, 56, 48, 40, 32, 24, 16, 8 points).

Every ring shares the same step clock, but because the rings have
different lengths, they drift in and out of phase with each other — they
only all land back at their starting point together every 96 steps on a
9×9 (480 on a 13×13, and 20,160 on a 19×19 — at 1/16 and 120 BPM, exactly
42 minutes). Each ring has its own **channel** and its own
**pitch transpose** (Spread), and — critically — **all rings sound
together**, so a fully-populated board plays as an evolving chord rather
than a single melodic line. This is the mode to reach for if you want
long-form, slowly-shifting polyrhythmic textures.

![Polyrhythm mode: one playhead per concentric ring, tengen excluded](docs/mockups/polyrhythm-mode.png)

## 8. Stone lifespan in depth

Every stone has a lifespan, set by **Stone Life** and measured according to
**Life Counts**:

- **Steps** — counts ticks of the sequencer's step clock since the stone
  was placed. A life of 15 means the stone stops sounding once 15 steps
  have passed, regardless of how many (or how few) other stones were
  placed in that time.
- **Placements** — counts *stones placed* (by anyone/anything: clicks or a
  game record) since this one landed. A life of 15 means it falls silent
  once 15 more stones have been placed, however long that took in real
  time.

When a stone's life runs out:

- It **stays on the board** — it still occupies the point, still blocks
  moves there, and is still subject to capture/liberties like any other
  stone.
- Playheads still pass over it and land on it in sequence, but it no
  longer fires a note.
- The editor draws it **faded** so you can see at a glance which stones
  are currently silent.

**Age is a property of the stone, not of the board.** Each stone remembers
the exact step-count or placement-count it was born on. Both counters
(the step clock and the placement counter) only ever climb — they're never
reset or rebased by the transport stopping, looping, or the host jumping
around its timeline. Two consequences:

1. If you **move the Stone Life slider while the sequencer is running**,
   every stone on the board is immediately re-evaluated against the age
   it has *already* reached. Some previously-silent stones can come back
   to life (if you raised the limit past their current age); some
   currently-sounding stones can go silent (if you lowered it below).
2. Stopping the transport, or letting the host loop, does **not** reset
   any stone's age — a stone placed 40 steps ago is still 40 steps old
   the next time playback starts, even after a stop/loop in between.

Setting **Stone Life** to its maximum value shows as **"hold"**: stones
never expire and stay audible for as long as they remain on the board.

## 9. Go rules reference

The board plays by real Go/Baduk rules, applied in this order whenever you
place a stone:

1. **Capture** — any enemy group adjacent to the new stone that's left with
   zero liberties (no empty adjacent points) is removed from the board
   first.
2. **Suicide check** — *after* those captures, if the group you just
   played still has zero liberties, the move is normally illegal
   (**suicide**) and is refused. Turning **Self capture** on instead
   allows it: your own just-played group is removed too.
3. **Ko rule** — if the resulting position would exactly recreate the
   position that stood immediately before the previous move (the classic
   "immediate recapture" loop), the move is refused. Turning **Ko rule**
   off skips this check.

Illegal attempts don't touch the board — the offending point flashes red
and the header explains why (`"no liberties: self capture is not a legal
move"`, `"ko: that would repeat the previous position"`, `"that point is
taken"`).

The **eraser click** (right-click / Shift-click / Alt-click, or left-click
on any occupied point) bypasses all of this — it's not a Go move, it just
lifts whatever's there. It also forgets any pending ko history at that
point, since it isn't a legal-move sequence anymore.

This rules engine ([`Source/GoBoard.h`](Source/GoBoard.h)) is a
self-contained, JUCE-free C++ header, unit-tested independently — see
[Running the rules-engine tests](#running-the-rules-engine-tests).

## 10. Routing MIDI out (Ableton Live and others)

Go Sequencer is built (`IS_SYNTH TRUE`, `NEEDS_MIDI_OUTPUT TRUE`,
`IS_MIDI_EFFECT FALSE`) to sit on an instrument track and produce MIDI,
exactly the shape a host expects a note-generating instrument to have.

**Ableton Live:**

1. Drop **Go Sequencer** onto a MIDI track.
2. Create a **second** MIDI track and load a synth/sampler on it (any of
   Live's own instruments work).
3. On that second track, set **MIDI From** → *(Go Sequencer's track)* →
   *Go Sequencer*, and set the track's monitor to **In** (or arm it).
4. Press play (or turn on **Free run** if you'd rather it play without the
   transport running). Clicking stones on the board — or a loaded game
   record — now plays notes through the second track's instrument.

The same pattern (route one track's MIDI output into another track's
input) works in Cubase, Studio One, Reaper, Bitwig, and most other hosts
that support inter-track MIDI routing.

## 11. Saving and recalling sessions

Every control here is a JUCE `AudioProcessorValueTreeState` parameter, so
your host's normal plugin-state saving covers all of it automatically: the
board (which stones are placed), rate/note/gate, mode, all channel
assignments, rule switches, which tab was left open,
and — separately — the loaded game record and its current scrub position.
Saving your DAW project (or a plugin preset, if your host supports them)
recalls the sequencer exactly as you left it, board included.

A self-play run is saved differently, and more cheaply: the record itself is
not written into the session at all. The **Seed**, the **Game length**, the
**Variation**, the opening and which game of the run was playing are — and the game is
generated again from those when the session opens, landing on the same move
with the same stones on the board. That is only sound because the players are
exactly reproducible; see [AI self-play](#ai-self-play).

## 12. Building it from source

### Requirements

- **CMake** ≥ 3.22
- A C++17 toolchain — MSVC (Visual Studio 2022) on Windows, Xcode on
  macOS, or GCC/Clang on Linux
- A [JUCE](https://github.com/juce-framework/JUCE) checkout — **not**
  vendored in this repo, you clone it yourself

### Windows

```powershell
# 1. Clone JUCE next to this project (default expected path)
git clone --depth 1 https://github.com/juce-framework/JUCE.git ..\.toolchains\JUCE

# 2. Configure + build + run the rules tests, all in one step
.\build.ps1

# ...and to also copy the built VST3 into your VST3 folder:
.\build.ps1 -Install
```

`build.ps1` flags:

| Flag | Effect |
|---|---|
| *(none)* | Configure, build Release, run `GoRulesTests`. |
| `-Config Debug` | Build a Debug configuration instead. |
| `-Clean` | Delete `build/` first, for a from-scratch rebuild. |
| `-Install` | After building, copy the `.vst3` into `%CommonProgramFiles%\VST3` (falls back to `%LOCALAPPDATA%\Programs\Common\VST3` if the first isn't writable). |

Equivalent raw CMake, if you'd rather not use the script:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target GoSequencer_VST3
```

If your JUCE checkout lives somewhere other than
`../.toolchains/JUCE`, point CMake at it explicitly:

```powershell
cmake -B build -DJUCE_PATH="C:\path\to\JUCE" -G "Visual Studio 17 2022" -A x64
```

### macOS / Linux

```bash
git clone --depth 1 https://github.com/juce-framework/JUCE.git ../.toolchains/JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target GoSequencer_VST3 -j
```

### Running the rules-engine tests

[`Source/GoBoard.h`](Source/GoBoard.h) — captures, suicide, ko, scoring —
has no JUCE dependency, so it's tested standalone. The test replays the
included sample SGF game through the engine and checks the result matches.

```powershell
cmake --build build --target GoRulesTests --config Release
ctest --test-dir build -C Release --output-on-failure
```

(`build.ps1` runs this automatically after every build.)

### Troubleshooting the build

- **"JUCE was not found at ..."** — CMake's error tells you the exact path
  it looked in; clone JUCE there, or re-run with `-DJUCE_PATH=...`
  pointing at an existing checkout.
- **Linker errors about the VC++ runtime** — the project statically links
  the CRT (`CMAKE_MSVC_RUNTIME_LIBRARY` = MultiThreaded) so the built
  `.vst3` has no redistributable dependency; make sure you're building
  with the MSVC generator (Visual Studio 17 2022), not Ninja/MinGW, or
  adjust that setting in `CMakeLists.txt` to match your toolchain.
- **DAW doesn't see the plugin after building** — you built it but didn't
  copy/install it; see [§13](#13-installing-the-built-plugin) below, or
  just re-run `.\build.ps1 -Install`.
- **Rescan needed** — after installing, most hosts need a manual
  plugin rescan (Live: Preferences → Plug-Ins → Rescan).

## 13. Installing the built plugin

Build artefacts land under `build/GoSequencer_artefacts/<Config>/`:

- `VST3/Go Sequencer.vst3` — copy (or let `build.ps1 -Install` copy) into:
  - Windows: `C:\Program Files\Common Files\VST3\`
  - macOS: `~/Library/Audio/Plug-Ins/VST3/`
  - Linux: `~/.vst3/`

After copying a new VST3 build over an existing one, rescan plugins in
your DAW (most hosts cache their plugin list).

## 14. Project layout reference

```
GoSequencer/
├── CMakeLists.txt          # Build configuration (JUCE plugin + rules tests)
├── build.ps1                # Windows one-shot configure/build/test/install script
├── Source/
│   ├── PluginProcessor.*   # Audio/MIDI engine: playheads, clock, params, state
│   ├── PluginEditor.*      # Plugin UI: all sliders/combos/buttons, board layout
│   ├── BoardComponent.*    # The clickable Go board widget and its painting
│   ├── GoAI.h              # The two self-play players (no JUCE deps, no floats)
│   ├── GoBoard.h           # Standalone Go/Baduk rules engine (no JUCE deps)
│   └── SgfParser.h         # Minimal SGF (game record) reader
├── sgf/                    # Sample game records for trying SGF playback
├── tools/
│   └── GoAiDump.cpp        # Runs the two players outside the plugin, writes .sgf
└── tests/
    └── GoRulesTests.cpp    # Rules and self-play tests, run via ctest
```

For a shorter overview, see [README.md](README.md).
