# MIDI controllers beyond the Launchpad X — implementation plan

Status 2026-09-17: **plan only, nothing below is implemented.** Today only a Launchpad X is supported,
and it is picked by name. Read `GOSEQ.md` first for the thread contract and the append-only rules;
this file assumes them.

How sure each fact is:
`[M]` vendor manual/doc, read · `[J]` JUCE 9.0.1 source in `../.toolchains/JUCE`, read ·
`[H]` seen on this machine's hardware · `[C]` community knowledge, unverified — check before relying
on it · `[?]` open question, answer on hardware.

## 0. Decisions taken (user, 2026-09-17) — do not relitigate

| # | Question | Decision |
|---|---|---|
| D1 | Second controller to implement and test | **Ableton Push 2**, which the user owns. §6 is its full plan. The other controllers in §8 come after, in the tier order given there. |
| D2 | A 9×9 board using the Launchpad's perimeter buttons as the outer line | **No.** Grids show exact-fit boards only (§4). The index arithmetic is kept in §4.3 as a rejected option, so nobody re-researches it. |
| D3 | What happens when a controller is plugged in | **Find the matching built-in config and apply it automatically**, with no Find click (§3.1). If the plugin has no config for that controller, do nothing and say so. |

---

## 1. What exists today (the baseline to generalise)

| Concern | Where | How it works now |
|---|---|---|
| Detection | `LaunchpadSurface::findLaunchpad` | Name match only. It prefers `MIDIIN*`/`MIDIOUT*` names containing `LPX MIDI`, then falls back to any `LPX MIDI`. It runs only when the user presses **Find Launchpad**. |
| Pairing in↔out | same | Separate searches for the in and out names. Nothing proves they belong to the same device. |
| Presence / hot-plug | `LaunchpadSurface::reconnect` via `MidiDeviceListConnection` | The stored names are re-resolved to identifiers on every device-list change. The connection only exists while ports are wanted. |
| Board size | `GoSequencerEditor::choosePadsPorts` → `useLaunchpadBoardSize` | Choosing ports switches the board to 8×8 only when `!boardHasSomethingToLose()`; otherwise the user presses **Use 8 x 8**. The switch goes through the `boardSize` parameter. |
| Fit rule | `lpx::canShow(size)` = `size == 8` | Exact fit only. The grid stays dark on 9/13/19. |
| Geometry | `Source/LaunchpadMap.h` (JUCE-free, tested in `testPadMapping`) | `padIndex = (8-row)*10 + col + 1`, scene column, top row, logo. |
| Protocol | `LaunchpadSurface.cpp` | Programmer-mode SysEx is hard-coded with model byte `0C`; LEDs are `0x90\|type, index, palette`; colours come from the `palette::` constants. |
| Function map | `LaunchpadSurface::handleButton` (switch) + `buildFrame` (LED per button) | Hard-coded: 16 actions on 16 fixed indices. |
| Map help | `LaunchpadDiagram` in `PluginEditor.h/.cpp` | A drawing plus two hard-coded 8-string label arrays, kept in sync with the switch by hand. |
| Persistence | `apvts.state` properties `launchpadIn` / `launchpadOut` | Port names only. No profile or binding is stored. |
| Input filter | `handleIncomingMidiMessage` | Anything that is not 3 bytes is dropped, **so SysEx replies are thrown away**. Every 3-byte message is queued, so **a Push 2's aftertouch stream would fill the 256-slot queue** (§6.4). |

The generalisation replaces every hard-coded row above with data from a *profile*. It keeps the
threading, the FIFO, the frame diffing and the close order exactly as they are: they are correct and
have been verified on hardware.

---

## 2. Target architecture

```
                       JUCE-free, unit-tested                 │  JUCE, message thread
                                                              │
 ControllerProfile  (constexpr data per model)                │  ControllerDetector
   id "novation.lpx", port needles, identity bytes,           │    enumerate → match → probe → pair
   geometry (cells[], controls[]), colour roles,              │    → auto-apply   (Timer state machine)
   protocol ops (take/release/led encoding), quirks           │
            │                                                 │  ControllerSurface  (= LaunchpadSurface
            ▼                                                 │    renamed; same threads, FIFO, diff,
 BoardFit  (profile × board size → cell↔point, exact only)    │    close order; driven by a Profile)
            │                                                 │
 ActionCatalogue  (every thing a control can do)              │  Editor "Pads" tab
 BindingSet       (action id ↔ control id)                    │    detected model + how it was
   defaultBindings(profile) — semantic, role, priority fill   │    confirmed, generated diagram,
                                                              │    learn mode
```

Rules:
- **Profiles are data**, not subclasses. Model differences go in the profile: indices, SysEx bytes,
  colour roles. Behaviour differences are `quirks` bits checked in the surface. Only a genuinely
  different transport needs another class; none of the planned controllers does (§8).
- Keep `LaunchpadMap.h`'s approach: every geometry, fit and binding function is `constexpr` and
  JUCE-free, so `tests/GoRulesTests.cpp` can cover it without a host.
- **Do not touch** the processor's move funnel (`playMove`), the match logic or the lock-free mirrors.
  The surface stays a pure client of the processor's public API.

### 2.1 Profile sketch (`Source/ControllerProfiles.h`, JUCE-free)

