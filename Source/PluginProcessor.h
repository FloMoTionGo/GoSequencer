#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

#include "GoBoard.h"
#include "SgfParser.h"

//==============================================================================
/** Step sequencer that reads a Go board as a pattern. It has two ways of
    walking it, chosen with the playMode parameter:

      Spiral      one playhead, clockwise from the top left corner and winding
                  in to tengen. Black and white have a channel each, so the
                  colour it passed can be routed as well as heard.

      Quads       one playhead per quadrant, each spiralling around that
                  quadrant's star point - the san-san point on a 9x9. The four
                  blocks share the middle row and column, so the heads meet on
                  the same edge in the centre. They can wind out from the star
                  point or in to it.

      Polyrhythm  one playhead per concentric ring, tengen aside: four of them
                  on a 9x9, six on a 13x13. They share the step clock, but the
                  rings are 32, 24, 16 and 8 points around, so they come back
                  into phase only every 96 steps (480 on a 13x13). Every ring
                  has its own channel and its own transpose, and they all sound
                  together, so the board plays as a chord rather than a line.

    In every mode a stone has a lifespan. It is counted either in steps of
    the sequencer clock, or in stones placed after it - so with a life of 15 the
    first stone falls silent as the sixteenth lands, whatever tempo the heads or
    the game record are running at. Once it is spent it stays on the board - it
    still blocks points and still lives or dies by the rules - but the playheads
    pass over it in silence, and the editor draws it faded.

    Age is a stone's own property, never the board's: each one remembers the
    count it landed on, and the two counts it is measured against only ever
    climb. So moving the lifespan while the sequencer runs re-reads every stone
    against the age it has already reached - some fall silent, some come back -
    instead of dealing the whole board a fresh life from the moment of the
    change, and stopping the transport or letting the host loop leaves the ages
    where they stood.

    Channels are set one at a time and never derived from one another: spiral
    has a channel for black and one for white, and the multi head modes have one
    per playhead - four on a 9x9, six rings on a 13x13. They start out on 1..6,
    but nothing stops two heads sharing a channel or the whole board sitting on
    one. The editor keeps them folded away, since a set that never leaves channel
    1 has no reason to look at them.

    A game record can be loaded on top of that: the moves of a real game are
    then played onto the board at their own speed while the spiral keeps
    running, so the pattern is rewritten by the game as it goes.

    Threading: the board, the loaded game and the playback position all live
    behind boardLock. The message thread takes it outright; the audio thread
    only ever tries for it and skips a block rather than waiting. Readers that
    must not block - the editor, and the step trigger itself - use the lock free
    mirror in stones[] instead.
*/
class GoSequencerProcessor final : public juce::AudioProcessor,
                                   private juce::AudioProcessorValueTreeState::Listener,
                                   private juce::AsyncUpdater
{
public:
    GoSequencerProcessor();
    ~GoSequencerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                            { return true; }

    const juce::String getName() const override                { return JucePlugin_Name; }
    bool acceptsMidi() const override                          { return true; }
    bool producesMidi() const override                         { return true; }
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    //  Board access. Everything here is called from the message thread.

    /** Plays a stone at idx, honouring the colour mode and the rule switches.
        Returns what the rules made of it so the editor can show the refusal. */
    go::MoveResult placeStone (int idx);

    /** The sequencer's eraser: lifts a stone regardless of the rules. */
    void eraseStone (int idx);

    void clearBoard();

    go::Stone stoneAt (int idx) const noexcept
    {
        return (idx >= 0 && idx < go::maxCells)
                 ? (go::Stone) stones[(size_t) idx].load (std::memory_order_relaxed)
                 : go::Stone::none;
    }

    /** The colour the next click will place. */
    go::Stone colourForNextMove() const noexcept;

    int  boardSize()      const noexcept { return activeSize.load (std::memory_order_relaxed); }
    int  stepCount()      const noexcept { const int s = boardSize(); return s * s; }
    int  spiralAt (int step) const noexcept;

    //  Rings. ringCount() is 4 on a 9x9 and 6 on a 13x13: tengen is left out.
    bool isPolyrhythm()   const noexcept;
    int  ringCount()      const noexcept { return go::ringCount (boardSize()); }

    //  Quadrants: four spirals around the corner star points.
    bool isQuads()        const noexcept;
    bool quadsWindOut()   const noexcept;

    /** How many playheads the current mode runs. */
    int  headCount()      const noexcept;

    /** How many head channels there are to assign: the widest any mode gets. */
    static constexpr int maxHeadChannels = go::maxRings;

    /** Where playhead head is sitting, and the point under it. */
    int  headPosition (int head) const noexcept
    {
        return (head >= 0 && head < go::maxRings)
                 ? headPos[(size_t) head].load (std::memory_order_relaxed)
                 : 0;
    }

    int  headCellAt (int head, int pos) const noexcept;

    /** How long one lap is, in steps: the whole spiral, or the outer ring. */
    int  cycleSteps()     const noexcept;

    /** The point that playhead ring is sitting on. */
    int  ringCellAt (int ring, int pos) const noexcept;

    /** The point playhead quad is sitting on, direction already applied. */
    int  quadCellAt (int quad, int pos) const noexcept;

    /** A stone whose lifespan has run out: still on the board, but silent. */
    bool stoneIsSpent (int idx) const noexcept;

    /** The lifespan slider's top value means "hold": stones never expire. */
    static constexpr int maxStoneLife = 128;

    int  capturedBlack()  const noexcept { return prisonersBlack.load (std::memory_order_relaxed); }
    int  capturedWhite()  const noexcept { return prisonersWhite.load (std::memory_order_relaxed); }
    int  lastMove()       const noexcept { return lastMoveIndex.load (std::memory_order_relaxed); }
    int  currentStep()    const noexcept { return step.load (std::memory_order_relaxed); }
    bool isRunning()      const noexcept { return running.load (std::memory_order_relaxed); }
    bool waveReplayOn()   const noexcept { return waveReplayParam != nullptr && waveReplayParam->get(); }

    //==============================================================================
    //  Game records. Message thread only, except the two atomics.

    /** Loads a game record. Returns an empty string on success, or a sentence
        explaining why the file could not be used. */
    juce::String loadSgf (const juce::File& file);
    juce::String loadSgfText (const juce::String& text, const juce::String& sourceName);
    void clearGame();

    bool hasGame()          const noexcept { return gameMoveTotal.load (std::memory_order_relaxed) > 0; }
    int  gameMoveCount()    const noexcept { return gameMoveTotal.load (std::memory_order_relaxed); }
    int  gamePosition()     const noexcept { return gamePositionMirror.load (std::memory_order_relaxed); }
    juce::String gameTitle() const;
    juce::String gameDetail() const;
    juce::String gameSource() const { return sourceName; }

    /** Scrubs the record: the board is rebuilt from the start up to this move. */
    void setGamePosition (int position);
    void nudgeGamePosition (int delta) { setGamePosition (gamePosition() + delta); }

    //==============================================================================
    //  Wave Replay: an alternative pacing for game record playback. Advance
    //  never pauses - every move still lands on its own Move Rate tick - but
    //  every waveGap moves after a move first landed, whatever currently sits
    //  on that point has its lifespan reset, as if it had just been placed.
    //  Different moves are born on different ticks, so their resets land on
    //  different ticks too: the effect ripples across the board one stone at
    //  a time rather than pulsing the whole board together. Loop is ignored
    //  while this is on - the game plays once through and holds at the end.
    //
    //  Stone Life is kept below Wave Gap (see clampStoneLifeToWaveGap()): a
    //  stone that could outlive a whole gap on its own would make its reset
    //  a no-op, and the wave would stop being audible.
    //
    //  Capped at maxWaveEchoes levels deep (move T's point is refreshed by
    //  echo k while T - k*waveGap >= 1, for k up to the cap) - a real-time
    //  bound, since the game-advance loop can run many times per audio block.
    static constexpr int maxWaveEchoes = 16;

    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static const std::array<double, 9>& rateInBeats();
    static juce::StringArray rateNames();
    static juce::StringArray gameRateNames();
    static juce::StringArray boardSizeNames();
    static juce::StringArray playModeNames();
    static juce::StringArray lifeModeNames();

private:
    //==============================================================================
    struct PendingNoteOff
    {
        int note = -1;
        int channel = 1;
        int samplesLeft = 0;
        bool active = false;
    };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void publishBoard();
    void applyBoardSize (int newSize);
    /** Sends one note for the stone on idx, if there is one and it is still
        inside its lifespan. Every mode goes through here, so the lifespan gate,
        the gate length, retrigger safety and the note off queue are shared. */
    void fireCell (int idx, int channel, int semitoneOffset, int offsetInBlock,
                   juce::MidiBuffer& midi, double samplesPerStep);

    /** The channel the spiral gives the stone on idx. An empty point falls
        through to black's - fireCell drops it before anything is sent. */
    int colourChannel (int idx) const noexcept;

    /** The channel playhead head owns, as set - the heads are independent, so
        this is a lookup and not an offset from a base. */
    int headChannelFor (int head) const noexcept;

    void triggerStep (int stepIndex, int offsetInBlock, juce::MidiBuffer& midi, double samplesPerStep);

    /** One tick of the step clock, counted from the start of the timeline.
        Spiral mode wraps it round the board, polyrhythm mode wraps it round
        each ring in turn and fires the lot. */
    void triggerAt (long long absStep, int offsetInBlock, juce::MidiBuffer& midi, double samplesPerStep);

    void resetPlayhead() noexcept;

    /** Whether the stone on idx has outlived its span. Both counters this
        measures against - the age clock and the placement counter - only ever
        climb and are never rebased, so a stone's age is its own and survives a
        transport stop, a loop, or the host jumping down its timeline. */
    bool isSpent (int idx, int life) const noexcept;
    void flushNoteOffs (juce::MidiBuffer& midi, int numSamples);
    void allNotesOff (juce::MidiBuffer& midi, int offsetInBlock);

    //  these expect boardLock to be held
    void resetGameLocked();
    bool advanceGameLocked();
    void rebuildBoardFromGameLocked (int position);
    /** Wave Replay: after movesPlaced (gameMovePosition, just advanced past
        the move that landed on this tick) has been reached, refreshes every
        point still echoing an earlier move - see the class doc above. */
    void applyWaveEchoesLocked (int movesPlaced);

    /** Keeps Stone Life below Wave Gap while Wave Replay is on, so a reset
        is always something a stone would otherwise have missed. Message
        thread only (routed there via handleAsyncUpdate, since the parameter
        change that asks for this can arrive on the audio thread). */
    void clampStoneLifeToWaveGap();

    go::Board board { 9 };
    juce::SpinLock boardLock;

    sgf::Game game;
    juce::String sgfText, sourceName;
    int gameMovePosition = 0;                      // guarded by boardLock

    std::array<std::atomic<std::uint8_t>, go::maxCells> stones {};
    std::atomic<int> prisonersBlack { 0 }, prisonersWhite { 0 }, lastMoveIndex { -1 };
    std::atomic<int> step { 0 };
    std::atomic<int> activeSize { 9 };
    std::atomic<int> gameMoveTotal { 0 }, gamePositionMirror { 0 };
    std::atomic<bool> running { false };
    std::atomic<int> nextAlternating { (int) go::Stone::black };

    std::array<int, go::maxCells> spiral9 {}, spiral13 {};
    std::array<std::array<int, go::maxCells>, go::quadCount> quad9 {}, quad13 {};

    //  room for every ring to hold a note at once, with headroom for the
    //  overlap when a long gate runs into the next step
    std::array<PendingNoteOff, 32> pending {};
    //  maxRings is the widest any mode gets: 6 rings beats 4 quadrants
    std::array<std::atomic<int>, go::maxRings> headPos {};

    //  Two ways of ageing a stone, both a difference between "then" and "now":
    //  bornAt/ageClock count steps the sequencer has actually played, and
    //  placedAt/placeCounter count stones laid down since. Which one is live is
    //  the lifeMode parameter. Neither counter is ever reset or rewound - that
    //  is what keeps each stone's age its own, and it is why the host's
    //  timeline, which does jump about, is not used for this.
    std::array<std::atomic<long long>, go::maxCells> bornAt {};
    std::array<std::atomic<long long>, go::maxCells> placedAt {};
    std::atomic<long long> ageClock { 0 };
    std::atomic<long long> placeCounter { 0 };

    double freeRunCountdown = 0.0;
    double gameCountdown = 0.0;
    long long absoluteStep = 0;
    bool wasRunning = false;

    juce::AudioParameterInt*    noteParam        = nullptr;
    juce::AudioParameterInt*    blackChannel     = nullptr;
    juce::AudioParameterInt*    whiteChannel     = nullptr;
    juce::AudioParameterInt*    blackVelocity    = nullptr;
    juce::AudioParameterInt*    whiteVelocity    = nullptr;
    juce::AudioParameterChoice* rateParam        = nullptr;
    juce::AudioParameterFloat*  gateParam        = nullptr;
    juce::AudioParameterFloat*  tempoParam       = nullptr;
    juce::AudioParameterBool*   freeRunParam     = nullptr;
    juce::AudioParameterBool*   koRuleParam      = nullptr;
    juce::AudioParameterBool*   selfCaptureParam = nullptr;
    juce::AudioParameterChoice* colourModeParam  = nullptr;
    juce::AudioParameterChoice* boardSizeParam   = nullptr;
    juce::AudioParameterChoice* playModeParam    = nullptr;
    juce::AudioParameterInt*    spreadParam      = nullptr;
    juce::AudioParameterInt*    stoneLifeParam   = nullptr;
    juce::AudioParameterChoice* lifeModeParam    = nullptr;
    juce::AudioParameterChoice* gameRateParam    = nullptr;

    //  one per playhead, assigned outright: headChannel[2] is the third head's
    //  channel whatever the others are set to
    std::array<juce::AudioParameterInt*, maxHeadChannels> headChannel {};

    juce::AudioParameterBool*   gameRunParam     = nullptr;
    juce::AudioParameterBool*   gameLoopParam    = nullptr;

    juce::AudioParameterBool*   waveReplayParam  = nullptr;
    juce::AudioParameterInt*    waveGapParam     = nullptr;

    //  re-entrancy guard: clampStoneLifeToWaveGap() sets stoneLifeParam,
    //  which would otherwise trigger parameterChanged() straight back into it
    bool clampingWaveParams = false;

    std::atomic<bool> pendingBoardSizeChange { false };
    std::atomic<bool> pendingWaveClamp       { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoSequencerProcessor)
};
