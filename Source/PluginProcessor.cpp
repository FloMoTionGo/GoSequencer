#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
const std::array<double, 9>& GoSequencerProcessor::rateInBeats()
{
    //  one step, measured in quarter notes
    static const std::array<double, 9> beats
    {
        4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125
    };

    return beats;
}

juce::StringArray GoSequencerProcessor::rateNames()
{
    return { "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
}

juce::StringArray GoSequencerProcessor::gameRateNames()
{
    return { "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars", "one lap" };
}

juce::StringArray GoSequencerProcessor::boardSizeNames()
{
    return { "9 x 9", "13 x 13" };
}

juce::StringArray GoSequencerProcessor::playModeNames()
{
    return { "Spiral", "Polyrhythm", "Quads out", "Quads in" };
}

juce::StringArray GoSequencerProcessor::lifeModeNames()
{
    return { "Steps", "Placements" };
}

namespace
{
    /** How many quarter notes one game move lasts. */
    double gameStepInBeats (int rateIndex, double barBeats, double stepBeats, int lapSteps)
    {
        switch (rateIndex)
        {
            case 0:  return 1.0;
            case 1:  return 2.0;
            case 2:  return barBeats;
            case 3:  return barBeats * 2.0;
            case 4:  return barBeats * 4.0;
            case 5:  return barBeats * 8.0;
            default: break;
        }

        return stepBeats * (double) lapSteps;       // one lap: the spiral, or the outer ring
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout GoSequencerProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "rate", 1 }, "Rate",
                                                        rateNames(), 6));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "note", 1 }, "Note", 0, 127, 60,
                                                     AudioParameterIntAttributes()
                                                         .withStringFromValueFunction ([] (int v, int)
                                                         {
                                                             return MidiMessage::getMidiNoteName (v, true, true, 3);
                                                         })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "gate", 1 }, "Gate",
                                                       NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.5f,
                                                       AudioParameterFloatAttributes()
                                                           .withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return String (juce::roundToInt (v * 100.0f)) + "%";
                                                           })));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "tempo", 1 }, "Free Tempo",
                                                       NormalisableRange<float> (20.0f, 300.0f, 0.1f), 120.0f,
                                                       AudioParameterFloatAttributes()
                                                           .withStringFromValueFunction ([] (float v, int)
                                                           {
                                                               return String (v, 1) + " BPM";
                                                           })));

    //  Spiral routes by colour; the multi head modes route by playhead. Every
    //  one of them is set outright rather than offset from a base, so any two
    //  can share a channel or the whole board can sit on channel 1.
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "blackChannel", 1 }, "Black Channel", 1, 16, 1));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "whiteChannel", 1 }, "White Channel", 1, 16, 2));

    for (int h = 0; h < maxHeadChannels; ++h)
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "headChannel" + String (h + 1), 1 },
                                                         "Head " + String (h + 1) + " Channel",
                                                         1, 16, h + 1));

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "blackVelocity", 1 }, "Black Velocity", 1, 127, 100));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "whiteVelocity", 1 }, "White Velocity", 1, 127, 100));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "freeRun", 1 }, "Free Run", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "koRule", 1 }, "Ko Rule", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "selfCapture", 1 }, "Self Capture", false));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "colourMode", 1 }, "Place",
                                                        StringArray { "Alternate", "Black", "White" }, 0));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "boardSize", 1 }, "Board",
                                                        boardSizeNames(), 0));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "playMode", 1 }, "Mode",
                                                        playModeNames(), 0));

    //  what a stone's life is counted in: ticks of the clock, or stones laid after it
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lifeMode", 1 }, "Life Counts",
                                                        lifeModeNames(), 0));

    //  how long a stone keeps sounding once it lands
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "stoneLife", 1 }, "Stone Life",
                                                     1, maxStoneLife, 15,
                                                     AudioParameterIntAttributes()
                                                         .withStringFromValueFunction ([] (int v, int)
                                                         {
                                                             return v >= maxStoneLife ? String ("hold")
                                                                                      : String (v) + " steps";
                                                         })));

    //  the id stays ringSpread so saved sessions keep their value
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "ringSpread", 1 }, "Spread", -12, 12, 0,
                                                     AudioParameterIntAttributes()
                                                         .withStringFromValueFunction ([] (int v, int)
                                                         {
                                                             //  the value box is 62px: "0 semitones" did not fit
                                                             return String (v > 0 ? "+" : "") + String (v) + " st";
                                                         })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "gameRate", 1 }, "Move Rate",
                                                        gameRateNames(), 2));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "gameRun", 1 }, "Run Game", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "gameLoop", 1 }, "Loop Game", true));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "waveReplay", 1 }, "Wave Replay", false));

    //  minimum 2, so Stone Life (minimum 1) always has room to sit below it
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "waveGap", 1 }, "Wave Gap", 2, 128, 20));

    return layout;
}