```cpp
namespace ctl {
enum class Msg  : uint8_t { note, cc };
enum class Led  : uint8_t { none, white, palette, rgb };       // what the light behind a control can do
enum class Kind : uint8_t { button, encoderRelative, fader };  // what the control sends
enum class Role : uint8_t { gridEdgeTop, gridEdgeBottom, gridEdgeLeft, gridEdgeRight,
                            arrows, transport, shift, reserved, other };
enum class Sem  : uint8_t { none, up, down, left, right, play, record, undo, del, newItem,
                            duplicate, repeat, metronome, tapTempo, noteLength /*1/4..1/32t*/ };

struct Cell    { uint8_t index; };                           // grid pad: row 0 = top, col 0 = left
struct Control { uint8_t index; Msg msg; Kind kind; Role role; Sem sem; uint8_t order;
                 Led led; const char* label; };             // reserved = never bindable (Push User cc59)

struct Identity {
    uint8_t manufacturer[3]; uint8_t manufacturerLen;        // 00 20 29 / 47 / 00 21 1D
    uint8_t family[2];  bool familyReliable;                 // false for the Novation MK3s (§3.3)
    uint8_t member[2];
    uint8_t probe[12];  uint8_t probeLen;                    // model-specific confirm request, if needed
    uint8_t probeReplyPrefix[8]; uint8_t probeReplyLen;
};

struct Profile {
    const char* id;                         // stable string, persisted — never rename
    const char* displayName;
    const char* portNeedles[4];             // "LPX MIDI", "Ableton Push 2", ...
    int8_t      windowsInterfaceOrdinal;    // 1 = bare name / first, 2 = MIDIIN2..., -1 unknown
    Identity    identity;
    uint8_t     cols, rows;                 // the pad grid
    Cell        cells[64];
    Control     controls[96]; uint8_t controlCount;
    uint8_t     ledIndexCount;              // Frame size: 100 for LPX, 128 generic
    ProtocolOps ops;                        // byte templates: take, release, led(type, index, value)
    ColourMap   colours;                    // roles → device values (§5)
    uint32_t    quirks;                     // settleTicks, pushUserModeHandshake, noteOffIsRelease,
                                            // dropAftertouchOnDeviceThread, restorePaletteOnRelease ...
};
inline constexpr std::array<const Profile*, N> profiles { &lpx, &push2, ... };   // append only
}
```

`LaunchpadMap.h` becomes the source of the `lpx` profile's tables. Its existing tests must keep
passing unchanged; that is the refactor's safety net.

---

## 3. Automatic detection and automatic config (D3)

### 3.1 Policy

| Situation | What happens |
|---|---|
| A device appears (a `MidiDeviceListConnection` callback), or an instance is created, **and this instance has no controller configured** | Run the pipeline (§3.2). If exactly one built-in profile is confirmed, **apply it**: open the ports, take control (`ops.take`), use default bindings plus any saved overrides for that profile, and advise or switch the board size (§4.2). Status: "Push 2 found - using the built-in Push 2 setup". |
| Same, but **no built-in profile matches** | Open nothing and send nothing. Status: "a MIDI controller is connected, but Go Sequencer has no setup for it" (only when Stage B found a grid-shaped name; otherwise stay silent). |
| Several known controllers are connected | Apply the first in profile-list order (`profiles[]`) and list the others in the Pads tab so the user can switch. The result is deterministic, not whichever answers first. |
| Session restore with a stored controller | **The stored config wins.** Reopen it and wait for it to appear, as today. Do not auto-switch to another controller that happens to be connected. |
| The user pressed **Stop** | Store `controllerMode = "off"`. Automatic detection is **suspended for this instance** until the user picks a controller or presses **Find controller**. Without this, Stop would be undone by the next device-list callback. |
| Several plugin instances in one Live set | A process-wide claim (a static `std::set<identifier>` behind a mutex) means only one instance takes a device. The others show "used by another Go Sequencer". Claims are released in `close()`. |

The listener must now exist **whenever automatic mode is on**, not only while ports are wanted. It
costs an enumeration (about 55 ms `[H]`) per device-list change, never per timer tick.

### 3.2 Pipeline (a message-thread state machine; never blocks)

**Stage A: enumerate.** Call `MidiInput/MidiOutput::getAvailableDevices()` once per trigger.

**Stage B: passive match (no I/O).** Score each port against every profile:
- the name contains a `portNeedles` entry → +2
- the Windows interface number matches `windowsInterfaceOrdinal` → +2. `MIDIIN2 (…)` counts as 2 and
  a bare name as 1. **The wrong interface gets −3.** This covers the LPX DAW-port trap and Push 2's
  Live port versus User port (§6.1).
- the identifier holds the profile's USB VID/PID → +3 (§3.4; depends on the backend)

Keep the candidates above a threshold, grouped into in/out pairs per profile.

**Stage C: identity probe.** Probe **only Stage-B candidates**, so no unrelated synth ever receives
SysEx. For each pair, **one at a time** (replies carry no port information):
1. Open the output and the input. On `nullptr` (the port is busy), record that and move on.
2. Send the Universal Device Inquiry `F0 7E 7F 06 01 F7` `[M]`. Push 2 documents `7F` as valid for
   "all devices" `[M]`.
3. Wait up to **300 ms**, checked from a timer tick, for the reply. This needs a SysEx-capable input
   path (§7 step 4).
4. Parse `F0 7E <dev> 06 02 <mfr 1|3 bytes> <family 2> <member 2> <version…> F7`.
5. If the profile's `familyReliable` is false, send `identity.probe` and match `probeReplyPrefix`
   (§3.3).
6. **The input that answered belongs with the output that asked.** This pairs in and out properly,
   instead of trusting two name matches.
7. Keep the winning pair open for §3.1's apply step and close the rest.

**Stage D: decide**, following §3.1. Persist
`{profileId, inName, outName, inIdentifier, outIdentifier, serial?}` on `apvts.state`. Store it as
properties, not parameters: a device on this machine is nothing to automate (the same rationale as
`midiOutPortProperty`).

**Fallback:** if the probe gets no reply but Stage B scored very high (needle + ordinal + VID/PID),
apply the profile anyway, with the status "not confirmed by the device". A controller whose firmware
ignores inquiries must still work.

### 3.3 Identity replies, researched

