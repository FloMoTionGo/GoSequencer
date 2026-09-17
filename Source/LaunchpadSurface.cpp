#include "LaunchpadSurface.h"
#include "PluginProcessor.h"

#include <algorithm>

namespace
{
    //  What the device does with a colour: the channel of the note that carries
    //  it. See claude_instructions/reference/launchpad-x-protocol.md.
    constexpr std::uint8_t staticLed = 0, flashLed = 1, pulseLed = 2;

    /** Palette indices, from the colour chart in Novation's Launchpad X
        Programmer's Reference Manual (page 12). 0, 3, 5, 19 and 45 have also been
        seen on this Launchpad itself; 10, 13, 41 and 47 are only on the chart so
        far - if something comes up the wrong colour, these lines are the ones
        to correct. */
    namespace palette
    {
        constexpr std::uint8_t off = 0, faint = 1, grey = 2, white = 3, red = 5, green = 19, blue = 45;

        constexpr std::uint8_t dimBlue = 47;

        //  where a playhead is - held colours, not pulses (see buildFrame)
        constexpr std::uint8_t headOnEmpty = 10, headOnBlack = 41, headOnWhite = 13;

        constexpr std::uint8_t blackStone = blue,  blackSpent = dimBlue;
        constexpr std::uint8_t whiteStone = white, whiteSpent = grey;

        constexpr std::uint8_t idle = faint, on = green, refused = red;
    }

    constexpr int tickHz = 30;

    //  the device ignores what it is told for about 150 ms after programmer mode
    constexpr int settleTickCount = 5;

    //  about half a second of red, as the board view's flash lasts
    constexpr int refusalTickCount = 15;

    //  about 0.7 s: long enough not to happen by accident, short enough not to
    //  feel like waiting
    constexpr int clearHoldTicks = 21;

    //  every five seconds everything is sent again, whether it changed or not -
    //  a cheap answer to a message the device dropped
    constexpr int resyncEvery = 5 * tickHz;

    //==============================================================================
    void setParameter (juce::RangedAudioParameter* parameter, float normalised)
    {
        if (parameter == nullptr)
            return;

        //  a gesture, so a host that is recording automation records this as a
        //  move of the control rather than a value appearing from nowhere
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (normalised);
        parameter->endChangeGesture();
    }

    bool isOn (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        const auto* parameter = state.getParameter (id);
        return parameter != nullptr && parameter->getValue() >= 0.5f;
    }

    void toggle (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        if (auto* parameter = state.getParameter (id))
            setParameter (parameter, parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
    }

    /** Moves a choice along by delta, stopping at either end. */
    void stepChoice (juce::AudioProcessorValueTreeState& state, const char* id, int delta)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
        {
            const int next = juce::jlimit (0, choice->choices.size() - 1, choice->getIndex() + delta);

            if (next != choice->getIndex())
                setParameter (choice, choice->convertTo0to1 ((float) next));
        }
    }

    /** Moves a choice along by one, going round from the last to the first. */
    void cycleChoice (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            setParameter (choice, choice->convertTo0to1 ((float) ((choice->getIndex() + 1) % choice->choices.size())));
    }

    juce::StringArray namesOf (const juce::Array<juce::MidiDeviceInfo>& devices)
    {
        juce::StringArray names;

        for (const auto& device : devices)
            names.add (device.name);

        return names;
    }
}

//==============================================================================
LaunchpadSurface::LaunchpadSurface (GoSequencerProcessor& p)
    : processor (p)
{
}

LaunchpadSurface::~LaunchpadSurface()
{
    close();
    deviceListConnection.reset();
}

juce::StringArray LaunchpadSurface::availableInputs()  { return namesOf (juce::MidiInput::getAvailableDevices()); }
juce::StringArray LaunchpadSurface::availableOutputs() { return namesOf (juce::MidiOutput::getAvailableDevices()); }

