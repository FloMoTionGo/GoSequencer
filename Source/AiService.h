#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "GoSearch.h"

//==============================================================================
/** The search player's work, off the message thread.

    A search takes seconds, not milliseconds - a whole self-play game about
    five, an answer in a match about one - so it cannot run where the editor and
    the host run. This thread takes three kinds of request and hands the results
    back for the message thread to collect:

      - the game that should be playing now (on a switch-on or a session load),
      - the game after it, prepared while the current one plays,
      - an answer in a match: every move it considered, best first, and the one
        to play.

    An answer outranks a game that should be playing now, which outranks the next
    game. Each kind holds at most one request: a newer one replaces a waiting
    one, and makes a running one stop at its next move - every request carries a
    token, and a result whose token is no longer current is thrown away.

    The thread starts on the first request, so an instance that never uses the
    search player never has one. `onResult` is called on this thread; the
    processor uses it to trigger its AsyncUpdater. */
class AiService final : private juce::Thread
{
public:
    explicit AiService (std::function<void()> onResult);
    ~AiService() override;

    struct GameRequest
    {
        goai::Settings settings;
        int number = 0;
        bool current = true;            //  the game to play now, or the one after it
        int restorePosition = -1;       //  where a restored session left the record
    };

    struct GameResult
    {
        sgf::Game game;
        int number = 0;
        unsigned int seed = 0;
        bool current = true;
        int restorePosition = -1;
    };

    struct ReplyRequest
    {
        go::Board board { 9 };
        go::Stone colour = go::Stone::white;
        goai::Settings settings;
        std::uint32_t seed = 0;
        int positionToken = 0;          //  the processor's count of what the board has seen
    };

    struct ReplyResult
    {
        int move = -1;                  //  -1 a pass
        go::Stone colour = go::Stone::white;
        int positionToken = 0;
        std::vector<gosearch::Choice> considered;
    };

    /** Message thread. */
    void requestGame (const GameRequest&);
    void requestReply (const ReplyRequest&);
    void cancelGames();
    void cancelGame (bool current);
    void cancelReply();

    /** Message thread: a finished result, if there is one. */
    std::optional<GameResult> takeGame (bool current);
    std::optional<ReplyResult> takeReply();

    /** Any thread: whether it is working on, or has waiting, a request of that kind. */
    bool thinkingAboutGame (bool current) const noexcept;
    bool thinkingAboutReply() const noexcept;

private:
    void run() override;

    struct Slot
    {
        std::atomic<int> token { 0 };       //  bumped by every request and cancel
        std::atomic<int> requested { 0 };   //  the token of the waiting request, 0 none
        std::atomic<bool> working { false };
    };

    std::function<void()> onResult;

    juce::CriticalSection lock;
    juce::WaitableEvent wake;

    Slot currentGame, nextGame, answer;

    //  guarded by lock
    GameRequest currentRequest, nextRequest;
    ReplyRequest replyRequest;
    std::optional<GameResult> currentResult, nextResult;
    std::optional<ReplyResult> replyResult;

    std::unique_ptr<gosearch::Workspace> workspace;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiService)
};
