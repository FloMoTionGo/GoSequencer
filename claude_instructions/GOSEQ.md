# GoSequencer — agent orientation

Written for an agent picking this project up cold. Dense on purpose: read this instead of
re-exploring. `file:line` anchors are the contract — follow them rather than trusting prose.
Line numbers drift; the symbol names do not.

**What it is.** A VST3 that turns a game of Go into a step sequencer. The board *is* the
pattern: playheads walk it on a path (spiral / rings / quadrants) and every stone they pass
fires a note. A game record (an `.sgf`, or one the built-in players generate) can be replayed
onto the board so the pattern evolves as the game is played.

---

## Build and verify

```
.\build.ps1                 # MSVC, Visual Studio 17 2022 generator, Release. Runs the tests, throws on failure.
.\build.ps1 -Install        # also copies the .vst3 to a system VST3 folder
.\build.ps1 -Clean          # deletes build/
```

The engine is header-only and JUCE-free, so the fast loop is a direct compile — seconds, no host:

```
export PATH="/c/msys64/mingw64/bin:$PATH"
g++ -std=c++17 -O2 -I../Source tests/GoRulesTests.cpp -o gorules.exe -static && ./gorules.exe
```

Do **not** use Ninja or MinGW for the plugin itself — `CMAKE_MSVC_RUNTIME_LIBRARY` pins a static
CRT and only the MSVC generator honours it (`CMakeLists.txt:8-11`).

Formats are VST3 **and Standalone** (`CMakeLists.txt:35`). Standalone exists for hardware work:
the Launchpad needs real pads, and a standalone window opens in a second where a host takes a
minute.

---

## The thread contract

This is the single most expensive thing to re-derive. It is stated at `PluginProcessor.h:64-68`.

| Thread | May touch |
|---|---|
| **audio** (`processBlock` → `renderBlock` → `triggerAt` → `fireCell`) | atomics and parameters only. The board **only** through `SpinLock::ScopedTryLockType`, skipping the block rather than waiting (`PluginProcessor.cpp:1432`). |
| **message** | everything. Takes `boardLock` outright. All board mutation lives here. |
| **device** (`juce::MidiInputCallback`) | a lock-free queue and `triggerAsyncUpdate()`. Never the board, never the heap. |

**Audio → message handoff:** atomic flag + `AsyncUpdater`. `parameterChanged` (`cpp:566`) sets a
`pendingXxx` flag and calls `triggerAsyncUpdate()`; `handleAsyncUpdate` (`cpp:594`) does the real
work. Flags at `PluginProcessor.h:545-549`. A parameter change **may arrive on the audio thread** —
that is why the indirection exists. Copy this pattern for anything new.

**Board mutation, all message-thread-only** (`PluginProcessor.h:104`): `placeStone` (`cpp:499`),
`eraseStone` (`:529`), `clearBoard` (`:553`), `loadSgfText` (`:700`), `clearGame`,
`setGamePosition` (`:1013`), `startAiSelfPlay`.

**`publishBoard()`** (`cpp:460`) is the single writer of the lock-free `stones[]` mirror, called
under the lock from both threads. Anything that changes the board must call it.

**Lock-free read surface** — safe from any thread, use these for mirroring/UI rather than the
board: `stoneAt`, `stoneIsSpent`, `lastMove`, `currentStep`, `headPosition`, `headCellAt`,
`headCount`, `isRunning`, `boardSize`, `gamePosition`, `gameMoveCount`, `colourForNextMove`
(`PluginProcessor.h:115-194`).

---

## Files

| File | What |
|---|---|
| `Source/GoBoard.h` | Rules + geometry. No JUCE, no heap, no floats. Board is runtime-sized, storage always `maxCells`. |
| `Source/GoAI.h` | Two player pairs, integer-only, fully deterministic. Generates whole games. |
| `Source/GoTactics.h` | What the reading players know: chains, ladders, influence, eyes. No playouts, no MCTS — deliberate. |
| `Source/SgfParser.h` | Game records in and out. |
| `Source/LaunchpadMap.h` | Pad ↔ board point. JUCE-free so it is unit-tested. |
| `Source/PluginProcessor.*` | Clocks, parameters, state, the board, MIDI generation. |
| `Source/PluginEditor.*` | Hand-rolled tabs, all controls, the theme. |
| `Source/BoardComponent.*` | The board view, mouse input, and `namespace theme` (all colours). |
| `Source/MidiPortOut.*` | Direct hardware/virtual MIDI out, bypassing the host. **The model for any device class.** |
| `tests/GoRulesTests.cpp` | Plain C++17, no JUCE. The only automated check there is. |
| `tools/` | `GoAiDump` (records → sgf/json), `GoAiTune` (offline weight tuning). **Gitignored.** |