//==============================================================================
GoSequencerProcessor::GoSequencerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GOSEQ", createParameterLayout())
{
    noteParam        = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("note"));
    blackChannel     = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("blackChannel"));
    whiteChannel     = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("whiteChannel"));
    blackVelocity    = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("blackVelocity"));
    whiteVelocity    = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("whiteVelocity"));
    rateParam        = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("rate"));
    gateParam        = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("gate"));
    tempoParam       = dynamic_cast<juce::AudioParameterFloat*>  (apvts.getParameter ("tempo"));
    freeRunParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("freeRun"));
    koRuleParam      = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("koRule"));
    selfCaptureParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("selfCapture"));
    colourModeParam  = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("colourMode"));
    boardSizeParam   = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("boardSize"));
    playModeParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("playMode"));
    spreadParam      = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("ringSpread"));
    stoneLifeParam   = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("stoneLife"));
    lifeModeParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("lifeMode"));
    gameRateParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("gameRate"));
    gameRunParam     = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("gameRun"));
    gameLoopParam    = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("gameLoop"));
    waveReplayParam  = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter ("waveReplay"));
    waveGapParam     = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter ("waveGap"));

    for (int h = 0; h < maxHeadChannels; ++h)
        headChannel[(size_t) h] = dynamic_cast<juce::AudioParameterInt*>
                                    (apvts.getParameter ("headChannel" + juce::String (h + 1)));

    jassert (noteParam != nullptr && rateParam != nullptr && boardSizeParam != nullptr);

    go::spiralOrder (9, spiral9);
    go::spiralOrder (13, spiral13);

    for (int q = 0; q < go::quadCount; ++q)
    {
        go::quadOrder (9, q, quad9[(size_t) q]);
        go::quadOrder (13, q, quad13[(size_t) q]);
    }

    apvts.addParameterListener ("boardSize", this);
    apvts.addParameterListener ("waveReplay", this);
    apvts.addParameterListener ("waveGap", this);
    apvts.addParameterListener ("stoneLife", this);

    publishBoard();
}

GoSequencerProcessor::~GoSequencerProcessor()
{
    apvts.removeParameterListener ("boardSize", this);
    apvts.removeParameterListener ("waveReplay", this);
    apvts.removeParameterListener ("waveGap", this);
    apvts.removeParameterListener ("stoneLife", this);
    cancelPendingUpdate();
}

//==============================================================================
void GoSequencerProcessor::prepareToPlay (double, int)
{
    for (auto& p : pending)
        p = {};

    resetPlayhead();
    gameCountdown = 0.0;
    wasRunning = false;
    running.store (false, std::memory_order_relaxed);
}

bool GoSequencerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

int GoSequencerProcessor::spiralAt (int stepIndex) const noexcept
{
    const int size  = activeSize.load (std::memory_order_relaxed);
    const int count = size * size;

    if (stepIndex < 0 || stepIndex >= count)
        return 0;

    return (size == 13 ? spiral13 : spiral9)[(size_t) stepIndex];
}

bool GoSequencerProcessor::isPolyrhythm() const noexcept
{
    return playModeParam != nullptr && playModeParam->getIndex() == 1;
}

bool GoSequencerProcessor::isQuads() const noexcept
{
    return playModeParam != nullptr && playModeParam->getIndex() >= 2;
}

bool GoSequencerProcessor::quadsWindOut() const noexcept
{
    return playModeParam != nullptr && playModeParam->getIndex() == 2;
}

int GoSequencerProcessor::headCount() const noexcept
{
    if (isQuads())      return go::quadCount;
    if (isPolyrhythm()) return ringCount();

    return 1;
}

int GoSequencerProcessor::cycleSteps() const noexcept
{
    if (isQuads())      return go::quadSteps (boardSize());
    if (isPolyrhythm()) return go::ringLength (boardSize(), 0);

    return stepCount();
}

int GoSequencerProcessor::ringCellAt (int ring, int pos) const noexcept
{
    const int size = boardSize();

    if (ring < 0 || ring >= go::ringCount (size))
        return 0;

    const int len = juce::jmax (1, go::ringLength (size, ring));

    return spiralAt (go::ringOffset (size, ring) + ((pos % len) + len) % len);
}

