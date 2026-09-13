#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <bitset>
#include <memory>

//==============================================================================
/** Sends the sequencer's notes to one of the system's MIDI ports as well as to
    the host, on the channels they were given.

    This exists because of how some hosts route MIDI between their own tracks.
    Ableton Live puts every note a plugin sends onto channel 1 before another
    track receives it, so the per-colour and per-playhead channels cannot split
    anything there. A port keeps them: a virtual one such as loopMIDI loops the
    notes straight back into the host, where a track listening to it can pick
    out a single channel.

    Threading. JUCE lists, opens and closes ports on the message thread, so this
    does too. The audio thread never touches the port: push() stamps each note
    with the time it is due and writes it into a lock free queue, dropping what
    does not fit rather than waiting. A high resolution timer - running only
    while a port is open, since a process only gets sixteen of them on Windows -
    sends each note once its time has come, which keeps the spacing of the notes
    rather than the spacing of the audio blocks they were made in.
*/
class MidiPortOut final : private juce::HighResolutionTimer
{
public:
    MidiPortOut();
    ~MidiPortOut() override;

    //  ---- message thread ---------------------------------------------------

    /** The names of the ports a note can be sent to right now. */
    static juce::StringArray availablePorts();

    /** Sends to the port with this name, or to none for an empty name. A port
        that is not there yet is opened as soon as it appears, and one that goes
        away is waited for in the same way. */
    void setPort (const juce::String& name);

    /** The port asked for, whether or not it is open. */
    juce::String getPort() const { return wantedName; }

    //  ---- any thread -------------------------------------------------------

    bool isOpen() const noexcept { return open.load (std::memory_order_relaxed); }

    //  ---- audio thread -----------------------------------------------------

    /** Queues the notes of one processed block. blockStartMs is
        Time::getMillisecondCounterHiRes() read as the block began, and each
        note is due as far after that as its sample position says. */
    void push (const juce::MidiBuffer& midi, double blockStartMs, double sampleRate) noexcept;

private:
    struct Event
    {
        double dueMs = 0.0;
        std::array<juce::uint8, 3> bytes {};
    };

    void hiResTimerCallback() override;

    /** Opens the wanted port if it is there and not open already, and closes
        the open one if it is no longer wanted or has gone away. */
    void reconnect();

    /** Stops sending, ends every note still sounding on the port, and lets it go. */
    void close();

    void send (const Event&);
    void discardQueued() noexcept;

    juce::String wantedName;
    std::unique_ptr<juce::MidiOutput> port;

    //  only held while a port is wanted, so an instance that never uses one
    //  never asks the system about its devices
    juce::MidiDeviceListConnection deviceListConnection;
    bool listening = false;

    std::atomic<bool> open { false };

    static constexpr int queueSize = 4096;
    juce::AbstractFifo fifo { queueSize };
    std::array<Event, (size_t) queueSize> queue {};

    //  notes the port has had a note on for and no note off yet, so closing it
    //  never leaves one hanging. The timer thread's while the port is open, the
    //  message thread's once the timer has stopped.
    std::array<std::bitset<128>, 16> sounding {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiPortOut)
};