---

## Engine invariants

**Sizes.** `go::supportedSizes { 9, 13, 19, 8 }`, `sizeCount = 4` (`GoBoard.h:18-36`). A size's
index in that array is its **slot**; per-size tables are indexed by it and the `boardSize`
parameter's choice index equals it.

> **Append-only rule.** New sizes — and new entries in *any* `AudioParameterChoice` — go on the
> **end**. A saved session stores the index, so reordering silently changes what old sessions
> mean. This is why 8×8 is last despite being smallest (`PluginProcessor.cpp:30-33`, `:47-48`).

`go::isSupportedSize` and `Board::setSize`/`Board(int)` are the **only** gates on size. Everything
geometric is a size-generic formula.

**Coordinates.** Flat row-major, 0-based, **row 0 is the TOP**. `index(col,row,size)=row*size+col`,
`colOf=idx%size`, `rowOf=idx/size` (`GoBoard.h:44-46`).

**Even boards are the special case.** 8×8 is the only one. No tengen, so: `ringCount = size/2`
covers every point with nothing left over, and the four quadrants **tile** the board instead of
overlapping on a shared middle line. Both formulas were rewritten rather than branched, because
the new form is the same number on an odd board — `size/2 == (size-1)/2` for odd sizes, and
`size - quadSide(size) == quadSide(size) - 1` for odd sizes. `starPoints` returns `-1` for the
fifth point on an even board and `hoshiPoints` skips it.

**No pass, no undo on `Board`.** Undo is replay-from-scratch (`rebuildBoardFromGameLocked`,
`cpp:1044`). Legality has no separate query — you call `play()` and read the `MoveResult`.

**Ko is one-ply** (immediate recapture), not positional superko (`GoBoard.h` `play()`).

### The hard constraint

`testAiClassicUnchanged` (`tests/GoRulesTests.cpp`) pins **six FNV-1a hashes** of generated AI
games — 9/13/19 × classic/reading. Its own comment: *a hash that moves is a saved session broken*,
because a session replays its games from a seed rather than storing them.

**If a hash moves, do not re-baseline it. Find the change that was not a no-op.** Any edit to
`GoAI.h`, `GoTactics.h` or `GoBoard.h` must be argued to be an identity for 9/13/19 *and* then
proven by running the tests.

The AI is size-agnostic (`board.size()`/`cellCount()` everywhere) and deterministic by
construction: xorshift32, an integer softmax table, no floats anywhere, ties broken by index.

---

## Parameters

All through `apvts` (`AudioProcessorValueTreeState`, tree type `"GOSEQ"`). Declared in
`createParameterLayout` (`PluginProcessor.cpp:76-201`), pointers cached in the ctor.

| Group | IDs |
|---|---|
| step clock | `rate` (9 divisions), `note`, `gate`, `tempo`, `freeRun`, `playMode` (Spiral/Polyrhythm/Quads out/Quads in), `ringSpread` |
| board | `boardSize`, `colourMode` (Alternate/Black/White), `koRule`, `selfCapture` |
| stone life | `stoneLife`, `lifeMode` (Steps/Placements) |
| channels | `blackChannel`, `whiteChannel`, `headChannel1..9`, `blackVelocity`, `whiteVelocity` |
| game record | `gameRate`, `gameRun`, `gameLoop`, `waveReplay`, `waveGap` |
| AI | `aiPlay`, `aiMoves`, `aiVariation`, `aiSeed`, `aiPlayers` (Classic/Reading) |

Only nine have listeners (`cpp:253-261`) — **add to both the ctor and dtor** or you leak a
dangling listener.

There is **no transport parameter**: the step clock runs when `freeRun || host is playing`
(`cpp:1360-1363`). `gameRun` is the record's transport, a separate thing.

**Two independent clocks** in `renderBlock` (`cpp:1324-1486`): the step clock (per block,
sample-accurate, free-run countdown or host ppq) and the game-move clock (advances the record
onto the live board under a try-lock).

---

## Persistence — three slots

1. **Automatable** → an APVTS parameter. Persists free via `copyState()`. Append-only for choices.
2. **Machine-local, not automatable** (device names, view state) → a `ValueTree` property on
   `apvts.state`. Precedent and rationale at `PluginProcessor.cpp:53-55`: *"it is a device on this
   machine, not something to automate"*. Existing: `midiOutPort`, `activeTab`, `darkMode`. Rides
   along in `getStateInformation` for free; only the *reopen* needs a hook (`cpp:1653-1664`).
3. **Derived board data** → an XML attribute in `getStateInformation` (`cpp:1494-1535`), read back
   in `setStateInformation`. Existing: `board`, `boardSizeValue`, `gamePosition`, `aiGame`,
   `aiOpening`, `nextColour`, the embedded SGF.