int GoSequencerProcessor::quadCellAt (int quad, int pos) const noexcept
{
    const int size = boardSize();
    const int n = juce::jmax (1, go::quadSteps (size));

    if (quad < 0 || quad >= go::quadCount)
        return 0;

    int i = ((pos % n) + n) % n;

    //  the table winds in to the star point, so reverse it to wind out
    if (quadsWindOut())
        i = n - 1 - i;

    return (size == 13 ? quad13 : quad9)[(size_t) quad][(size_t) i];
}

int GoSequencerProcessor::headCellAt (int head, int pos) const noexcept
{
    if (isQuads())      return quadCellAt (head, pos);
    if (isPolyrhythm()) return ringCellAt (head, pos);

    return spiralAt (pos);
}

int GoSequencerProcessor::colourChannel (int idx) const noexcept
{
    const bool white = (idx >= 0 && idx < go::maxCells)
                         && stones[(size_t) idx].load (std::memory_order_relaxed)
                              == (std::uint8_t) go::Stone::white;

    auto* param = white ? whiteChannel : blackChannel;

    return param != nullptr ? param->get() : 1;
}

int GoSequencerProcessor::headChannelFor (int head) const noexcept
{
    if (head < 0 || head >= maxHeadChannels || headChannel[(size_t) head] == nullptr)
        return 1;

    return headChannel[(size_t) head]->get();
}

bool GoSequencerProcessor::isSpent (int idx, int life) const noexcept
{
    if (idx < 0 || idx >= go::maxCells)
        return true;

    if (life >= maxStoneLife)
        return false;                   //  "hold": nothing ever expires

    //  Both branches are the same shape: how far the counter has moved on since
    //  this one stone was stamped, against the span asked for now. Nothing is
    //  cached, so dragging the lifespan slider mid run simply asks the question
    //  again of each stone at whatever age it has reached - the board does not
    //  restart together.

    //  counting placements ignores the clock entirely: a stone dies when enough
    //  others have landed on top of it, however fast or slow they arrive
    if (lifeModeParam != nullptr && lifeModeParam->getIndex() == 1)
        return placeCounter.load (std::memory_order_relaxed)
                 - placedAt[(size_t) idx].load (std::memory_order_relaxed) >= (long long) life;

    return ageClock.load (std::memory_order_relaxed)
             - bornAt[(size_t) idx].load (std::memory_order_relaxed) >= (long long) life;
}

bool GoSequencerProcessor::stoneIsSpent (int idx) const noexcept
{
    if (idx < 0 || idx >= go::maxCells)
        return false;

    if (stones[(size_t) idx].load (std::memory_order_relaxed) == (std::uint8_t) go::Stone::none)
        return false;

    return isSpent (idx, stoneLifeParam != nullptr ? stoneLifeParam->get() : maxStoneLife);
}

void GoSequencerProcessor::resetPlayhead() noexcept
{
    freeRunCountdown = 0.0;
    absoluteStep = 0;
    step.store (0, std::memory_order_relaxed);

    for (auto& h : headPos)
        h.store (0, std::memory_order_relaxed);

    //  the age clock and the stones' birth stamps are deliberately left alone.
    //  Only the playheads go back to their corners: a stone that was two thirds
    //  through its life when the transport stopped picks up two thirds through,
    //  and one placed while stopped still gets its full span.
}

//==============================================================================
void GoSequencerProcessor::publishBoard()
{
    const long long now = ageClock.load (std::memory_order_relaxed);

    for (int i = 0; i < go::maxCells; ++i)
    {
        const auto was = stones[(size_t) i].load (std::memory_order_relaxed);
        const auto is  = (std::uint8_t) board.at (i);

        if (is == was)
            continue;

        //  a stone that has just landed starts its life here; a lifted one forgets
        const bool landed = (is != (std::uint8_t) go::Stone::none);

        bornAt[(size_t) i].store (landed ? now : 0, std::memory_order_relaxed);
        placedAt[(size_t) i].store (landed ? placeCounter.fetch_add (1, std::memory_order_relaxed) + 1 : 0,
                                    std::memory_order_relaxed);

        stones[(size_t) i].store (is, std::memory_order_relaxed);
    }

    prisonersBlack.store (board.capturedBlack(), std::memory_order_relaxed);
    prisonersWhite.store (board.capturedWhite(), std::memory_order_relaxed);
    lastMoveIndex.store (board.lastMove(), std::memory_order_relaxed);
}