| Controller | Reply (application) | Distinct? |
|---|---|---|
| Launchpad X | `F0 7E 00 06 02 00 20 29 13 01 00 00 <ver×4> F7` `[M]` | **no.** Identical in all three MK3 manuals. |
| Launchpad Mini MK3 | same bytes `[M]` | no |
| Launchpad Pro MK3 | same bytes `[M]` | no |
| Launchpad MK2 | `F0 7E <dev> 06 02 00 20 29 69 00 00 00 <rev×4> F7`, where the USB identity runs 69h–78h with the bootloader device ID `[M]` | yes |
| Push 2 | `F0 7E 01 06 02 00 21 1D 67 32 02 00 <maj> <min> <build×2> <serial×5> <boardRev> F7`. Family `0x1967` = USB PID, member 2 = Push 2 `[M]` | yes, and it carries a serial |
| APC mini mk2 | `F0 7E <ch> 06 02 47 4F 00 19 <ver×4> <devId> <serial×4> <mfg×16> F7` `[M]` | yes, and it carries a serial |

The three Novation MK3 replies are either a manual copy-paste or truly shared. `[C]` Community code
reports different family bytes. **Capture it on the user's LPX before trusting either** (§7 step 0).
Until then, the MK3s confirm with a model-specific request that only the right model answers:

| Model | Probe (read current layout) | Reply must start with |
|---|---|---|
| Launchpad X | `F0 00 20 29 02 0C 00 F7` | `F0 00 20 29 02 0C 00` |
| Launchpad Mini MK3 | `F0 00 20 29 02 0D 00 F7` | `F0 00 20 29 02 0D 00` |
| Launchpad Pro MK3 | `F0 00 20 29 02 0E 00 F7` `[M]` (p7) | `F0 00 20 29 02 0E 00` |

`[?]` X and Mini: the read form is assumed from the Pro MK3 manual. The port needles `LPX` /
`LPMiniMK3` / `LPProMK3` are unambiguous anyway.

### 3.4 USB VID/PID from the identifier (Windows)

JUCE 9.0.1 has two Windows backends `[J]`. `ump::Endpoints::getBackend()` says which one is live
(`juce_UMPEndpoints.h:146`).
- **WinMM:** `MidiDeviceInfo::identifier` is the driver's device interface path, from
  `DRV_QUERYDEVICEINTERFACE` (`juce_Midi_windows.cpp:2445-2461`). `[C]` It usually looks like
  `\\?\usb#vid_1235&pid_0103&mi_01#…`. That gives the VID, the PID **and the USB interface number**,
  which would settle in/out pairing and the interface traps with no probe.
- **Windows MIDI Services / WinRT:** identifiers are service or container IDs (`containerID`,
  `:1439-1598`) with no VID/PID. `ump::StaticDeviceInfo` has `manufacturer` / `product` strings,
  which might be filled.

This machine runs Windows MIDI Services `[H]`, so **dump before designing Stage B** (§7 step 0).
`[C]` VID/PIDs to confirm: Novation `1235` (LPX `0103`, Mini MK3 `0113`, Pro MK3 `0123`); Akai
`09E8`; Ableton `2982` with Push 2 PID `1967`, which matches its inquiry family `[M]`.

### 3.5 Pitfalls

- **Port busy.** A second app *could* open the LPX here `[H]`, so exclusivity cannot be assumed
  either way. Handle `openDevice` returning `nullptr`, and never retry in a loop; wait for the next
  device-list callback.
- **Bootloader replies** (Novation `… 13 11 …` `[M]`): tell the user; do not drive the device.
- **Settle time.** The LPX ignores LEDs for about 150 ms after entering programmer mode `[H]`. This
  becomes `quirks.settleTicks` per profile; assume the same until measured.
- **Automatic mode and hosts.** Plugin instances are created during a scan or project load too.
  Detection must start from the editor-independent processor, but **only after
  `prepareToPlay`/`setStateInformation` have run**, so a restore can say "configured" before
  automatic mode looks.

---

## 4. Board size

### 4.1 Fit rule (D2: exact only)

A grid shows a board only when `size == min(cols, rows)` **and that size is in
`go::supportedSizes`**. Every controller planned here has an 8×8 grid, so the board is 8×8; on
anything else the grid stays dark with a status line, as today. `BoardFit` generalises
`lpx::boardIndexFor` / `padIndexFor` to take the profile's `cells[]`, and every profile gets a
round-trip test like `testPadMapping`.

Rejected: *inset* (a smaller board centred on a bigger grid) and *viewport* (panning a bigger board).
Neither is needed by any planned controller; revisit only if a 16×16 grid is ever added.

### 4.2 Automatic size policy

1. When a profile is applied (§3.1) and the board is not its fit size:
   - **If `!boardHasSomethingToLose()`**, switch through the `boardSize` parameter, exactly as
     `useLaunchpadBoardSize` does.
   - **Otherwise don't.** Status: "the pads stay dark until the board is 8 x 8 - Use 8 x 8 clears
     this one". This is today's rule, because automatic mode must never wipe a position.
2. **Never on session restore.** A saved 9×9 board with a stored Launchpad stays 9×9, with a dark
   grid.
3. Changing the board size later: dark grid plus the status, as today.

### 4.3 Rejected: 9×9 ring on the perimeter (kept so it is not re-derived)

- LPX / Mini MK3: `index = (9-row)*10 + col + 1`. It uses every edge button, and the top-right point
  would be the logo (99), which is an LED, not a button.
- Pro MK3: `index = (9-row)*10 + col`, taking the top row 90–98 and the left column 80–10, and leaving
  24 buttons free `[M]` (p19 layout image).

Rejected by D2.

---

## 5. Functions and colours on a new controller

### 5.1 Action catalogue instead of a switch

Pull the current behaviours out of `handleButton` / `buildFrame` into one table. **Action ids are
persisted** in bindings, so they are append-only and never renamed.