`setStateInformation` injects a missing `aiPlayers` PARAM for old sessions (`cpp:1562-1567`) —
the pattern to copy when adding a parameter that old sessions lack.

---

## Editor

Tabs are **hand-rolled**, not `juce::TabbedComponent`: `enum Tab` (`PluginEditor.h:86`) drives
`tabCount`, the `tabNames` literal (`cpp:334`) and two `std::array`s. `addToTab` / `showTab`
(`cpp:539-564`) just flip visibility. `pinned = -1` means "always visible beside the board".

- The active tab is saved as a **raw int** (`activeTab`) → **append new tabs at the end**.
- `resized()` lays **every** tab into the same rectangle whether visible or not; each block opens
  `auto rows = column;` (`cpp:868-990`).
- Control builders: `setUpCaption/Text/Slider/Combo/Toggle/Button` (`cpp:587-656`).
- Timers: editor 20 Hz, `BoardComponent` 30 Hz (`BoardComponent::timerCallback`). Nothing pushes;
  everything polls - and repaints only what changed: the editor header only when its text or the
  next-stone colour reads differently.
- **The board is two layers.** `GridLayer` (surface, lines, stars, coordinates) is opaque and
  `setBufferedToImage` - drawn again only on resize or `refreshAll()`. `StoneLayer` is repainted
  **cell by cell**: each tick compares a per-point snapshot (`cellState`: stone, spent, last move)
  and the head cells with what was last drawn, and `repaintCell`s the differences; `paintStones` /
  `paintPlayhead` skip cells outside the clip. A new colour scheme or board size must call
  `board.refreshAll()` (the editor does, and the timer catches a missed size or scheme change).
  Anything new drawn on a point must fit `cellBounds` and be part of `cellState`, or it will not be
  repainted. There is no "Show path" any more - removed for performance (2026-09-17).
- `RefreshingComboBox` (`PluginEditor.h:48-60`) re-reads a device list when its popup opens.
- **All colours** live in `namespace theme` (`BoardComponent.h:11-76`), light and dark.
  `stoneBlack`/`stoneWhite` are `const` and fixed in both schemes.

---

## Device layer

`MidiPortOut` (`Source/MidiPortOut.*`) sends the sequencer's notes to a system MIDI port as well
as to the host, because Live flattens plugin MIDI to channel 1 and that destroys the per-colour
and per-playhead channels. Used with loopMIDI.

**Copy its shape for any new device class:**

- Message thread lists, opens and closes. `juce::MidiDeviceListConnection` created **lazily**,
  only when a port is actually wanted (`cpp:43`) — so an instance that uses no port never asks the
  system about devices. That connection is also the reconnect mechanism: a port that is not there
  yet is opened when it appears.
- Matching is by **name**, but `reconnect()` compares `identifier` to avoid a needless reopen.
- `close()` ordering **is** the safety contract (`cpp:84-106`): clear the `open` flag first, then
  `stopTimer()` (which waits out a callback in progress), and only then touch the port.
- Audio→timer queue: `AbstractFifo` + fixed array, **drops when full rather than waiting**.
- A `sounding` bitset so closing never leaves a note hanging.

Constraints specific to `MidiPortOut` and **not** general: it uses a `HighResolutionTimer` (only
16 per process on Windows) and skips anything that is not 1–3 bytes, i.e. no SysEx. Both exist
because it is fed from the audio thread. A class that is not in the signal path should use a plain
`juce::Timer` and may use SysEx freely.

**MIDI input:** the bus exists (`NEEDS_MIDI_INPUT TRUE`, `acceptsMidi()` true) but `renderBlock`
discards it on its first line — `buffer.clear(); midi.clear();` (`cpp:1326-1327`). This is a
generator, not an insert. Device-level input goes through `juce::MidiInput`, not `processBlock`.

`juce_audio_devices` is already linked transitively via `juce::juce_audio_utils`, so
`MidiInput`/`MidiOutput` need **no CMake change**.

---

## Known traps

- **The docs contradict the code on pitch.** `README.md:51-52,92` and `GoAI.h:38-39` say board
  position picks the pitch. It does not. `fireCell` uses `noteParam + head*spread` only
  (`cpp:1197`, `:1300-1303`) — every stone in a given playhead plays the same note. Board position
  decides *when* a note fires, colour decides channel and velocity.
- **A fresh clone cannot configure.** `CMakeLists.txt:84` references `tools/GoAiDump.cpp` but
  `tools/` is gitignored and untracked.
- **The ctest case points at a missing file** (`CMakeLists.txt:78`). `build.ps1` sidesteps it by
  running the exe with no argument. Prefer `build.ps1` over `ctest`.