go::Stone GoSequencerProcessor::colourForNextMove() const noexcept
{
    switch (colourModeParam != nullptr ? colourModeParam->getIndex() : 0)
    {
        case 1:  return go::Stone::black;
        case 2:  return go::Stone::white;
        default: break;
    }

    return (go::Stone) nextAlternating.load (std::memory_order_relaxed);
}

go::MoveResult GoSequencerProcessor::placeStone (int idx)
{
    const auto colour = colourForNextMove();
    const bool allowSuicide = selfCaptureParam != nullptr && selfCaptureParam->get();
    const bool applyKo      = koRuleParam      != nullptr && koRuleParam->get();

    go::MoveResult result;

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);
        result = board.play (idx, colour, allowSuicide, applyKo);

        if (result == go::MoveResult::ok)
            publishBoard();
    }

    if (result == go::MoveResult::ok && (colourModeParam == nullptr || colourModeParam->getIndex() == 0))
        nextAlternating.store ((int) go::other (colour), std::memory_order_relaxed);

    return result;
}

void GoSequencerProcessor::eraseStone (int idx)
{
    const juce::SpinLock::ScopedLockType sl (boardLock);

    if (board.removeStone (idx))
        publishBoard();
}

void GoSequencerProcessor::clearBoard()
{
    {
        const juce::SpinLock::ScopedLockType sl (boardLock);
        board.clear();
        publishBoard();
    }

    nextAlternating.store ((int) go::Stone::black, std::memory_order_relaxed);
}

//==============================================================================
void GoSequencerProcessor::parameterChanged (const juce::String& parameterID, float)
{
    if (parameterID == "boardSize")
    {
        pendingBoardSizeChange.store (true, std::memory_order_relaxed);
        triggerAsyncUpdate();       //  the change may arrive on the audio thread
    }
    else if (parameterID == "waveReplay" || parameterID == "waveGap" || parameterID == "stoneLife")
    {
        pendingWaveClamp.store (true, std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void GoSequencerProcessor::handleAsyncUpdate()
{
    if (pendingBoardSizeChange.exchange (false, std::memory_order_relaxed))
        applyBoardSize (boardSizeParam != nullptr && boardSizeParam->getIndex() == 1 ? 13 : 9);

    if (pendingWaveClamp.exchange (false, std::memory_order_relaxed))
        clampStoneLifeToWaveGap();
}

void GoSequencerProcessor::clampStoneLifeToWaveGap()
{
    if (clampingWaveParams)
        return;

    if (waveReplayParam == nullptr || waveGapParam == nullptr || stoneLifeParam == nullptr)
        return;

    if (! waveReplayParam->get())
        return;

    const int maxLife = juce::jmax (1, waveGapParam->get() - 1);

    if (stoneLifeParam->get() <= maxLife)
        return;

    clampingWaveParams = true;
    stoneLifeParam->beginChangeGesture();
    stoneLifeParam->setValueNotifyingHost (stoneLifeParam->convertTo0to1 ((float) maxLife));
    stoneLifeParam->endChangeGesture();
    clampingWaveParams = false;
}

void GoSequencerProcessor::applyBoardSize (int newSize)
{
    if (! go::isSupportedSize (newSize) || newSize == board.size())
        return;

    const bool dropGame = (gameMoveTotal.load (std::memory_order_relaxed) > 0 && game.size != newSize);

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);

        if (dropGame)
        {
            game = {};
            sgfText.clear();
            sourceName.clear();
            gameMovePosition = 0;
        }

        board.setSize (newSize);        //  a different board means different points: it clears

        if (! dropGame && gameMoveTotal.load (std::memory_order_relaxed) > 0)
            rebuildBoardFromGameLocked (gameMovePosition);

        publishBoard();
    }

    if (dropGame)
    {
        gameMoveTotal.store (0, std::memory_order_relaxed);
        gamePositionMirror.store (0, std::memory_order_relaxed);
    }

    activeSize.store (newSize, std::memory_order_relaxed);
    step.store (0, std::memory_order_relaxed);
    nextAlternating.store ((int) go::Stone::black, std::memory_order_relaxed);
}

//==============================================================================
juce::String GoSequencerProcessor::loadSgf (const juce::File& file)
{
    if (! file.existsAsFile())
        return "that file is not there any more";

    const auto text = file.loadFileAsString();

    if (text.isEmpty())
        return "that file is empty";

    return loadSgfText (text, file.getFileName());
}

juce::String GoSequencerProcessor::loadSgfText (const juce::String& text, const juce::String& name)
{
    const auto parsed = sgf::parse (text.toStdString());

    if (! parsed.valid)
        return "could not read that record: " + juce::String (parsed.error);

    if (! go::isSupportedSize (parsed.size))
        return juce::String (parsed.size) + "x" + juce::String (parsed.size)
             + " records are not supported yet - 9x9 and 13x13 only";

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);

        game = parsed;
        sgfText = text;
        sourceName = name;
        gameMovePosition = 0;

        board.setSize (parsed.size);
        rebuildBoardFromGameLocked (0);
        publishBoard();
    }

    activeSize.store (parsed.size, std::memory_order_relaxed);
    gameMoveTotal.store (parsed.moveCount(), std::memory_order_relaxed);
    gamePositionMirror.store (0, std::memory_order_relaxed);
    step.store (0, std::memory_order_relaxed);
    gameCountdown = 0.0;

    //  keep the board size control in step with the record
    if (boardSizeParam != nullptr)
    {
        const int wanted = (parsed.size == 13 ? 1 : 0);

        if (boardSizeParam->getIndex() != wanted)
        {
            boardSizeParam->beginChangeGesture();
            boardSizeParam->setValueNotifyingHost (boardSizeParam->convertTo0to1 ((float) wanted));
            boardSizeParam->endChangeGesture();
        }
    }

    return {};
}

