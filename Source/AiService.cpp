#include "AiService.h"

AiService::AiService (std::function<void()> callback)
    : juce::Thread ("Go Sequencer search"), onResult (std::move (callback))
{
}

AiService::~AiService()
{
    cancelGames();
    cancelReply();
    signalThreadShouldExit();
    wake.signal();
    stopThread (30000);     //  a move in progress finishes first; they take about a second
}

//==============================================================================
void AiService::requestGame (const GameRequest& request)
{
    {
        const juce::ScopedLock sl (lock);
        auto& slot = request.current ? currentGame : nextGame;

        (request.current ? currentRequest : nextRequest) = request;
        (request.current ? currentResult : nextResult).reset();
        slot.requested = ++slot.token;
    }

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);

    wake.signal();
}

void AiService::requestReply (const ReplyRequest& request)
{
    {
        const juce::ScopedLock sl (lock);
        replyRequest = request;
        replyResult.reset();
        answer.requested = ++answer.token;
    }

    if (! isThreadRunning())
        startThread (juce::Thread::Priority::normal);

    wake.signal();
}

void AiService::cancelGames()
{
    cancelGame (true);
    cancelGame (false);
}

void AiService::cancelGame (bool current)
{
    const juce::ScopedLock sl (lock);
    auto& slot = current ? currentGame : nextGame;

    ++slot.token;
    slot.requested = 0;
    (current ? currentResult : nextResult).reset();
}

void AiService::cancelReply()
{
    const juce::ScopedLock sl (lock);
    ++answer.token;
    answer.requested = 0;
    replyResult.reset();
}

std::optional<AiService::GameResult> AiService::takeGame (bool current)
{
    const juce::ScopedLock sl (lock);
    auto& result = current ? currentResult : nextResult;
    auto taken = std::move (result);
    result.reset();
    return taken;
}

std::optional<AiService::ReplyResult> AiService::takeReply()
{
    const juce::ScopedLock sl (lock);
    auto taken = std::move (replyResult);
    replyResult.reset();
    return taken;
}

bool AiService::thinkingAboutGame (bool current) const noexcept
{
    const auto& slot = current ? currentGame : nextGame;
    return slot.working.load() || slot.requested != 0;
}

bool AiService::thinkingAboutReply() const noexcept
{
    return answer.working.load() || answer.requested != 0;
}

//==============================================================================
void AiService::run()
{
    workspace = std::make_unique<gosearch::Workspace>();

    while (! threadShouldExit())
    {
        //  the most urgent waiting request, copied out so the lock is not held
        //  while it is worked on
        enum class Kind { none, reply, current, next } kind = Kind::none;
        int token = 0;
        GameRequest game;
        ReplyRequest reply;

        {
            const juce::ScopedLock sl (lock);

            if (answer.requested != 0)
            {
                kind = Kind::reply;
                token = answer.requested;
                reply = replyRequest;
                answer.requested = 0;
                answer.working = true;
            }
            else if (currentGame.requested != 0)
            {
                kind = Kind::current;
                token = currentGame.requested;
                game = currentRequest;
                currentGame.requested = 0;
                currentGame.working = true;
            }
            else if (nextGame.requested != 0)
            {
                kind = Kind::next;
                token = nextGame.requested;
                game = nextRequest;
                nextGame.requested = 0;
                nextGame.working = true;
            }
        }

        if (kind == Kind::none)
        {
            wake.wait (1000);
            continue;
        }

        if (kind == Kind::reply)
        {
            ReplyResult result;
            result.colour = reply.colour;
            result.positionToken = reply.positionToken;
            result.move = gosearch::reply (reply.board, reply.colour, reply.settings, reply.seed, *workspace,
                                           &result.considered);

            {
                const juce::ScopedLock sl (lock);

                if (answer.token == token)
                    replyResult = std::move (result);

                answer.working = false;
            }
        }
        else
        {
            auto& slot = kind == Kind::current ? currentGame : nextGame;

            //  a game is given up the moment anything newer is asked of this slot,
            //  or an answer is wanted - the answer goes first and the game starts
            //  over after it, which gives the same game: a seed names it
            const auto keepGoing = [this, &slot, token]
            {
                return ! threadShouldExit() && slot.token.load() == token && answer.requested == 0;
            };

            GameResult result;
            result.game = gosearch::generate (game.settings, keepGoing, *workspace);
            result.number = game.number;
            result.seed = game.settings.seed;
            result.current = game.current;
            result.restorePosition = game.restorePosition;

            {
                const juce::ScopedLock sl (lock);

                if (slot.token.load() == token && result.game.valid)
                    (game.current ? currentResult : nextResult) = std::move (result);
                else if (slot.token.load() == token)
                    slot.requested = token;         //  interrupted by an answer: do it again after

                slot.working = false;
            }
        }

        if (onResult != nullptr)
            onResult();
    }
}