- **Editor change-detection keys on `headCount()`, not the mode** (`PluginEditor.cpp:1002-1023`).
  Quads out, Quads in and Polyrhythm-on-9×9 all return 4, so switching between them fires no
  refresh. Noted in `ideas.md:22-25`.
- **A board size change clears the board** (`applyBoardSize`, `cpp:643-682`). Anything that
  changes size programmatically is destructive — confirm first.
- **`handPlayed` is not the board.** An opening is a *sequence*, because order decides what is
  captured and what is legal. `setOpeningFromBoard` reads `handPlayed`, not the position
  (`PluginProcessor.h:223-231`).
- **Windows names the Launchpad's DAW interface a bare `LPX MIDI`**, listed above
  `MIDIIN2`/`MIDIOUT2 (LPX MIDI)` — the pair the grid uses. Match the prefix
  (`LaunchpadSurface::findLaunchpad`); the Pads status line flags the bare one.
- **Port exclusivity is not reliable either way.** Legacy WinMM gives a port to one app; Windows
  MIDI Services here let a second app open the Launchpad. Handle `openDevice` returning `nullptr`,
  but don't depend on it happening.
- **Listing MIDI devices is slow** (≈55 ms per direction here). Never enumerate on a timer.
- **Launchpad pulse/flash run on the device's own 2-beat clock** (1 s at 120 bpm) — far too slow for
  a playhead. Heads are held colours.
- **`BoardComponent` repaints from the lock-free `stones[]` mirror**, which only `publishBoard()`
  writes. Anything that changes the board must publish, or the screen lags behind the pads.

---

## Work in progress: External Controller (Launchpad X)

Approved plan at `C:\Users\flori\.claude\plans\valiant-booping-valley.md`.

**Done** (MSVC build clean, tests green, six pinned hashes unchanged):

- 8×8 as the fourth board size; even-board geometry generalised and tested.
- `Source/LaunchpadMap.h` + tests. Standalone format.
- Play against the AI, in the processor: params `aiOpponent`, `aiOpponentColour`; `playMove` is the
  single stone funnel; `placeStone` refuses on their turn; reply via `pendingOpponentReply` →
  `playOpponentReply`; `passMove`, `newMatch`; turn = `nextAlternating`; `matchPasses` saved as an XML
  attribute; restore takes the switch up directly so the listener cannot clear a restored board.
  AI-tab controls, and the board view refuses erases mid-game.
- `Source/LaunchpadSurface.*`: written, compiled, owned by the processor as `pads` (declared last, so
  destroyed first); ports persist as `launchpadIn` / `launchpadOut`; full edge-button map.
- Protocol note checked against Novation's manual.

- Editor "Pads" tab (`padsTab`, appended): in/out `RefreshingComboBox`es, Find Launchpad, Use 8 x 8,
  Stop, status line, legend of the edge buttons. Choosing ports switches to 8×8 only when
  `boardHasSomethingToLose()` is false, else waits for Use 8 x 8 — no modal dialogs, which hosts
  handle badly. Status is polled in `timerCallback`, never pushed (`onStatusChanged` is unused: the
  editor can die while the surface lives). The tab-strip gap shrinks so six tabs fit at 780 px.
- User docs: `how-to-use-it.md` §6 "Playing against the AI" and §11 "The Launchpad X"; README.

**Hardware:** confirmed working on the user's Launchpad X (2026-09-17), after the port-pick,
static-playhead and repaint fixes. VST3 installed as `C:\Program Files\Common Files\VST3\GoSequencer.vst3`.

**Other controllers / auto-detection / map transfer:** researched plan in
`claude_instructions/midi_controller.md` — not implemented. Read it before touching detection.
Headline trap: the three Novation manuals document the *same* identity reply. Decided 2026-09-17:
Push 2 is the next controller, grids show exact-fit boards only (no 9×9 perimeter ring), and a
plugged-in controller with a built-in config is applied automatically.

**Decisions already taken** — do not relitigate:

- 8×8 only; the grid goes dark on 9/13/19 rather than showing a partial window
  (`lpx::canShow`). 64 pads cannot honestly represent 81 points.
- The AI opponent replies **instantly**, not on a musical tick.
- Programmer mode, never a Components Custom Mode — Novation's own recommendation, and Custom Modes
  reach only the grid. The plugin enters and leaves it by SysEx; nothing is set up on the device.
- LEDs go out as per-pad notes (channel 1 static, 2 flashing, 3 pulsing), sent only when a light
  changes. The bulk SysEx colour spec is a valid later optimisation; both forms are in the manual.
- Play-vs-AI belongs in the **processor**, so the mouse board and the pads drive the same match.
  `nextAlternating` is already the turn state and already persists as `nextColour`; do not add a
  second source of truth.