void GoSequencerProcessor::clearGame()
{
    {
        const juce::SpinLock::ScopedLockType sl (boardLock);
        game = {};
        sgfText.clear();
        sourceName.clear();
        gameMovePosition = 0;
    }

    gameMoveTotal.store (0, std::memory_order_relaxed);
    gamePositionMirror.store (0, std::memory_order_relaxed);
}

juce::String GoSequencerProcessor::gameTitle() const
{
    if (! hasGame())
        return {};

    const auto players = juce::String (game.players());

    if (players.isNotEmpty())
        return players;

    return game.gameName.empty() ? sourceName : juce::String (game.gameName);
}

juce::String GoSequencerProcessor::gameDetail() const
{
    if (! hasGame())
        return {};

    juce::String detail;
    detail << game.size << "x" << game.size << "  " << game.moveCount() << " moves";

    if (! game.result.empty())
        detail << "  " << juce::String (game.result);

    if (! game.date.empty())
        detail << "  " << juce::String (game.date);

    return detail;
}

void GoSequencerProcessor::setGamePosition (int position)
{
    if (! hasGame())
        return;

    const int clamped = juce::jlimit (0, gameMoveCount(), position);

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);
        rebuildBoardFromGameLocked (clamped);
        publishBoard();
    }

    gamePositionMirror.store (clamped, std::memory_order_relaxed);
}

void GoSequencerProcessor::resetGameLocked()
{
    rebuildBoardFromGameLocked (0);
}

void GoSequencerProcessor::wrapGameLocked()
{
    //  deliberately not a rebuild: the board stays exactly as the record left
    //  it, so the stones the wave is still rippling are there to be refreshed
    //  on the other side of the wrap
    gameMovePosition = 0;
    gameWrapped = true;
}

void GoSequencerProcessor::rebuildBoardFromGameLocked (int position)
{
    board.clear();

    for (const auto& placement : game.setup)
        board.setStone (placement.index, placement.colour);

    const int limit = juce::jlimit (0, (int) game.moves.size(), position);

    for (int i = 0; i < limit; ++i)
    {
        const auto& move = game.moves[(size_t) i];

        if (! move.isPass)
            board.play (move.index, move.colour, true, false);
    }

    board.resetCaptureCounts();
    gameMovePosition = limit;
    gameWrapped = false;        //  a fresh board has no earlier pass behind it
}

bool GoSequencerProcessor::advanceGameLocked (bool refreshOccupied)
{
    if (gameMovePosition >= (int) game.moves.size())
        return false;

    const auto& move = game.moves[(size_t) gameMovePosition];

    if (! move.isPass)
    {
        if (refreshOccupied && board.at (move.index) != go::Stone::none)
            refreshStoneLifeLocked (move.index);    //  a head replaying a point it already owns
        else
            board.play (move.index, move.colour, true, false);   //  records are trusted: no ko test
    }

    ++gameMovePosition;
    return true;
}