LaunchpadSurface::Ports LaunchpadSurface::findLaunchpad()
{
    //  The grid talks on the Launchpad's second interface - "LPX MIDI" in
    //  Novation's manual. Windows lists that one as MIDIIN2 (LPX MIDI) and
    //  MIDIOUT2 (LPX MIDI), and puts a bare "LPX MIDI" above it which is the
    //  FIRST interface, the DAW one. A name match alone lands on that, being
    //  listed first, so the numbered pair is looked for before anything else.
    //  Where there is no such pair, the second interface is the plain name.
    const auto pick = [] (const juce::StringArray& names, const char* numberedPrefix)
    {
        for (const auto& name : names)
            if (name.startsWithIgnoreCase (numberedPrefix) && name.containsIgnoreCase ("LPX MIDI"))
                return name;

        for (const auto& name : names)
            if (name.containsIgnoreCase ("LPX MIDI"))
                return name;

        return juce::String();
    };

    return { pick (availableInputs(), "MIDIIN"), pick (availableOutputs(), "MIDIOUT") };
}

void LaunchpadSurface::setPorts (Ports ports)
{
    if (ports == wanted && (ports.isEmpty() || isOpen()))
        return;

    wanted = ports;

    if (! wanted.isEmpty() && ! listening)
    {
        //  called on the message thread whenever a device comes or goes - which
        //  is how a Launchpad plugged in after the session opened is taken up
        deviceListConnection = juce::MidiDeviceListConnection::make ([this] { reconnect(); });
        listening = true;
    }
    else if (wanted.isEmpty() && listening)
    {
        deviceListConnection.reset();
        listening = false;
    }

    reconnect();
}

juce::String LaunchpadSurface::status() const
{
    return statusText;
}

void LaunchpadSurface::flashRefusal (int boardIndex)
{
    refusalIndex = boardIndex;
    refusalTicks = refusalTickCount;
}

//==============================================================================
void LaunchpadSurface::reconnect()
{
    const auto inputs  = juce::MidiInput::getAvailableDevices();
    const auto outputs = juce::MidiOutput::getAvailableDevices();

    const auto* inMatch  = std::find_if (inputs.begin(), inputs.end(),
                                         [this] (const juce::MidiDeviceInfo& d) { return d.name == wanted.in; });
    const auto* outMatch = std::find_if (outputs.begin(), outputs.end(),
                                         [this] (const juce::MidiDeviceInfo& d) { return d.name == wanted.out; });

    const bool present = wanted.in.isNotEmpty() && wanted.out.isNotEmpty()
                           && inMatch != inputs.end() && outMatch != outputs.end();

    if (present && input != nullptr && output != nullptr
         && input->getIdentifier()  == inMatch->identifier
         && output->getIdentifier() == outMatch->identifier)
        return;             //  already driving it

    close();

    if (wanted.isEmpty())
    {
        updateStatus ({});
        return;
    }

    if (! present)
    {
        updateStatus ("waiting for " + wanted.out + " - is the Launchpad plugged in?");
        return;             //  not there yet: the device list will say when it is
    }

    output = juce::MidiOutput::openDevice (outMatch->identifier);
    input  = juce::MidiInput::openDevice (inMatch->identifier, this);

    if (output == nullptr || input == nullptr)
    {
        //  On Windows a port belongs to whoever opened it first. Retrying here
        //  would only fail again; the device list calls back when that changes.
        input.reset();
        output.reset();
        updateStatus (wanted.out + " is busy - the DAW or another Go Sequencer has it. "
                                   "In Live, set its control surface to None.");
        return;
    }

    //  nothing can be queued yet - the input has not been started - so this is
    //  the one moment the queue may be reset without a race
    fifo.reset();

    open.store (true, std::memory_order_relaxed);
    input->start();

    enterProgrammerMode();

    lastBoardSize = 0;
    resyncTicks = 0;
    startTimerHz (tickHz);

    updateStatus (describeOpen());
}

void LaunchpadSurface::close()
{
    //  The order is the contract - see the header. Nothing new is queued or
    //  drawn once the flag is down; stopping the input waits out a callback
    //  already running, which is what makes the queue safe to leave behind.
    open.store (false, std::memory_order_relaxed);
    stopTimer();

    if (input != nullptr)
        input->stop();

    if (output != nullptr)
        leaveProgrammerMode();

    input.reset();
    output.reset();

    cancelPendingUpdate();
    fifo.reset();

    sent.fill ({});
    settleTicks = 0;
    clearHeldTicks = -1;
    refusalIndex = -1;
    refusalTicks = 0;
}