| id | kind | today on LPX | LED role | priority | semantic hint |
|---|---|---|---|---|---|
| `transport.rateUp` / `transport.rateDown` | press, `stepChoice rate ±1` | 91 / 92 | idle | 2 | `up` / `down` |
| `game.stepBack` / `game.stepOn` | press, `nudgeGamePosition ∓1` | 93 / 94 | usable if `hasGame()` | 3 | `left` / `right` |
| `game.run` | toggle `gameRun` | 95 | on/idle | 1 | — |
| `sequencer.freeRun` | toggle `freeRun` | 96 | on/idle | 2 | `play` |
| `board.cyclePlace` | press, `cycleChoice colourMode` | 97 | the next stone's colour; off in a match | 3 | — |
| `board.clear` | **hold** 21 ticks, `clearBoard` / `newMatch` | 98 | refused (red) while held | 1 | `del` |
| `match.toggle` | toggle `aiOpponent` | 89 | on/idle | 1 | — |
| `match.pass` | press, `passMove` | 79 | usable if `yourTurn()` | 1 | — |
| `board.liftLast` | press, `eraseStone(lastMove)` | 69 | usable if erase allowed | 2 | `undo` |
| `game.loop` | toggle `gameLoop` | 59 | on/idle | 3 | `repeat` |
| `game.waveReplay` | toggle `waveReplay` | 49 | on/idle | 3 | — |
| `game.rateFaster` / `game.rateSlower` | `stepChoice gameRate −1/+1` (inverted list) | 39 / 29 | idle | 3 | — |
| `surface.redraw` | re-take and redraw | 19 | idle | 4 (droppable) | — |
| *new, for encoders* `game.scrub` | relative: `nudgeGamePosition(delta)` | — | — | 2 | encoder |
| *new* `transport.rate` / `game.rate` | relative: `stepChoice` by the sign, rate-limited | — | — | 3 | encoder |
| *new* `match.new` | press, `newMatch` | — (hold 98 in a match) | usable in a match | 2 | `newItem` |

Each entry is `{ id, kind (press|hold|toggle|relative), perform, ledRole, label, priority, sem,
preferredRoles[] }`. `buildFrame` loops over the bindings, and **`LaunchpadDiagram` is generated from
the same table**, which removes today's by-hand sync.

### 5.2 Default bindings (`defaultBindings(profile)`, deterministic, constexpr-testable)

1. **Semantic match first.** A control whose `sem` equals an action's hint takes that action: arrows
   take rate/step, Undo takes lift-last, Delete takes clear (a hold), Play takes free run, New takes
   new match, Repeat takes loop.
2. **Encoders** take the relative actions, in `order`: scrub first.
3. **Group by role.** Transport/replay actions prefer the control row nearest the top of the grid
   (`gridEdgeTop`); match and board actions prefer the column beside the grid (`gridEdgeRight`).
4. Fill the remaining actions in priority order into the remaining controls, in `order`.
5. **Not enough controls:** leftovers go to shift+control when the profile has a `shift` role
   (Push 2 cc49 `[M]`, APC mini mk2 0x7A `[M]`, Pro MK3 CC 90 `[M]`). Otherwise the highest priority
   number is dropped first, so `redraw` goes before `pass`.
6. **`Role::reserved` controls are never bound.** Push 2's User button (cc59) switches modes for
   Live `[M]`.
7. **`board.clear` stays a hold** on every device.

**First test:** `defaultBindings(lpx)` must reproduce today's 16 index↔action pairs exactly (give
the LPX profile no `sem` hints that would move anything). Otherwise the refactor silently changes
the user's controller.

### 5.3 Overrides, learn mode, persistence, transfer

- Store a `ControllerBindings` child tree on `apvts.state`, keyed by `profileId`, holding **only
  overrides**: `<Binding action="match.pass" control="cc:79" shift="0"/>`. The defaults are
  recomputed.
- **Learn:** click an action in the diagram, and the next control event (through the FIFO, on the
  message thread) binds to it. The control's previous action becomes unbound, and the diagram shows
  that.
- **Migration:** a session with `launchpadIn`/`launchpadOut` and no `profileId` means
  `profileId = "novation.lpx"`, `controllerMode = "manual"`, default bindings. Keep reading the old
  keys forever.
- **Transferring customised overrides to another profile** goes action by action. If the new
  profile has a control with the same `sem`, use it. Otherwise use the same `role` + `order`.
  Otherwise fall back to §5.2.

### 5.4 Colour roles

The surface asks for **roles**:
`off, idle, on, refused, blackStone, blackSpent, whiteStone, whiteSpent, star, headEmpty, headBlack,
headWhite, nextBlack, nextWhite`. Each profile maps a role to `{channel/type, value}` for palette
LEDs and to a brightness for white LEDs.

- **Heads are always held colours, never device animations.** Novation pulses on a 2-beat clock
  `[M][H]`. Push 2 animations need MIDI start/clock on the User port and do not run at all until a
  start message arrives `[M]`.
- A device with a programmable palette (Push 2) writes its role colours into palette slots on take
  and **restores the previous entries on release** (§6.3).

---

## 6. Push 2 (D1), the full profile plan

Everything here is `[M]` from Ableton's *Push 2 MIDI and Display Interface Manual* v1.1 (firmware
1.0.60) unless marked otherwise.

### 6.1 Ports and modes: the part that is different from Launchpads

- **Two MIDI ports, each owned by one application at a time.**
  - The **Live port** is used by Live's Push 2 script.
  - The **User port** is the one GoSequencer uses.
  - Windows names: Live port `Ableton Push 2 nn`; User port `MIDIIN2 (Ableton Push 2) nn` /
    `MIDIOUT2 (Ableton Push 2) nn`, where `nn` may be blank. macOS names:
    `Ableton Push 2 Live Port` / `Ableton Push 2 User Port`. `[?]` Confirm the names under Windows
    MIDI Services.
  - **In Live, the user must turn Track/Sync/Remote Off for Push 2's port 2**, or Live keeps the User
    port.