void GoSequencerProcessor::refreshStoneLifeLocked (int idx)
{
    bornAt[(size_t) idx].store (ageClock.load (std::memory_order_relaxed), std::memory_order_relaxed);
    placedAt[(size_t) idx].store (placeCounter.fetch_add (1, std::memory_order_relaxed) + 1,
                                  std::memory_order_relaxed);
}

void GoSequencerProcessor::applyWaveEchoesLocked (int movesPlaced)
{
    if (waveGapParam == nullptr)
        return;

    const int gap = waveGapParam->get();
    const int total = (int) game.moves.size();

    if (gap < 1 || total < 1)
        return;

    //  echo k reaches, on this move, the point that move (movesPlaced - k*gap)
    //  touched when IT landed. Different moves were born on different ticks,
    //  so each echo level lands on a different move - never all at once.
    for (int k = 1; k <= maxWaveEchoes; ++k)
    {
        int sourceMove = movesPlaced - k * gap;

        if (sourceMove < 1)
        {
            //  before the first wrap there is no earlier pass to reach into, so
            //  the wave is only as deep as the record is long so far. After one,
            //  the heads run off the front of the record into its tail, which is
            //  still standing on the board - that is what keeps the wave whole
            //  across the loop instead of thinning out and rebuilding.
            if (! gameWrapped)
                break;

            sourceMove = ((sourceMove - 1) % total + total) % total + 1;   //  into 1..total
        }

        if (sourceMove == movesPlaced)
            continue;                   //  a head that has lapped onto this very move

        const auto& mv = game.moves[(size_t) (sourceMove - 1)];

        if (mv.isPass || board.at (mv.index) == go::Stone::none)
            continue;

        refreshStoneLifeLocked (mv.index);
    }
}

//==============================================================================
void GoSequencerProcessor::allNotesOff (juce::MidiBuffer& midi, int offsetInBlock)
{
    for (auto& p : pending)
    {
        if (p.active)
        {
            midi.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), offsetInBlock);
            p = {};
        }
    }
}

void GoSequencerProcessor::flushNoteOffs (juce::MidiBuffer& midi, int numSamples)
{
    for (auto& p : pending)
    {
        if (! p.active)
            continue;

        if (p.samplesLeft < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), juce::jmax (0, p.samplesLeft));
            p = {};
        }
        else
        {
            p.samplesLeft -= numSamples;
        }
    }
}

void GoSequencerProcessor::fireCell (int idx, int channelIn, int semitoneOffset, int offsetInBlock,
                                     juce::MidiBuffer& midi, double samplesPerStep)
{
    if (idx < 0 || idx >= go::maxCells)
        return;

    const auto stone = (go::Stone) stones[(size_t) idx].load (std::memory_order_relaxed);

    if (stone == go::Stone::none)
        return;

    //  The one gate every note passes through, for every mode and every head:
    //  outside its lifespan the stone is still on the board and still bound by
    //  the rules, but the playhead goes over it without a sound.
    if (isSpent (idx, stoneLifeParam != nullptr ? stoneLifeParam->get() : maxStoneLife))
        return;

    const bool isBlack = (stone == go::Stone::black);
    const int channel  = juce::jlimit (1, 16, channelIn);
    const int note     = juce::jlimit (0, 127, noteParam->get() + semitoneOffset);
    const int velocity = juce::jlimit (1, 127, isBlack ? blackVelocity->get() : whiteVelocity->get());

    //  retrigger safety: if this note is still ringing on this channel, close it first
    for (auto& p : pending)
    {
        if (p.active && p.note == note && p.channel == channel)
        {
            midi.addEvent (juce::MidiMessage::noteOff (p.channel, p.note), juce::jmax (0, offsetInBlock - 1));
            p = {};
        }
    }

    midi.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity), offsetInBlock);

    const int gateSamples = juce::jlimit (1, juce::jmax (1, (int) samplesPerStep - 1),
                                          (int) (gateParam->get() * samplesPerStep));

    for (auto& p : pending)
    {
        if (! p.active)
        {
            p.note        = note;
            p.channel     = channel;
            p.samplesLeft = offsetInBlock + gateSamples;
            p.active      = true;
            break;
        }
    }
}

void GoSequencerProcessor::triggerStep (int stepIndex, int offsetInBlock,
                                        juce::MidiBuffer& midi, double samplesPerStep)
{
    step.store (stepIndex, std::memory_order_relaxed);

    //  one head, no transpose, routed by the colour it passed: an empty point
    //  and a spent stone both fall out inside fireCell
    const int idx = spiralAt (stepIndex);

    fireCell (idx, colourChannel (idx), 0, offsetInBlock, midi, samplesPerStep);
}