void LaunchpadSurface::enterProgrammerMode()
{
    static constexpr std::uint8_t programmerOn[] { 0xf0, 0x00, 0x20, 0x29, 0x02, 0x0c, 0x0e, 0x01, 0xf7 };

    output->sendMessageNow (juce::MidiMessage (programmerOn, (int) sizeof (programmerOn)));

    //  whatever was lit before is unknown now, so the first frame sends it all
    sent.fill ({});
    forceRedraw = true;
    settleTicks = settleTickCount;
}

void LaunchpadSurface::leaveProgrammerMode()
{
    static constexpr std::uint8_t programmerOff[] { 0xf0, 0x00, 0x20, 0x29, 0x02, 0x0c, 0x0e, 0x00, 0xf7 };

    //  lights out first, while the device still listens to them - told to leave
    //  programmer mode before this, it would keep showing the last frame
    for (int index = 11; index < lpx::indexCount; ++index)
        if (lpx::isPad (index) || lpx::isScene (index) || lpx::isTop (index))
            output->sendMessageNow (juce::MidiMessage (0x90, index, 0));

    output->sendMessageNow (juce::MidiMessage (programmerOff, (int) sizeof (programmerOff)));
}

//==============================================================================
void LaunchpadSurface::timerCallback()
{
    if (! isOpen() || output == nullptr)
        return;

    if (settleTicks > 0)
    {
        --settleTicks;
        return;
    }

    if (clearHeldTicks >= 0 && ++clearHeldTicks >= clearHoldTicks)
    {
        clearHeldTicks = -1;

        //  in a game, an empty board is a new game - which may mean they open
        if (processor.matchActive())
            processor.newMatch();
        else
            processor.clearBoard();
    }

    if (refusalTicks > 0 && --refusalTicks == 0)
        refusalIndex = -1;

    const int size = processor.boardSize();

    if (size != lastBoardSize)
    {
        lastBoardSize = size;
        forceRedraw = true;
        updateStatus (describeOpen());
    }

    if (++resyncTicks >= resyncEvery)
    {
        resyncTicks = 0;
        forceRedraw = true;
    }

    Frame next;
    buildFrame (next);
    sendFrame (next, forceRedraw);
    forceRedraw = false;
}