- **The MIDI mode decides which port the pads and buttons use.**
  - In Live mode, non-SysEx MIDI goes to and from port 1 only.
  - In **User mode**, only port 2 carries it.
  - SysEx is accepted on both ports, and replies go to the port that asked.
  - The reply to Set MIDI Mode goes to **both** ports.
  - The User button (cc59) always reports to both ports.
- **Set MIDI Mode:** `F0 00 21 1D 01 01 0A <m> F7`, where `m` is 0 Live, 1 User, 2 Dual. The reply
  echoes the mode.

**Handshake design (`quirks.pushUserModeHandshake`):**
1. Open the User port pair (it is detected as `ordinal 2` + needle `Ableton Push 2`, then confirmed
   by the inquiry: family `67 32`, member `02 00`).
2. **Hosted inside Live**, where the Push 2 script usually runs: **do not send Set MIDI Mode.**
   - Show "press User on the Push".
   - The User press makes Live's script send `0A 01`, and the reply arrives on the plugin's port too.
     On `0A 01`: take the surface and redraw everything.
   - On `0A 00` (the user pressed User again, or Live took the Push back): **stop drawing at once**,
     keep the ports open, and wait for the next `0A 01`.
   - The processor, the board and the match are untouched either way.
3. **Standalone, or a host where no Live-mode reply is ever seen:** send `0A 01` on take and `0A 00`
   on release. `[?]` Decide by a setting, `pushTakeMode = waitForUser | takeDirectly`, defaulting to
   `waitForUser` inside a plugin and `takeDirectly` in the standalone app. Test both on hardware.
4. `[?]` Check on hardware what Live's script does when the plugin sends `0A 01` while the script is
   active. The documented design says Live toggles on the User button, so taking the Push directly
   may confuse the script.

### 6.2 Geometry and controls

- **Pads:** notes 36–99. Note 36 is bottom-left and 99 top-right, so `note = 36 + (7-row)*8 + col`
  with row 0 at the top.
  - Press: Note On with velocity 1–127.
  - **Release: Note Off `0x80`**, not Note On with velocity 0. Today's handler ignores releases, so
    that is fine, but the filter must accept `0x80`.
  - Aftertouch arrives as `0xD0` channel pressure by default, or `0xA0` poly.
- **Buttons:** CC, value 127 on press and 0 on release. From the MIDI map image:

| Group | CCs | LED | Suggested role / sem |
|---|---|---|---|
| Row directly **above the pads** (under the display) | 20–27 | RGB | `gridEdgeTop` order 0–7 |
| Row above the display | 102–109 | RGB | `other` (a second top row) |
| Column **right of the pads** ("1/4 … 1/32t", cc36 at the bottom up to cc43 at the top) | 36–43 | RGB | `gridEdgeRight`; order 0 = cc43 top |
| Arrows: left / right / up / down | 44 / 45 / 46 / 47 | white | `arrows`, `sem` left/right/up/down |
| Play / Record | 85 / 86 | RGB | `transport`, `sem` play / record |
| Undo / Delete / New / Duplicate | 119 / 118 / 87 / 88 | white | `sem` undo / del / newItem / duplicate |
| Repeat / Accent | 56 / 57 | white | `sem` repeat |
| Tap Tempo / Metronome | 3 / 9 | white | `sem` tapTempo / metronome |
| Shift / Select | 49 / 48 | white | `shift` / `other` |
| Octave up / down, Page left / right | 55 / 54, 62 / 63 | white | `other` |
| Mute / Solo / Stop Clip | 60 / 61 / 29 | RGB | `other` |
| **User** | 59 | white | **`reserved`** (mode switch) |
| Setup, Note / Session, Scale / Layout, Device / Mix / Browse / Clip, Add Device / Add Track, Master, Fixed Length, Automate, Convert, Double Loop, Quantize | 30, 50 / 51, 58 / 31, 110 / 112 / 111 / 113, 52 / 53, 28, 90, 89, 35, 117, 116 | mixed | `other` |

- **Encoders:** relative CC in two's complement: 1–63 is right, 127–64 is left, about 210 steps per
  turn. Touching an encoder sends a note (velocity 127 on touch, 0 on release).
  - Track encoders: cc71–78 with touch notes 0–7.
  - Master encoder: cc79, note 8.
  - Tempo encoder: cc14, note 10, 18 detented steps per turn.
  - Swing encoder: cc15, note 9.
- **Touch strip:** pitch bend plus note 12. Not used.

**Default Push 2 bindings the §5.2 algorithm should produce** (put this in a test):

| Control | Action |
|---|---|
| ← / → (cc44/45) | `game.stepBack` / `game.stepOn` |
| ↑ / ↓ (cc46/47) | `transport.rateUp` / `transport.rateDown` |
| Undo (119) | `board.liftLast` |
| Delete (118), hold | `board.clear` |
| New (87) | `match.new` |
| Play (85) | `sequencer.freeRun` |
| Repeat (56) | `game.loop` |
| cc20–27 (above the pads) | `game.run`, `game.waveReplay`, `board.cyclePlace`, … |
| cc43…36 (right of the pads) | `match.toggle`, `match.pass`, `game.rateFaster`, `game.rateSlower`, … |
| Encoder 1 (cc71) | `game.scrub` |
| Encoder 2 (cc72) | `transport.rate` |
| Encoder 3 (cc73) | `game.rate` |

Relative actions need a detent accumulator on the message thread, for example one choice step per
±8 encoder units, so a flick does not jump across the whole rate list.

### 6.3 LEDs and colours

- **Message:** Note On (pads) or CC (buttons). The velocity is the palette index. The **channel is
  the animation**: 0 is static; 1–5 one-shot, 6–10 pulse, 11–15 blink, each at 24th/16th/8th/quarter/
  half. Use channel 0 only (§5.4).