void GoSequencerProcessor::triggerAt (long long absStep, int offsetInBlock,
                                      juce::MidiBuffer& midi, double samplesPerStep)
{
    //  The age clock is ours, not the host's: one tick per step actually played
    //  and never rewound. absStep is the host's timeline and may loop or leap,
    //  which is fine for working out where the heads sit but would make a stone
    //  younger - and so audible again - every time the host jumped backwards.
    ageClock.fetch_add (1, std::memory_order_relaxed);

    if (! isPolyrhythm() && ! isQuads())
    {
        const int steps = juce::jmax (1, stepCount());

        triggerStep ((int) (((absStep % steps) + steps) % steps), offsetInBlock, midi, samplesPerStep);
        return;
    }

    //  Every head fires on this tick, so whatever stones sit under them sound
    //  together as a chord. In polyrhythm the rings are different lengths and
    //  drift apart; the four quadrants are the same length and stay in step,
    //  which is the point - they mirror each other around the centre.
    const int heads  = headCount();
    const bool quads = isQuads();
    const int size   = boardSize();
    const int spread = spreadParam != nullptr ? spreadParam->get() : 0;

    for (int h = 0; h < heads; ++h)
    {
        const int len = juce::jmax (1, quads ? go::quadSteps (size) : go::ringLength (size, h));
        const int pos = (int) (((absStep % len) + len) % len);

        headPos[(size_t) h].store (pos, std::memory_order_relaxed);

        if (h == 0)
            step.store (pos, std::memory_order_relaxed);        //  what the editor follows

        const int cell = quads ? quadCellAt (h, pos)
                               : spiralAt (go::ringOffset (size, h) + pos);

        fireCell (cell,
                  headChannelFor (h),                           //  this head's own channel
                  h * spread,                                   //  and its own transpose
                  offsetInBlock, midi, samplesPerStep);
    }
}

void GoSequencerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();
    midi.clear();                       //  this is a generator, not a MIDI insert

    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0)
        return;

    double bpm = (double) tempoParam->get();
    double barBeats = 4.0;
    bool hostPlaying = false, ppqValid = false;
    double ppq = 0.0;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();

            if (const auto hostBpm = pos->getBpm())
                bpm = *hostBpm;

            if (const auto position = pos->getPpqPosition())
            {
                ppq = *position;
                ppqValid = true;
            }

            if (const auto signature = pos->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    barBeats = (double) signature->numerator * 4.0 / (double) signature->denominator;
        }
    }

    const bool freeRun = freeRunParam->get();
    const bool isNowRunning = freeRun || hostPlaying;

    running.store (isNowRunning, std::memory_order_relaxed);

    if (! isNowRunning)
    {
        if (wasRunning)
        {
            allNotesOff (midi, 0);
            wasRunning = false;
            resetPlayhead();
        }

        flushNoteOffs (midi, numSamples);
        return;
    }

    bpm = juce::jlimit (10.0, 999.0, bpm);

    const int lapSteps = juce::jmax (1, cycleSteps());
    const double beatsPerStep   = rateInBeats()[(size_t) rateParam->getIndex()];
    const double samplesPerStep = juce::jmax (2.0, (60.0 / bpm) * beatsPerStep * getSampleRate());

    const double beatsPerMove   = gameStepInBeats (gameRateParam->getIndex(), barBeats, beatsPerStep, lapSteps);
    const double samplesPerMove = juce::jmax (2.0, (60.0 / bpm) * beatsPerMove * getSampleRate());

    if (! wasRunning)
    {
        //  a fresh start: every playhead restarts from its own top left corner
        resetPlayhead();
        gameCountdown = samplesPerMove;
        wasRunning = true;
    }

    //  ---- the playheads ----------------------------------------------------
    if (freeRun || ! ppqValid)
    {
        while (freeRunCountdown < (double) numSamples)
        {
            triggerAt (absoluteStep, (int) freeRunCountdown, midi, samplesPerStep);
            ++absoluteStep;
            freeRunCountdown += samplesPerStep;
        }

        freeRunCountdown -= (double) numSamples;
    }
    else
    {
        //  locked to the host timeline: step 0 sits on the start of the arrangement
        const double stepPosition = ppq / beatsPerStep;
        double boundary = std::ceil (stepPosition - 1.0e-9);
        double offset = (boundary - stepPosition) * samplesPerStep;

        while (offset < (double) numSamples)
        {
            //  every ring counts from the same absolute step, so the whole
            //  polyrhythm lines up with the start of the arrangement
            triggerAt ((long long) boundary, juce::jlimit (0, numSamples - 1, (int) offset), midi, samplesPerStep);

            boundary += 1.0;
            offset += samplesPerStep;
        }
    }

    //  ---- the game record --------------------------------------------------
    if (gameRunParam->get() && gameMoveTotal.load (std::memory_order_relaxed) > 0)
    {
        gameCountdown -= (double) numSamples;

        if (gameCountdown <= 0.0)
        {
            const juce::SpinLock::ScopedTryLockType sl (boardLock);

            if (sl.isLocked())
            {
                int guard = 0;

                const bool waveReplay = waveReplayParam != nullptr && waveReplayParam->get();

                while (gameCountdown <= 0.0 && ++guard < 512)
                {
                    if (! advanceGameLocked (waveReplay))
                    {
                        if (! gameLoopParam->get())
                        {
                            gameCountdown = samplesPerMove;     //  hold on the final position
                            break;
                        }

                        //  Wave Replay wraps without clearing: the echo heads
                        //  are mid-record all over the board, and wiping it
                        //  would cut every one of them off at the loop point
                        if (waveReplay)
                            wrapGameLocked();
                        else
                            resetGameLocked();
                    }
                    else if (waveReplay)
                    {
                        applyWaveEchoesLocked (gameMovePosition);
                    }

                    //  per move rather than once at the end of the catch up: a
                    //  point taken and played again inside the same run of the
                    //  loop reads as unchanged, and the new stone would inherit
                    //  the dead one's age and arrive already spent
                    publishBoard();
                    gameCountdown += samplesPerMove;
                }

                gamePositionMirror.store (gameMovePosition, std::memory_order_relaxed);
            }
            else
            {
                gameCountdown = 0.0;                            //  try again next block
            }
        }
    }

    flushNoteOffs (midi, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* GoSequencerProcessor::createEditor()
{
    return new GoSequencerEditor (*this);
}

void GoSequencerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    auto xml = state.createXml();

    if (xml == nullptr)
        return;

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);
        xml->setAttribute ("board", juce::String (board.toString()));
        xml->setAttribute ("boardSizeValue", board.size());
        xml->setAttribute ("gamePosition", gameMovePosition);

        if (sgfText.isNotEmpty())
        {
            xml->setAttribute ("sgfName", sourceName);
            xml->createNewChildElement ("SGF")->addTextElement (sgfText);
        }
    }

    xml->setAttribute ("nextColour", nextAlternating.load (std::memory_order_relaxed));
    copyXmlToBinary (*xml, destData);
}

void GoSequencerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto boardText  = xml->getStringAttribute ("board");
    const int savedSize   = xml->getIntAttribute ("boardSizeValue", 9);
    const int position    = xml->getIntAttribute ("gamePosition", 0);
    const int nextColour  = xml->getIntAttribute ("nextColour", (int) go::Stone::black);
    const auto sgfName    = xml->getStringAttribute ("sgfName");

    juce::String savedSgf;

    if (auto* child = xml->getChildByName ("SGF"))
        savedSgf = child->getAllSubText();

    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    clampStoneLifeToWaveGap();      //  belt and suspenders: a saved session's own values might predate the rule

    const int size = go::isSupportedSize (savedSize) ? savedSize : 9;

    {
        const juce::SpinLock::ScopedLockType sl (boardLock);

        game = {};
        sgfText.clear();
        sourceName.clear();
        gameMovePosition = 0;

        board.setSize (size);
        board.clear();

        if (savedSgf.isNotEmpty())
        {
            const auto parsed = sgf::parse (savedSgf.toStdString());

            if (parsed.valid && parsed.size == size)
            {
                game = parsed;
                sgfText = savedSgf;
                sourceName = sgfName;
                gameMovePosition = juce::jlimit (0, parsed.moveCount(), position);
            }
        }

        if (boardText.isNotEmpty())
            board.fromString (boardText.toStdString());

        publishBoard();
    }

    activeSize.store (size, std::memory_order_relaxed);
    gameMoveTotal.store (game.moveCount(), std::memory_order_relaxed);
    gamePositionMirror.store (gameMovePosition, std::memory_order_relaxed);
    nextAlternating.store (nextColour == (int) go::Stone::white ? (int) go::Stone::white
                                                                : (int) go::Stone::black,
                           std::memory_order_relaxed);

    cancelPendingUpdate();      //  the size is already where the state wants it
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GoSequencerProcessor();
}