void LaunchpadSurface::buildFrame (Frame& frame) const
{
    frame.fill ({});

    const auto lit = [&frame] (int index, std::uint8_t colour, std::uint8_t type = staticLed)
    {
        frame[(size_t) index] = { type, colour, 0 };
    };

    //  ---- the board --------------------------------------------------------
    const int size = processor.boardSize();

    if (lpx::canShow (size))
    {
        //  the star points faintly, so there is something to find your way by
        std::array<int, go::maxHoshi> stars {};
        const int starCount = go::hoshiPoints (size, stars);

        for (int s = 0; s < starCount; ++s)
            lit (lpx::padIndexFor (stars[(size_t) s], size), palette::faint);

        for (int idx = 0; idx < size * size; ++idx)
        {
            const auto stone = processor.stoneAt (idx);

            if (stone == go::Stone::none)
                continue;

            const bool spent = processor.stoneIsSpent (idx);

            lit (lpx::padIndexFor (idx, size),
                 stone == go::Stone::black ? (spent ? palette::blackSpent : palette::blackStone)
                                           : (spent ? palette::whiteSpent : palette::whiteStone));
        }

        //  The playheads, only while the sequencer runs - a head that is not
        //  moving says nothing. Held, not pulsed: the device pulses on its own
        //  two beat clock, a second a pulse at 120 bpm and far slower than a step,
        //  so a pulsing head spent most of its short visit to a pad dark and the
        //  grid looked as if it lagged. A head is a colour of its own instead, one
        //  that still says what it is on - a lighter blue over a black stone,
        //  yellow over a white one, and over an empty point a faint orange, the
        //  colour of the playhead on the screen.
        if (processor.isRunning())
        {
            const int heads = processor.headCount();

            for (int h = 0; h < heads; ++h)
            {
                const int cell = heads > 1
                    ? processor.headCellAt (h, processor.headPosition (h))
                    : processor.spiralAt (juce::jlimit (0, juce::jmax (0, processor.stepCount() - 1),
                                                        processor.currentStep()));

                const int pad = lpx::padIndexFor (cell, size);

                if (pad < 0)
                    continue;

                const auto under = processor.stoneAt (cell);

                lit (pad, under == go::Stone::black ? palette::headOnBlack
                        : under == go::Stone::white ? palette::headOnWhite
                                                    : palette::headOnEmpty);
            }
        }

        //  last, so a refusal shows whatever else is on that point
        if (refusalIndex >= 0 && refusalTicks > 0)
        {
            const int pad = lpx::padIndexFor (refusalIndex, size);

            if (pad >= 0)
                lit (pad, palette::refused);
        }
    }

    //  ---- the top row ------------------------------------------------------
    auto& state = processor.apvts;

    const auto onOff = [&state] (const char* id) { return isOn (state, id) ? palette::on : palette::idle; };
    const auto usable = [] (bool b) { return b ? palette::idle : palette::off; };

    lit (lpx::topIndex (0), palette::idle);                                    //  91  step rate faster
    lit (lpx::topIndex (1), palette::idle);                                    //  92  step rate slower
    lit (lpx::topIndex (2), usable (processor.hasGame()));                     //  93  a move back
    lit (lpx::topIndex (3), usable (processor.hasGame()));                     //  94  a move on
    lit (lpx::topIndex (4), onOff ("gameRun"));                                //  95  run game
    lit (lpx::topIndex (5), onOff ("freeRun"));                                //  96  free run

    //  97: the colour the next press places, in that colour - in a game the
    //  colours alternate whatever Place says, so the button has nothing to show
    lit (lpx::topIndex (6), processor.matchActive() ? palette::off
                          : processor.colourForNextMove() == go::Stone::black ? palette::blackStone
                                                                              : palette::whiteStone);

    lit (lpx::topIndex (7), clearHeldTicks >= 0 ? palette::refused : palette::idle);   //  98  clear, held

    //  ---- the right hand column, top to bottom ------------------------------
    lit (lpx::sceneIndex (0), processor.matchActive() ? palette::on : palette::idle);  //  89  play against them
    lit (lpx::sceneIndex (1), usable (processor.yourTurn()));                          //  79  pass
    lit (lpx::sceneIndex (2), usable (processor.eraseAllowed() && processor.lastMove() >= 0));  //  69  lift the last stone
    lit (lpx::sceneIndex (3), onOff ("gameLoop"));                             //  59  loop
    lit (lpx::sceneIndex (4), onOff ("waveReplay"));                           //  49  wave replay
    lit (lpx::sceneIndex (5), palette::idle);                                  //  39  move rate faster
    lit (lpx::sceneIndex (6), palette::idle);                                  //  29  move rate slower
    lit (lpx::sceneIndex (7), palette::idle);                                  //  19  redraw
}

void LaunchpadSurface::sendFrame (const Frame& next, bool everything)
{
    for (int index = 11; index < lpx::indexCount; ++index)
    {
        if (! (lpx::isPad (index) || lpx::isScene (index) || lpx::isTop (index)))
            continue;

        const auto& led = next[(size_t) index];

        //  compared on the type as well as the colours, so a light that changes
        //  only in how it is lit is still sent
        if (! everything && led == sent[(size_t) index])
            continue;

        output->sendMessageNow (juce::MidiMessage (0x90 | led.type, index, led.colour));
        sent[(size_t) index] = led;
    }
}

//==============================================================================
void LaunchpadSurface::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    //  The device thread. Three bytes into the queue and a request for the
    //  message thread, and nothing else: no allocation, no lock, no board.
    if (message.getRawDataSize() != 3)
        return;             //  a SysEx reply, or anything else that is not a press

    const auto* raw = message.getRawData();

    int start1, size1, start2, size2;
    fifo.prepareToWrite (1, start1, size1, start2, size2);

    if (size1 < 1)
        return;             //  full: nobody is reading, and the next frame will say so anyway

    queue[(size_t) start1] = ((std::uint32_t) raw[0] << 16) | ((std::uint32_t) raw[1] << 8) | (std::uint32_t) raw[2];
    fifo.finishedWrite (1);

    triggerAsyncUpdate();
}