- **White-LED buttons** use a separate white palette: 0 black, 16 dark grey, 48 light grey, 127 white.
- **The default RGB palette is documented only in part:** 0 black, 122 white, 123 light grey,
  124 dark grey, 125 blue, 126 green, 127 red. The rest can change with firmware.
- **So the profile writes its own colours:**
  1. On take, read each role slot with `F0 00 21 1D 01 01 04 <i> F7`. The reply is
     `… 04 i rL rM gL gM bL bM wL wM F7`, 7-bit LSB plus 1-bit MSB per channel.
  2. Write the role colours with `03`, then `F0 00 21 1D 01 01 05 F7` (reapply).
  3. **On release, write the saved entries back and reapply**, so Live's colours are unharmed.
  - Use a few high slots, for example 100–113. `[?]` Check that Live's script does not rely on them.
  - Replies must not be nested: send the next command only after the previous reply `[M]`, so the
    palette setup is a small sequenced state inside the take step.
- **Brightness:** when USB-powered, global LED brightness is **capped at 8 of 127** `[M]`, so dim
  colours may be invisible. Pick spent-stone colours with that in mind, and tell the user that
  mains power makes the grid readable.
- **Display:** needs libusb bulk transfers, not MIDI. **Out of scope** (the user avoids extra
  dependencies). Live keeps drawing its own display.

### 6.4 Input path changes Push 2 forces

- **Aftertouch floods the queue.** `0xD0`/`0xA0` arrive continuously while a pad is held. Drop them on
  the device thread before the FIFO. Also drop pitch bend `0xE0` (the touch strip) and encoder-touch
  notes 0–12 unless an action is bound to them. Keep the drop cheap and allocation-free.
- **Note Off `0x80`** is a pad release; pads act on press, so the filter just must not choke on it.
- **SysEx**: take the mode replies (`0A`), palette replies (`04`) and the inquiry reply off the device
  thread into a preallocated buffer (§7 step 4).
- The CC value is velocity-like (127/0) for buttons and relative for encoders. `Control::kind` tells
  them apart, so the decoder needs the profile.

---

## 7. Implementation steps (in order, each ends verifiable)

Every step: `.\build.ps1` stays warning-free and all tests pass, with the **pinned AI hashes
unchanged**. Close the standalone before building; the exe is locked while it runs.

**Step 0: capture before coding.** Hardware, the user's LPX **and** Push 2, about 20 minutes.
- A throwaway debug button or console tool that dumps every in/out `name` and `identifier`, the
  backend and `StaticDeviceInfo` (§3.4).
- LPX: the inquiry reply and the `02 0C 00` layout-read reply (settles §3.3).
- Push 2: the inquiry reply; the port names under Windows MIDI Services; with Live running and the
  script active, what arrives on the User port when User is pressed (confirms `0A 01` both ways);
  palette entries 100–113 before and after Live starts.
- Paste the results into `reference/launchpad-x-protocol.md` and a new `reference/push2-protocol.md`.

**Step 1: profile data, no behaviour change.**
- Add `ControllerProfiles.h` with the `lpx` profile built from `LaunchpadMap.h`, plus tests.
- Make `LaunchpadSurface` read geometry, SysEx and colours from `const Profile&`.
- Gate: all existing tests pass, including `testPadMapping`, and the LPX behaves identically on
  hardware.

**Step 2: action catalogue and bindings.**
- Add `ActionCatalogue` and `defaultBindings`.
- Test: `defaultBindings(lpx)` equals today's map.
- `LaunchpadDiagram` is generated from the bindings.
- Gate: the Pads tab looks the same and the hardware behaves the same.

**Step 3: rename and generalise the surface.**
- `LaunchpadSurface` becomes `ControllerSurface`, and the `lpx::` fit becomes `BoardFit`.
- Add the `profileId` and `controllerMode` properties, and the migration.
- Test: an old session XML with `launchpadIn`/`launchpadOut` still opens the LPX.

**Step 4: input path.**
- The device thread sorts messages: a per-profile drop filter (aftertouch, pitch bend, unbound
  touches), 3-byte messages into the FIFO as today, and SysEx up to 64 bytes into a preallocated
  single-writer slot plus an async update.
- Test the reply parsers against the Step 0 captures (pure functions).

**Step 5: `ControllerDetector` with the automatic policy (§3.1).**
- Stages A–D; the process-wide claim; the `off` suspension after Stop.
- **Find Launchpad** becomes **Find controller**; the status shows the model and how it was
  confirmed.
- Gates:
  - Plugging in the LPX configures it with no click, on the `MIDIIN2/MIDIOUT2` pair confirmed by the
    probe.
  - A restored session never switches.
  - After Stop, replugging does nothing.

**Step 6: Push 2 profile (D1).**
- Geometry and controls from §6.2, the User-mode handshake (§6.1), the palette take and restore
  (§6.3), the relative-encoder actions, and the §6.2 default-binding test.
- Gates, on hardware inside Live:
  - Press User, and the grid shows the board while the pads play it.
  - Press User again, and Live's session view comes back intact with correct colours.
  - The standalone app takes the Push directly and gives it back on quit.

**Step 7: learn mode and overrides UI.** Clickable actions in the diagram, the `ControllerBindings`
tree, and a reset-to-defaults button.

**Step 8: further profiles**, in the §8 tier order. Every one needs a geometry round-trip test, a
default-binding test (all priority 1–2 actions reachable) and an inquiry-parser test from a captured
reply.

**Step 9: docs.**
- `how-to-use-it.md` §11 becomes "Controllers", with a table per model and the Push 2 Live setup
  (port 2 Track/Sync/Remote Off; press User).
- Update the README, `GOSEQ.md` (file map, traps) and this file's status line.

---

## 8. The ten most popular controllers with at least an 8×8 pad grid

