#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <iterator>
#include <vector>

#include "GoAI.h"
#include "GoBoard.h"
#include "SgfParser.h"

//==============================================================================
/** Step sequencer that reads a Go board as a pattern. It has two ways of
    walking it, chosen with the playMode parameter:

      Spiral      one playhead, clockwise from the top left corner and winding
                  in to tengen. Black and white have a channel each, so the
                  colour it passed can be routed as well as heard.

      Quads       one playhead per quadrant, each spiralling around the middle
                  of its block - the corner star point on a 9x9 and a 13x13,
                  the four points inside the 4-4 star on a 19x19. The four
                  blocks share the middle row and column, so the heads meet on
                  the same edge in the centre. They can wind out from the
                  middle or in to it.

      Polyrhythm  one playhead per concentric ring, tengen aside: four of them
                  on a 9x9, six on a 13x13, nine on a 19x19. They share the step
                  clock, but the rings are 32, 24, 16 and 8 points around, so
                  they come back into phase only every 96 steps (480 on a
                  13x13, 20160 on a 19x19). Every ring
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
    per playhead - four on a 9x9, six rings on a 13x13, nine on a 19x19. They
    start out on 1..9, but nothing stops two heads sharing a channel or the whole board sitting on
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

    //  Rings. ringCount() is 4 on a 9x9, 6 on a 13x13 and 9 on a 19x19: tengen
    //  is left out.
    bool isPolyrhythm()   const noexcept;
    int  ringCount()      const noexcept { return go::ringCount (boardSize()); }

    //  Quadrants: four spirals, one per corner block.
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
    //  AI self-play: the record is written rather than loaded.
    //
    //  The two players in GoAI.h play a game out, and what comes back is an
    //  ordinary record - so Move Rate, Run, Loop, the step buttons, the position
    //  slider and Wave Replay all go on meaning exactly what they meant for an
    //  .sgf, and none of them had to learn about this.
    //
    //  Every game opens on the same ten book moves and diverges from the first
    //  move after: the sequencer reads position as pitch, so that is a fixed
    //  motif followed by a variation on it, once per game.
    //
    //  Threading. Generating a game allocates, so it only ever happens on the
    //  message thread, one game ahead of the one being played. When the record
    //  runs out under Loop, the audio thread swaps the waiting game in - a
    //  member-wise swap of two records, which is a handful of pointer exchanges
    //  and no heap traffic (goai::swapGames) - and asks the message thread for
    //  the next one. If that request has not been answered by the time the game
    //  ends, nothing breaks: the record simply repeats, as it would have before.
    //
    //  Length, Variation and Seed are read when a game is generated, so a change
    //  to any of them lands on the next game rather than interrupting this one.
    //  Switching AI Self-Play off and on starts a fresh run from game 1.

    bool aiSelfPlay()   const noexcept { return aiActive.load (std::memory_order_relaxed); }

    //==============================================================================
    //  The opening. Ten moves, black first, and the same ten at the start of
    //  every game of a run - either the book line in GoAI.h or one played by
    //  hand on the board.
    //
    //  A position is not an opening: the order the stones went down in decides
    //  what is captured and what is legal, and the board does not remember it.
    //  So the stones a click places are recorded as they are played (handPlayed)
    //  and it is that sequence, not the board, which an opening is taken from.

    static constexpr int openingLength = goai::openingLength;

    /** Takes the first ten stones played by hand since the board was last
        cleared as the opening. Returns an empty string, or a sentence saying
        why those ten will not do. */
    juce::String setOpeningFromBoard();

    /** Back to the book line for this board size. */
    void useBookOpening();

    bool hasCustomOpening() const noexcept { return customOpeningCount == openingLength; }

    /** "your ten moves" or "the book line", for the editor to show. */
    juce::String openingDescription() const;

    /** How many hand-played stones an opening could be taken from right now. */
    int handPlayedCount() const noexcept { return (int) handPlayed.size(); }

    /** Which game of the run is playing, counted from 1, and the seed it was
        generated from - the pair that names it exactly. */
    int  aiGameNumber() const noexcept { return aiGameCounter.load (std::memory_order_relaxed) + 1; }
    unsigned int aiSeed() const noexcept { return aiSeedShown.load (std::memory_order_relaxed); }

    //==============================================================================
    //  Wave Replay: an alternative pacing for game record playback. Advance
    //  never pauses - every move still lands on its own Move Rate tick - but
    //  every waveGap moves after a move first landed, whatever currently sits
    //  on that point has its lifespan reset, as if it had just been placed.
    //  Different moves are born on different ticks, so their resets land on
    //  different ticks too: the effect ripples across the board one stone at
    //  a time rather than pulsing the whole board together.
    //
    //  Each echo level is a replay head: level k is the head that started k*gap
    //  moves ago, and since the stone it would play is already standing, playing
    //  it can only mean refreshing it. With Loop on the record wraps without
    //  clearing the board, echo sources wrap with it (see gameWrapped), and the
    //  wave keeps running until the transport stops rather than dying at the end
    //  of the record. The board stops evolving once it is full - replayed moves
    //  land on occupied points and become refreshes too - which is the point:
    //  the finished shape is what ripples.
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

    /** The size the board size parameter asks for. Its choices are listed in
        go::supportedSizes order, so the choice index is the size slot. */
    int chosenBoardSize() const noexcept;
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
    /** Wave Replay's loop: sends the record back to move 0 leaving the board
        standing, so the stones the wave is still rippling survive the wrap. */
    void wrapGameLocked();
    /** Plays the move at the current position. refreshOccupied turns a move
        onto a point that is already taken into a lifespan refresh rather than
        a dropped move - what a replay head can mean once the board is full. */
    bool advanceGameLocked (bool refreshOccupied);
    void rebuildBoardFromGameLocked (int position);
    /** Deals idx a fresh lifespan, as if a stone had just landed on it. */
    void refreshStoneLifeLocked (int idx);
    /** Wave Replay: after movesPlaced (gameMovePosition, just advanced past
        the move that landed on this tick) has been reached, refreshes every
        point still echoing an earlier move - see the class doc above. */
    void applyWaveEchoesLocked (int movesPlaced);

    //  ---- AI self-play, message thread unless noted ------------------------
    /** The settings a game is generated from: the three parameters, plus the
        seed that game number turns into. */
    goai::Settings aiSettingsFor (int size, int gameNumber) const;

    /** Starts a run at this game number: generates it, puts it on the board,
        and asks for the one after it. */
    void startAiSelfPlay (int gameNumber);

    /** The switch going off: the run ends and its record is unloaded. Does
        nothing if the record on the board is not one of ours. */
    void stopAiSelfPlay();

    /** Hands the board back to whatever is taking over - a loaded .sgf, an
        unload - without clearing the record that replaced ours. Turns the
        parameter off too, so the switch tells the truth. */
    void releaseAiSelfPlay();

    /** Generates the game after the one playing, into aiNextGame. */
    void prepareNextAiGame();

    /** Audio thread: puts the waiting game on the board. Expects boardLock, and
        must not allocate - see the note above. */
    void swapInNextAiGameLocked();

    /** Drops the record, leaving the board as it stands. Expects boardLock. */
    void clearGameLocked();

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

    //  Stones placed by hand, in the order they were played: where a custom
    //  opening comes from. Message thread only, and dropped whenever something
    //  other than a click puts stones on the board.
    std::vector<sgf::Placement> handPlayed;

    //  the opening every game of a run starts from, or count 0 for the book.
    //  Kept as board indices, so it belongs to the size it was played on.
    std::array<int, (size_t) goai::openingLength> customOpening {};
    int customOpeningCount = 0;
    int customOpeningSize = 0;

    //  the game after the one playing, generated in advance so the swap at the
    //  end of a game costs the audio thread nothing
    sgf::Game aiNextGame;                          // guarded by boardLock
    bool aiNextReady = false;                      // guarded by boardLock
    int aiNextNumber = 0;                          // guarded by boardLock
    unsigned int aiNextSeed = 0;                   // guarded by boardLock
    //  set once the record has wrapped at least once under Wave Replay, so echo
    //  sources may reach back past move 1 into the tail of the record - before
    //  that there is no earlier pass for them to find. Cleared by any rebuild.
    bool gameWrapped = false;                      // guarded by boardLock

    std::array<std::atomic<std::uint8_t>, go::maxCells> stones {};
    std::atomic<int> prisonersBlack { 0 }, prisonersWhite { 0 }, lastMoveIndex { -1 };
    std::atomic<int> step { 0 };
    std::atomic<int> activeSize { 9 };
    std::atomic<int> gameMoveTotal { 0 }, gamePositionMirror { 0 };
    std::atomic<bool> running { false };
    std::atomic<int> nextAlternating { (int) go::Stone::black };

    //  the walks, worked out once per board size and indexed by go::sizeSlot()
    std::array<std::array<int, go::maxCells>, go::sizeCount> spiralTables {};
    std::array<std::array<std::array<int, go::maxCells>, go::quadCount>, go::sizeCount> quadTables {};

    //  room for every ring to hold a note at once, with headroom for the
    //  overlap when a long gate runs into the next step
    std::array<PendingNoteOff, 32> pending {};
    //  maxRings is the widest any mode gets: 9 rings beats 4 quadrants
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

    juce::AudioParameterBool*   aiPlayParam      = nullptr;
    juce::AudioParameterInt*    aiMovesParam     = nullptr;
    juce::AudioParameterInt*    aiVariationParam = nullptr;
    juce::AudioParameterInt*    aiSeedParam      = nullptr;

    //  re-entrancy guard: clampStoneLifeToWaveGap() sets stoneLifeParam,
    //  which would otherwise trigger parameterChanged() straight back into it
    bool clampingWaveParams = false;

    //  the same, for releaseAiSelfPlay() writing aiPlayParam
    bool settingAiPlayParam = false;

    //  true while the record on the board is one the players wrote, which is
    //  what tells the audio thread it may swap the next game in at the wrap
    std::atomic<bool> aiActive { false };
    std::atomic<int>  aiGameCounter { 0 };
    std::atomic<unsigned int> aiSeedShown { 0 };

    std::atomic<bool> pendingBoardSizeChange { false };
    std::atomic<bool> pendingWaveClamp       { false };
    std::atomic<bool> pendingAiRestart       { false };
    std::atomic<bool> pendingAiPrepare       { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoSequencerProcessor)
};
