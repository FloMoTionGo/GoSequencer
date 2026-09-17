# Launchpad X — wire protocol

Sources, in order of authority:

1. Novation, *Launchpad X Programmer's Reference Manual* — page numbers below refer to it.
   https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/downloads/Launchpad%20X%20-%20Programmers%20Reference%20Manual.pdf
2. The `LaunchpadX_Seq` Max-for-Live project, tested on this very Launchpad. It was deleted and its
   Recycle Bin entry emptied on 2026-09-16; what it knew survives only here. Facts from it that the
   manual does not state are marked *(hardware)*.

## Programmer mode, not a Custom Mode

The manual (p9) calls Programmer mode "the best option for lighting pads/buttons or creating an
interactive surface". Lighting Custom Modes — the kind built in Novation Components — reach only the
8x8 grid unless Ghost mode is on, and the top row stays the device's own mode buttons. GoSequencer uses
Programmer mode and needs nothing set up in Components.

    programmer mode on   F0 00 20 29 02 0C 0E 01 F7      (p7)
    live mode            F0 00 20 29 02 0C 0E 00 F7

- **Once software has selected Programmer mode, holding Session no longer opens the setup menu**
  (p7-8). Nobody can take the device out of it by hand; the software has to send live mode. If a crash
  leaves it stuck, unplugging and replugging should reset it — an inference, not in the manual.
- By hand, with no software involved: hold Session, press the bottom right-column button (p9).
- After mode-on the device ignores LED writes for about 150 ms *(hardware)*. Count timer ticks rather
  than sleeping on the message thread.
- On teardown, clear the LEDs first and send live mode after *(hardware)*.

## Ports (p6)

    LPX DAW  In/Out    first interface on Windows     DAW / Session mode. Not used here.
    LPX MIDI In/Out    second interface on Windows    Programmer mode and lighting live here.

**Windows naming trap** (this machine, class-compliant driver + Windows MIDI Services, 2026-09): the
ports list as `LPX MIDI` and `MIDIIN2 (LPX MIDI)` / `MIDIOUT2 (LPX MIDI)`, and there is no `LPX DAW`.
The bare `LPX MIDI` is the FIRST interface — DAW. The grid is on the `MIDIIN2`/`MIDIOUT2` pair, as the
old Max project found on hardware. A name match on "LPX MIDI" alone picks the DAW port, because it is
listed first — match the numbered prefix. In and out are named apart, so store both.

Measured here: one short-message send ≈ 0.1 ms (81 lights ≈ 8 ms); listing the MIDI inputs ≈ 55 ms, a
round trip to the MIDI service, so never on a timer. A second program opened the Launchpad's output
while Go Sequencer was running, so do not rely on port exclusivity either way. Keep Live off the
device regardless (control surface None; Track/Sync/Remote off): its script would leave Programmer
mode and its tracks would play the pads.

This machine once refused to list the ports at all. What fixed it: the Microsoft class-compliant
"USB Audio Device" driver, `Restart-Service midsrv -Force` as admin, then unplug and replug.

## Layout (p10)

Row 0 is the top and col 0 the left — the same way up as `go::rowOf` / `go::colOf`.

    grid           Note   (8 - row) * 10 + col + 1    11 bottom-left .. 88 top-right
    right column   CC     (8 - row) * 10 + 9          89 top .. 19 bottom
    top row        CC     91 .. 98                    arrows 91-94, Session 95, Note 96,
                                                      Custom 97, Capture MIDI 98
    logo           CC     99

In Programmer mode every button sends a message (p9), Session, Note and Custom included — they do not
change layout. Releases arrive as Note On velocity 0 (p6) or CC value 0.

## Lighting

**Every LED accepts either a Note or a CC on its index** (p10), the edge buttons included.

    channel 1   90h / B0h   static     value = palette index                      (p13)
    channel 2   91h / B1h   flashing   between the static or pulsing colour and this one
    channel 3   92h / B2h   pulsing    dark to full

The device keeps flashing (one beat a period) and pulsing (two beats) in time by itself, off incoming
MIDI clock or at 120 bpm without one (p13). So there is no phase to protect by not re-sending — only
traffic to save. Sending MIDI clock to the device would put a pulse in time with the sequencer.

**Too slow for a playhead.** A 1/16 step at 120 bpm is 125 ms, an eighth of a pulse, so a pulsing head
spends most of its visit dark and the grid reads as lagging. Show heads as held colours.

Bulk form (p15) — up to 81 entries in one message, addressed by the same indices in any layout:

    F0 00 20 29 02 0C 03   <type> <index> <data...>   [ ... ]   F7
        type 0  static     data: palette index
        type 1  flashing   data: colour B, colour A
        type 2  pulsing    data: palette index
        type 3  RGB        data: red, green, blue, 0-127 each

## Palette (p12)

Most hues come in fours — pale, full, darker, dim — for example 4 5 6 7 for red and 44 45 46 47 for
blue.

    0 off        1 dark grey    2 light grey    3 white
    5 red        13 yellow      19 green (21 brighter, 23 dimmer)
    37 turquoise                45 blue (47 dim)

Seen on this Launchpad *(hardware)*: 0, 3, 5, 19 and 45. The others come from the manual's chart only.