There are no sales figures, so "popular" here is a judgement from three buyer's guides (§10):
- Gearank's aggregate of 1,500+ user reviews ranks the Launchpad X and Pro MK3 top among pad
  controllers.
- MusicRadar picks the Launchpad X and Push 2.
- Headliner gives the APC mini mk2 its Editor's Choice.

Discontinued models with a large second-hand base are included. Tiers say how well each fits this
plan.

| # | Controller | Grid | Tier | Why that tier |
|---|---|---|---|---|
| 1 | Novation **Launchpad X** | 8×8 | **A: done** | the reference implementation |
| 2 | Ableton **Push 2** | 8×8 | **A: next (D1)** | fully documented `[M]`, owned by the user |
| 3 | Novation **Launchpad Pro MK3** | 8×8 + 32 edge buttons | A | same protocol family as the X, model byte `0E`; its MIDI port is the **first** interface on Windows, not the second `[M]` |
| 4 | Novation **Launchpad Mini MK3** | 8×8 + 16 edge | A | layout identical to the X, model byte `0D`, `LPMiniMK3 MIDI` on the **second** interface `[M]`; pads not velocity-sensitive (irrelevant here) |
| 5 | Akai **APC mini mk2** | 8×8 + 16 single-LED buttons + 9 faders | A | documented `[M]`; different geometry, and it needs an intro message; §8.1 |
| 6 | Novation **Launchpad MK2** | 8×8 + 16 edge | A | documented `[M]`; distinct inquiry; §8.1 |
| 7 | Ableton **Push 3** | 8×8 | B | same User-mode idea as Push 2 `[C]`, but no published LED/SysEx protocol; pads default to MPE expression, and Ableton's help says to set Poly/Mono Aftertouch before User mode `[C]`; §8.2 |
| 8 | Ableton **Push (1)**, made by Akai | 8×8 | B | pads 36–99 in User mode, velocity = colour `[C]`; no official protocol doc; §8.2 |
| 9 | Roger Linn **LinnStrument** (128 / 200) | 8 rows × 16 / 25 columns (+ control column) | B | open firmware and documented User Firmware Mode `[M]`, but only 11 colours and three CCs per LED; §8.2 |
| 10 | Native Instruments **Maschine Jam** | 8×8 | C | MIDI mode depends on a template made in NI Controller Editor `[C]`; no raw protocol, so no automatic config; §8.3 |

**Seen but left out, with the reason:**
- Akai Fire (4×16) and APC40 mkII / APC Key 25 mk2 (5×8): under 8 rows.
- Maschine MK3 / Mikro and PreSonus ATOM (4×4): too small.
- monome grid (8×8 / 16×8 / 16×16): **OSC via serialosc, not MIDI.** It would need a second
  transport; `juce_osc` is in the tree `[J]`.
- Akai Force (8×8): a standalone device; its controller mode is proprietary to Live.
- Launchpad S / Mini MK2: red/green LEDs only, an older protocol.

### 8.1 Tier A details (beyond the X and Push 2)

**Launchpad Pro MK3** `[M]`
- **Ports:** `LPProMK3 MIDI` (1st interface), `DIN` (2nd), `DAW` (3rd).
- **Take / release:** take `F0 00 20 29 02 0E 0E 01 F7`; release `…0E 00 F7`. Release goes to the
  Session layout (DAW mode) or Note mode. The device boots in Live mode.
- **Layout:** grid notes 11–88; top row CC 90–98 (90 is the top-left corner); left column CC 80…10;
  right column CC 89…19; bottom rows CC 101–108 and CC 1–8; logo 99. Every index accepts Note or CC
  for lighting.
- **LEDs:** ch 1/2/3 static/flash/pulse; bulk `…0E 03` with up to 106 colour specs, type 3 = RGB.
- **Several units:** the bootloader sets a USB device ID 1–16 per unit.
- **Default bindings:** `shift` = CC 90; the arrows are `[?]` (read them from the device's printed
  labels).

**Launchpad Mini MK3** `[M]`
- **Ports:** `LPMiniMK3 DAW` (1st interface), `LPMiniMK3 MIDI` (2nd).
- **Take:** `F0 00 20 29 02 0D 0E 01 F7`.
- **Layout:** the same as the X, including top CC 91–98, right CC 89…19 and logo 99.

**APC mini mk2** `[M]` (Communications Protocol v1.0)
- **Grid:** notes `0x00–0x3F`. **`0x00` is bottom-left** and `0x38` top-left, so
  `note = (7-row)*8 + col`.
- **Buttons:** track buttons `0x64–0x6B` (bottom row, red LED); scene buttons `0x70–0x77` (right
  column, green LED); shift `0x7A` (no LED). All are notes on channel 0, port 0.
- **Faders:** CC `0x30–0x38` (8 channel faders + master). Absolute; a later `kind = fader` action.
- **Pad LEDs:** `9<ch> note palette`.
  - Channels 0–6: solid at 10/25/50/65/75/90/100%.
  - Channels 7–10: pulse at 1/16…1/2.
  - Channels 11–15: blink at 1/24…1/2.
  - The palette is fixed; e.g. 0 black, 3 white, 5 red, 45 blue.
  - **Brightness by channel** gives natural spent stones: the same hue at a lower channel.
- **Button LEDs:** `90 note v`, where v 0 = off, 1 = on, 2 = blink. Single colour, so the colour
  roles degrade to on/off/blink.
- **RGB SysEx:** `F0 47 7F 4F 24 <lenMSB> <lenLSB> <startPad> <endPad> <R MSB LSB> <G MSB LSB>
  <B MSB LSB> … F7`.
- **Take:** the intro message **before anything else**:
  `F0 47 7F 4F 60 00 04 00 <verHi> <verLo> <bugfix> F7`. The reply `F0 47 7F 4F 61 00 04 <9 fader
  values> F7` doubles as model confirmation. `[?]` Byte layout reconstructed from a garbled
  two-column PDF table; recheck on hardware.