void LaunchpadSurface::handleAsyncUpdate()
{
    for (;;)
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (1, start1, size1, start2, size2);

        if (size1 < 1)
            return;

        const auto packed = queue[(size_t) start1];
        fifo.finishedRead (1);

        if (! isOpen())
            continue;       //  drained, but a closed surface acts on nothing

        const int type  = (int) (packed >> 16) & 0xf0;
        const int data1 = (int) (packed >> 8)  & 0x7f;
        const int data2 = (int)  packed        & 0x7f;

        //  pads are notes and the edge is control change; a note on of velocity
        //  0 is the pad being let go, which means nothing here
        if (type == 0x90 && data2 > 0 && lpx::isPad (data1))
            handlePad (data1);
        else if (type == 0xb0 && (lpx::isTop (data1) || lpx::isScene (data1)))
            handleButton (data1, data2 > 0);
    }
}

void LaunchpadSurface::handlePad (int index)
{
    const int size = processor.boardSize();
    const int idx  = lpx::boardIndexFor (index, size);

    if (idx < 0)
        return;             //  the grid is dark on a board it cannot show

    //  as on the screen: a press on a stone lifts it - except in a game, whose
    //  position is its record
    if (processor.stoneAt (idx) != go::Stone::none)
    {
        if (processor.eraseAllowed())
            processor.eraseStone (idx);
        else
            flashRefusal (idx);

        return;
    }

    //  in a game, a press while they are to move would play their stone for them
    if (processor.matchActive() && ! processor.yourTurn())
    {
        flashRefusal (idx);
        return;
    }

    if (processor.placeStone (idx) != go::MoveResult::ok)
        flashRefusal (idx);
}

void LaunchpadSurface::handleButton (int index, bool pressed)
{
    //  98 acts on a hold, so it has to hear the release as well
    if (index == lpx::topIndex (7))
    {
        clearHeldTicks = pressed ? 0 : -1;
        return;
    }

    if (! pressed)
        return;             //  everything else acts the moment it goes down

    auto& state = processor.apvts;

    switch (index)
    {
        //  Rate runs 1/1 to 1/32, so faster is up the list. Move Rate runs 1/4 to
        //  one lap, so faster is down it - hence the signs look the wrong way round.
        case lpx::topIndex (0):   stepChoice (state, "rate", +1);           break;
        case lpx::topIndex (1):   stepChoice (state, "rate", -1);           break;
        case lpx::topIndex (2):   processor.nudgeGamePosition (-1);         break;
        case lpx::topIndex (3):   processor.nudgeGamePosition (+1);         break;
        case lpx::topIndex (4):   toggle (state, "gameRun");                break;
        case lpx::topIndex (5):   toggle (state, "freeRun");                break;
        case lpx::topIndex (6):   cycleChoice (state, "colourMode");        break;

        case lpx::sceneIndex (0): toggle (state, "aiOpponent");             break;
        case lpx::sceneIndex (1): processor.passMove();                     break;
        case lpx::sceneIndex (2):
            if (processor.eraseAllowed() && processor.lastMove() >= 0)
                processor.eraseStone (processor.lastMove());
            break;
        case lpx::sceneIndex (3): toggle (state, "gameLoop");               break;
        case lpx::sceneIndex (4): toggle (state, "waveReplay");             break;
        case lpx::sceneIndex (5): stepChoice (state, "gameRate", -1);       break;
        case lpx::sceneIndex (6): stepChoice (state, "gameRate", +1);       break;

        //  put the surface right: back into programmer mode and every light
        //  sent again, for when the device has been knocked out of step
        case lpx::sceneIndex (7):
            if (output != nullptr)
                enterProgrammerMode();
            break;

        default: break;
    }
}

//==============================================================================
void LaunchpadSurface::updateStatus (juce::String text)
{
    if (text == statusText)
        return;

    statusText = std::move (text);

    if (onStatusChanged != nullptr)
        onStatusChanged();
}

juce::String LaunchpadSurface::describeOpen() const
{
    if (! lpx::canShow (processor.boardSize()))
        return "driving " + wanted.out + " - the pads stay dark: only an 8 x 8 board fits the grid";

    return "driving " + wanted.out;
}
