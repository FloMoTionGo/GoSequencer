#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include "LaunchpadMap.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>

class GoSequencerProcessor;

//==============================================================================
/** A Launchpad X as a view of the board and a way to play on it.

    The grid shows the stones, the playheads and the point a refused move was
    tried on; pressing a pad places or lifts a stone, and the buttons round the
    edge drive the sequencer. It is the same board the screen shows - there is no
    second copy of anything - so whichever one you touch, both follow.

    The grid is only a board when the board is the grid's size, which is what the
    8x8 is for (see go::supportedSizes). On a bigger board the pads go dark
    rather than show a corner of it: sixty four lights cannot stand for eighty
    one points without hiding some, and a surface whose whole display is the
    board cannot afford that.

    Threading. Three threads, and only one of them may touch the board.

      message thread  opens and closes both ports, runs the thirty times a
                      second tick that redraws the lights, and turns a queued
                      press into a move on the processor. close() and
                      handleAsyncUpdate() are both its own, so they cannot
                      interleave - which is the whole safety argument for the
                      input path below.

      device thread   juce::MidiInput's callback. It may not allocate, block, or
                      look at the board: it copies three bytes into a lock free
                      queue and posts an async update.

      audio thread    never involved. Nothing here is in the signal path, which
                      is why this can take its time over a SysEx where
                      MidiPortOut cannot.
*/
class LaunchpadSurface final : private juce::Timer,
                               private juce::MidiInputCallback,
                               private juce::AsyncUpdater
{
public:
    explicit LaunchpadSurface (GoSequencerProcessor&);
    ~LaunchpadSurface() override;

    //  ---- message thread ---------------------------------------------------

    /** A Launchpad speaks on two ports, and on Windows they are not named the
        same - the grid is the second interface, so it comes up as MIDIIN2 (LPX
        MIDI) against MIDIOUT2 (LPX MIDI). One name cannot give the other, so
        both are kept. */
    struct Ports
    {
        juce::String in, out;

        bool operator== (const Ports& other) const { return in == other.in && out == other.out; }
        bool operator!= (const Ports& other) const { return ! operator== (other); }
        bool isEmpty() const                       { return in.isEmpty() && out.isEmpty(); }
    };

    static juce::StringArray availableInputs();
    static juce::StringArray availableOutputs();

    /** The pair that looks like a Launchpad's grid, or two empty strings. */
    static Ports findLaunchpad();

    /** Talks to this pair, or to nothing for empty names. A device that is not
        plugged in yet is taken up as soon as it appears, and one that goes away
        is waited for in the same way - the same bargain MidiPortOut makes with
        loopMIDI. */
    void setPorts (Ports);
    Ports getPorts() const { return wanted; }

    /** A sentence for the editor's status line. It names what is wrong when
        something is, since on Windows a port can be perfectly present and still
        refuse to open because the DAW or another instance of this plugin got
        there first. */
    juce::String status() const;

    /** Called on the message thread when status() would answer differently. */
    std::function<void()> onStatusChanged;

    /** One press worth of red on a point, for a move the rules refused - the
        same feedback the board view flashes. */
    void flashRefusal (int boardIndex);

    /** Redraws every light on the next tick, whatever it looks like now. */
    void refresh() { forceRedraw = true; }

    //  ---- any thread -------------------------------------------------------

    /** Whether the grid is being driven right now. */
    bool isOpen() const noexcept { return open.load (std::memory_order_relaxed); }

private:
    //==============================================================================
    /** One light. The type is what the device should do with the colour -
        hold it, flash between two, or pulse - and it is part of what a frame is
        compared on, so a light that changes only in how it is lit is still sent.
        The device keeps flashing and pulsing in time by itself, off MIDI clock or
        at 120 bpm without one: too slow to show a playhead, which is why a head
        is a held colour. */
    struct Led
    {
        std::uint8_t type = 0, colour = 0, alternate = 0;

        bool operator== (const Led& o) const
        {
            return type == o.type && colour == o.colour && alternate == o.alternate;
        }

        bool operator!= (const Led& o) const { return ! operator== (o); }
    };

    using Frame = std::array<Led, (size_t) lpx::indexCount>;

    //  ---- message thread ---------------------------------------------------

    void timerCallback() override;
    void handleAsyncUpdate() override;

    /** Opens the wanted pair if it is there and not open already, and closes
        what is open if it is no longer wanted or has gone away. */
    void reconnect();

    /** Stops driving the surface and gives both ports back. The order matters
        and is the safety contract: the flag goes down first so nothing new is
        queued or drawn, then the timer stops - which waits out a tick already
        running - then the input stops, which waits out a device callback and is
        what makes the queue safe to leave behind. Only then are the lights
        cleared and programmer mode left, in that order, because a Launchpad that
        is told to leave first keeps whatever it was showing. */
    void close();

    void enterProgrammerMode();
    void leaveProgrammerMode();

    /** What the surface should look like, read from the processor's lock free
        mirrors. Touches nothing that could block. */
    void buildFrame (Frame&) const;

    /** Sends what differs from the last frame, or all of it when asked. */
    void sendFrame (const Frame&, bool everything);

    void handlePad    (int index);
    void handleButton (int index, bool pressed);

    void updateStatus (juce::String);

    /** What the status line says while the surface is being driven. */
    juce::String describeOpen() const;

    //  ---- device thread ----------------------------------------------------

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    //==============================================================================
    GoSequencerProcessor& processor;

    Ports wanted;
    std::unique_ptr<juce::MidiInput>  input;
    std::unique_ptr<juce::MidiOutput> output;

    //  only held while a pair is wanted, so an instance that never uses a
    //  controller never asks the system about its devices
    juce::MidiDeviceListConnection deviceListConnection;
    bool listening = false;

    std::atomic<bool> open { false };

    //  the device ignores what it is told for a moment after being put into
    //  programmer mode, so the first draw waits a few ticks rather than sleeping
    //  on the message thread and stalling the host's window
    int settleTicks = 0;

    //  presses, on their way from the device thread to the message thread.
    //  Small on purpose: a queue that has overflowed is a surface nobody is
    //  reading, and the frames that follow will put it right anyway.
    static constexpr int queueSize = 256;
    juce::AbstractFifo fifo { queueSize };
    std::array<std::uint32_t, (size_t) queueSize> queue {};

    Frame sent {};
    bool  forceRedraw = true;
    int   lastBoardSize = 0;
    int   resyncTicks = 0;

    int refusalIndex = -1, refusalTicks = 0;

    //  Clear is on a hold, since the button sits beside four that are pressed
    //  all the time: -1 while it is up, else how many ticks it has been down
    int clearHeldTicks = -1;

    juce::String statusText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaunchpadSurface)
};
