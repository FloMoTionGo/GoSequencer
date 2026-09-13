#include "MidiPortOut.h"

#include <algorithm>

namespace
{
    /** A note on this late is dropped rather than sent. It means the timer was
        starved or the port has only just opened, and a burst of stale notes is
        worse than a gap. A note off is always sent, so nothing is left hanging. */
    constexpr double staleAfterMs = 100.0;
}

//==============================================================================
MidiPortOut::MidiPortOut() = default;

MidiPortOut::~MidiPortOut()
{
    close();
    deviceListConnection.reset();
}

juce::StringArray MidiPortOut::availablePorts()
{
    juce::StringArray names;

    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        names.add (device.name);

    return names;
}

void MidiPortOut::setPort (const juce::String& name)
{
    if (name == wantedName && (name.isEmpty() || isOpen()))
        return;

    wantedName = name;

    if (wantedName.isNotEmpty() && ! listening)
    {
        //  called on the message thread whenever a device comes or goes - which
        //  is how a port created after the session opened gets picked up
        deviceListConnection = juce::MidiDeviceListConnection::make ([this] { reconnect(); });
        listening = true;
    }
    else if (wantedName.isEmpty() && listening)
    {
        deviceListConnection.reset();
        listening = false;
    }

    reconnect();
}

void MidiPortOut::reconnect()
{
    const auto devices = juce::MidiOutput::getAvailableDevices();

    const auto* match = std::find_if (devices.begin(), devices.end(),
                                      [this] (const juce::MidiDeviceInfo& device) { return device.name == wantedName; });

    const bool present = wantedName.isNotEmpty() && match != devices.end();

    if (present && port != nullptr && port->getIdentifier() == match->identifier)
        return;             //  already sending to it

    close();

    if (! present)
        return;             //  not there yet: the device list will say when it is

    port = juce::MidiOutput::openDevice (match->identifier);

    if (port == nullptr)
        return;

    //  anything still queued was pushed for a port that has since closed
    discardQueued();

    open.store (true, std::memory_order_relaxed);
    startTimer (1);
}

void MidiPortOut::close()
{
    //  The audio thread stops queueing first, then the timer stops sending -
    //  stopTimer() waits out a callback in progress - and only after that is the
    //  port the message thread's to finish off.
    open.store (false, std::memory_order_relaxed);
    stopTimer();

    if (port != nullptr)
    {
        for (int channel = 0; channel < 16; ++channel)
            for (int note = 0; note < 128; ++note)
                if (sounding[(size_t) channel][(size_t) note])
                    port->sendMessageNow (juce::MidiMessage::noteOff (channel + 1, note));

        port.reset();
    }

    for (auto& notes : sounding)
        notes.reset();

    discardQueued();
}

//==============================================================================
void MidiPortOut::push (const juce::MidiBuffer& midi, double blockStartMs, double sampleRate) noexcept
{
    if (! isOpen() || sampleRate <= 0.0)
        return;

    const double msPerSample = 1000.0 / sampleRate;

    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 1 || metadata.numBytes > 3)
            continue;           //  the sequencer only ever sends notes

        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 < 1)
            return;             //  full: drop the rest of the block rather than wait

        auto& event = queue[(size_t) start1];
        event.dueMs = blockStartMs + metadata.samplePosition * msPerSample;
        event.bytes = {};
        std::copy (metadata.data, metadata.data + metadata.numBytes, event.bytes.begin());

        fifo.finishedWrite (1);
    }
}

void MidiPortOut::hiResTimerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();

    for (;;)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);

        if (size1 < 1)
            return;

        const auto& event = queue[(size_t) start1];

        //  blocks arrive in order and a block's notes are sorted, so nothing
        //  behind a note that is not due yet is due either
        if (event.dueMs > now)
            return;

        send (event);
        fifo.finishedRead (1);
    }
}

void MidiPortOut::send (const Event& event)
{
    const int type    = event.bytes[0] & 0xf0;
    const int channel = event.bytes[0] & 0x0f;
    const int note    = event.bytes[1] & 0x7f;

    const bool noteOn  = type == 0x90 && event.bytes[2] > 0;
    const bool noteOff = type == 0x80 || (type == 0x90 && event.bytes[2] == 0);

    if (noteOn)
    {
        if (juce::Time::getMillisecondCounterHiRes() - event.dueMs > staleAfterMs)
            return;

        sounding[(size_t) channel].set ((size_t) note);
    }
    else if (noteOff)
    {
        if (! sounding[(size_t) channel][(size_t) note])
            return;             //  its note on was dropped, or went to a port since closed

        sounding[(size_t) channel].reset ((size_t) note);
    }

    port->sendMessageNow (juce::MidiMessage (event.bytes[0], event.bytes[1], event.bytes[2]));
}

void MidiPortOut::discardQueued() noexcept
{
    fifo.finishedRead (fifo.getNumReady());
}