- **Release:** `[?]` the manual documents no release; clear the LEDs.

**Launchpad MK2** `[M]` (Programmer's Reference v1.03)
- **Layout:** Session layout (`F0 00 20 29 02 18 22 00 F7`). Grid notes 11–88; **the right column
  sends notes** 19…89, not CC; the top row sends **CC 104–111**.
- **LEDs:** ch1 static, ch2 flash, ch3 pulse; RGB SysEx exists.
- **Inquiry:** family `69 00`, shifted by the bootloader device ID (69h–78h). **Accept that range.**
- **Port name:** `[C]` `Launchpad MK2`.

### 8.2 Tier B details

- **Push 3:**
  - Control mode + User mode, as on Push 2.
  - Windows User port `[C]` "Ableton Push 3 (Port 2)"-style naming.
  - Pads send MPE unless Expression is set to Poly/Mono Aftertouch first `[C]`.
  - **No published LED/palette/SysEx document** was found. Treat Push 2's `0A` mode command and
    palette SysEx as `[?]` until captured.
  - Implement only after Push 2 works; then capture Push 3's inquiry reply and try the Push 2 profile
    with a `push3` id.
- **Push 1:**
  - User port; pads notes 36–99 with velocity = colour `[C]`.
  - An RGB SysEx has been reported: `F0 47 7F 15 04 00 08 <pad> 00 rH rL gH gL bH bL F7` `[C]`.
  - Channels select animations `[C]`.
  - Without an official document, build it only with a Push 1 on the desk to capture from.
- **LinnStrument** `[M]` (firmware repo `user_firmware_mode.md`, `midi.md`):
  - **User mode:** NRPN 245 = 1 on, 0 off. It wipes all LEDs; the device sends NRPN 245 on ch 9 when
    the user leaves by hand.
  - **Cell press:** row = channel 1–8 (**1 = bottom**), column = note 0–25 (**0 = the control
    column**), Note Off on release.
  - **LED:** three CCs: CC20 column, CC21 row (both from 0), CC22 colour. Colours: 1 red, 2 yellow,
    3 green, 4 cyan, 5 blue, 6 magenta, 7 off, 8 white, 9 orange, 10 lime, 11 pink.
  - **Consequences:**
    - Three messages per LED make the frame diff essential.
    - No dim shades: spent stones need a separate hue, for example blue → cyan and white → pink.
  - **Geometry:** rows = 8, so the board is 8×8, placed at columns 1–8. The rest of the surface is
    free for controls, but its cells are not labelled buttons, so default bindings need `order` only.

### 8.3 Tier C

**Maschine Jam:** no automatic config. At most, document a manual setup: build an NI Controller
Editor template that sends LPX-like notes, then bind it with learn mode.

---

## 9. Later ideas (not required for the above)

- **Faders** (APC mini mk2; Pro MK3 DAW faders): `kind = absolute` actions for continuous parameters,
  with gestures as `setParameter` already does.
- **MIDI clock to a controller**, so device-side animations follow the sequencer. Not for heads
  (§5.4). It must not come from the audio thread directly; mind the thread contract.
- **MIDI-CI discovery.** `juce_midi_ci` exists `[J]`, but none of the researched controllers claim
  MIDI-CI support. Add it later as another Stage C source.
- **Buttons-only profiles** (Launchkey, Launch Control XL): transport and match control beside a
  mouse board, with `cols = rows = 0`.

## 10. Sources

- Novation, Launchpad X Programmer's Reference Manual — https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20X%20-%20Programmers%20Reference%20Manual.pdf
- Novation, Launchpad Mini [MK3] Programmer's Reference Manual — https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20Mini%20-%20Programmers%20Reference%20Manual.pdf
- Novation, Launchpad Pro [MK3] Programmer's Reference Manual — https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/LPP3_prog_ref_guide_200415.pdf
- Novation, Launchpad MK2 Programmer's Reference Manual v1.03 — https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20MK2%20Programmers%20Reference%20Manual%20v1.03.pdf
- Akai Professional, APC mini mk2 Communications Protocol v1.0 — https://cdn.inmusicbrands.com/akai/attachments/APC%20mini%20mk2%20-%20Communication%20Protocol%20-%20v1.0.pdf
- Ableton, Push 2 MIDI and Display Interface Manual v1.1 (+ `MidiMapping.png`) — https://github.com/Ableton/push-interface/blob/master/doc/AbletonPush2MIDIDisplayInterface.asc
- Ableton help, Push User Mode / Push 3 MIDI FAQ (search summaries only; the pages return 403 to fetch) — https://help.ableton.com/hc/en-us/articles/209071249-Push-User-Mode-for-custom-MIDI-mappings , https://help.ableton.com/hc/en-us/articles/26201367296924-Push-3-MIDI-FAQ
- Roger Linn Design, LinnStrument firmware `user_firmware_mode.md` and `midi.md` — https://github.com/rogerlinndesign/linnstrument-firmware
- Popularity: Gearank pad-controller ranking https://gearank.com/guides/midi-pad-controllers · MusicRadar https://www.musicradar.com/news/the-best-midi-pad-controllers · Headliner https://headlinerhub.com/best-midi-pad-controllers-any-budget.html
- Push 1 community notes — https://forum.ableton.com/viewtopic.php?t=207483 , https://github.com/Ableton/push-interface/issues/12
- JUCE 9.0.1: `modules/juce_audio_devices/native/juce_Midi_windows.cpp`, `midi_io/ump/juce_UMPEndpoints.h`, `midi_io/ump/juce_UMPStaticDeviceInfo.h`
- In-repo: `reference/launchpad-x-protocol.md`, `Source/LaunchpadMap.h`, `Source/LaunchpadSurface.*`
