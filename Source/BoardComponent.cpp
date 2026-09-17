#include "BoardComponent.h"

namespace
{
    //  Go coordinates skip the letter I
    const char* const columnNames[go::maxSize] = { "A", "B", "C", "D", "E", "F", "G", "H", "J",
                                                   "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T" };
}

//==============================================================================
BoardComponent::BoardComponent (GoSequencerProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (false);

    //  the grid is an image until something it shows changes; clicks go through
    //  both layers to this component
    grid.setBufferedToImage (true);
    grid.setOpaque (true);

    for (auto* layer : { (juce::Component*) &grid, (juce::Component*) &stones })
    {
        layer->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (layer);
    }

    rememberEverything();
    startTimerHz (30);
}

void BoardComponent::resized()
{
    grid.setBounds (getLocalBounds());      //  a new size draws the grid image again
    stones.setBounds (getLocalBounds());
}

void BoardComponent::refreshAll()
{
    rememberEverything();
    grid.repaint();
    stones.repaint();
}

//==============================================================================
juce::Rectangle<float> BoardComponent::boardArea() const
{
    auto area = getLocalBounds().toFloat();
    const float side = juce::jmin (area.getWidth(), area.getHeight());

    return juce::Rectangle<float> (side, side).withCentre (area.getCentre());
}

float BoardComponent::inset() const   { return boardArea().getWidth() * 0.075f; }

float BoardComponent::spacing() const
{
    return (boardArea().getWidth() - 2.0f * inset()) / (float) juce::jmax (1, size() - 1);
}

juce::Point<float> BoardComponent::pointFor (int idx) const
{
    const auto board = boardArea();
    const float s = spacing();
    const int n = size();

    return { board.getX() + inset() + (float) go::colOf (idx, n) * s,
             board.getY() + inset() + (float) go::rowOf (idx, n) * s };
}

int BoardComponent::indexFor (juce::Point<float> p) const
{
    const auto board = boardArea();
    const float s = spacing();
    const int n = size();

    if (s <= 0.0f)
        return -1;

    const int col = juce::roundToInt ((p.x - board.getX() - inset()) / s);
    const int row = juce::roundToInt ((p.y - board.getY() - inset()) / s);

    if (col < 0 || col >= n || row < 0 || row >= n)
        return -1;

    const int idx = go::index (col, row, n);

    return pointFor (idx).getDistanceFrom (p) <= s * 0.55f ? idx : -1;
}

juce::Rectangle<int> BoardComponent::cellBounds (int idx) const
{
    //  the widest thing drawn on a point is the playhead ring round a stone:
    //  1.06 spacings across plus its stroke
    const float side = spacing() * 1.3f;
    return juce::Rectangle<float> (side, side).withCentre (pointFor (idx)).getSmallestIntegerContainer().expanded (2);
}

void BoardComponent::repaintCell (int idx)
{
    if (idx >= 0 && idx < processor.stepCount())
        stones.repaint (cellBounds (idx));
}

std::uint8_t BoardComponent::cellState (int idx) const noexcept
{
    const auto stone = processor.stoneAt (idx);

    if (stone == go::Stone::none)
        return 0;

    return (std::uint8_t) ((int) stone
                           | (processor.stoneIsSpent (idx) ? 4 : 0)
                           | (processor.lastMove() == idx ? 8 : 0));
}

int BoardComponent::headCells (std::array<int, go::maxRings>& cells) const noexcept
{
    const int heads = juce::jmin (processor.headCount(), go::maxRings);

    if (heads > 1)
    {
        for (int h = 0; h < heads; ++h)
            cells[(size_t) h] = processor.headCellAt (h, processor.headPosition (h));

        return heads;
    }

    const int stepIndex = juce::jlimit (0, juce::jmax (0, processor.stepCount() - 1), processor.currentStep());
    cells[0] = processor.spiralAt (stepIndex);
    return 1;
}

void BoardComponent::rememberEverything()
{
    drawnSize = size();
    drawnDark = theme::isDark;
    drawnRunning = processor.isRunning();
    drawnHeadCount = headCells (drawnHeads);
    drawnHoverColour = processor.colourForNextMove();

    for (int i = 0; i < go::maxCells; ++i)
        drawnCells[(size_t) i] = i < processor.stepCount() ? cellState (i) : 0;
}

//==============================================================================
void BoardComponent::timerCallback()
{
    //  a new board or colour scheme: nothing on screen is right any more. Normally
    //  the editor says so through refreshAll; this catches anything that does not.
    if (size() != drawnSize || theme::isDark != drawnDark)
    {
        refreshAll();
        return;
    }

    //  Stones: whatever put them there - the record, a pad, the players' answer,
    //  a stone running out of life - shows up as a point that reads differently.
    const int cells = processor.stepCount();

    for (int i = 0; i < cells; ++i)
    {
        const auto state = cellState (i);

        if (state != drawnCells[(size_t) i])
        {
            drawnCells[(size_t) i] = state;
            repaintCell (i);
        }
    }

    //  Playheads: where each one left and where it is now. Starting or stopping
    //  changes how bright they all are, and a mode change how many there are.
    std::array<int, go::maxRings> heads {};
    const int headCount = headCells (heads);
    const bool running = processor.isRunning();

    if (headCount != drawnHeadCount || running != drawnRunning)
    {
        for (int h = 0; h < drawnHeadCount; ++h) repaintCell (drawnHeads[(size_t) h]);
        for (int h = 0; h < headCount; ++h)      repaintCell (heads[(size_t) h]);
    }
    else
    {
        for (int h = 0; h < headCount; ++h)
        {
            if (heads[(size_t) h] != drawnHeads[(size_t) h])
            {
                repaintCell (drawnHeads[(size_t) h]);
                repaintCell (heads[(size_t) h]);
            }
        }
    }

    drawnHeads = heads;
    drawnHeadCount = headCount;
    drawnRunning = running;

    //  the ghost under the cursor is the colour the next stone will be
    const auto hoverColour = processor.colourForNextMove();

    if (hoverColour != drawnHoverColour)
    {
        drawnHoverColour = hoverColour;
        repaintCell (hoverIndex);
    }

    if (flashAlpha > 0.0f)
    {
        flashAlpha = juce::jmax (0.0f, flashAlpha - 0.06f);
        repaintCell (flashIndex);

        if (flashAlpha <= 0.0f)
            flashIndex = -1;
    }
}

void BoardComponent::mouseMove (const juce::MouseEvent& e)
{
    const int idx = indexFor (e.position);

    if (idx != hoverIndex)
    {
        repaintCell (hoverIndex);
        hoverIndex = idx;
        repaintCell (hoverIndex);
    }
}

void BoardComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0)
    {
        repaintCell (hoverIndex);
        hoverIndex = -1;
    }
}

void BoardComponent::mouseDown (const juce::MouseEvent& e)
{
    const int idx = indexFor (e.position);

    if (idx < 0)
        return;

    const bool wantsErase = e.mods.isRightButtonDown() || e.mods.isShiftDown() || e.mods.isAltDown();

    if (wantsErase || processor.stoneAt (idx) != go::Stone::none)
    {
        //  the eraser is not a thing you may use in a game: the position is the
        //  record of it, and lifting a move back out would make it another game
        if (! processor.eraseAllowed())
        {
            flashIndex = idx;
            flashAlpha = 1.0f;

            if (onMessage != nullptr)
                onMessage ("a game is on: a move cannot be taken back out of it");

            stones.repaint();
            return;
        }

        processor.eraseStone (idx);
        stones.repaint();
        return;
    }

    //  in a game the board waits for them as well as for you - the processor
    //  refuses the move either way, but this is where it can say why
    if (processor.matchActive() && ! processor.yourTurn())
    {
        flashIndex = idx;
        flashAlpha = 1.0f;

        if (onMessage != nullptr)
            onMessage (processor.matchIsOver() ? "the game is over - New game starts another"
                                               : "their move");

        stones.repaint();
        return;
    }

    const auto result = processor.placeStone (idx);

    if (result != go::MoveResult::ok)
    {
        flashIndex = idx;
        flashAlpha = 1.0f;

        if (onMessage != nullptr)
        {
            switch (result)
            {
                case go::MoveResult::suicide:
                    onMessage ("no liberties: self capture is not a legal move");
                    break;
                case go::MoveResult::ko:
                    onMessage ("ko: that would repeat the previous position");
                    break;
                case go::MoveResult::occupied:
                    onMessage ("that point is taken");
                    break;
                default:
                    break;
            }
        }
    }
    else if (onMessage != nullptr)
    {
        onMessage ({});
    }

    stones.repaint();
}

//==============================================================================
void BoardComponent::GridLayer::paint (juce::Graphics& g)
{
    //  opaque, so a cell repainted above it never reaches the editor behind
    g.fillAll (theme::background);
    owner.paintSurface (g);
    owner.paintGrid (g);
}

void BoardComponent::StoneLayer::paint (juce::Graphics& g)
{
    owner.paintStones (g);
    owner.paintPlayhead (g);

    if (owner.flashIndex >= 0 && owner.flashAlpha > 0.0f)
    {
        const float r = owner.spacing() * 0.5f;
        g.setColour (theme::error.withAlpha (owner.flashAlpha));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (owner.pointFor (owner.flashIndex)), 2.0f);
    }
}

void BoardComponent::paintSurface (juce::Graphics& g)
{
    //  one flat step down from the panel: no wood, no shadow, no edge
    g.setColour (theme::boardFill);
    g.fillRect (boardArea());
}

void BoardComponent::paintGrid (juce::Graphics& g)
{
    const int n = size();
    const float s = spacing();
    const float lineWidth = juce::jmax (1.0f, s * 0.02f);

    const auto topLeft     = pointFor (go::index (0, 0, n));
    const auto bottomRight = pointFor (go::index (n - 1, n - 1, n));

    g.setColour (theme::gridLine);

    for (int i = 0; i < n; ++i)
    {
        const float y = topLeft.y + (float) i * s;
        const float x = topLeft.x + (float) i * s;
        const bool edge = (i == 0 || i == n - 1);
        const float w = edge ? lineWidth * 1.4f : lineWidth;

        g.fillRect (topLeft.x - w * 0.5f, y - w * 0.5f, bottomRight.x - topLeft.x + w, w);
        g.fillRect (x - w * 0.5f, topLeft.y - w * 0.5f, w, bottomRight.y - topLeft.y + w);
    }

    std::array<int, go::maxHoshi> stars {};
    const int starCount = go::hoshiPoints (n, stars);
    const float starRadius = juce::jmax (1.5f, s * 0.06f);

    g.setColour (theme::faintText);

    for (int i = 0; i < starCount; ++i)
        g.fillEllipse (juce::Rectangle<float> (starRadius * 2.0f, starRadius * 2.0f)
                           .withCentre (pointFor (stars[(size_t) i])));

    g.setFont (theme::font (juce::jmax (8.0f, s * 0.22f)));

    for (int i = 0; i < n; ++i)
    {
        const auto columnPoint = pointFor (go::index (i, 0, n));
        g.drawText (columnNames[i],
                    juce::Rectangle<float> (s, inset() * 0.8f)
                        .withCentre ({ columnPoint.x, boardArea().getY() + inset() * 0.45f }),
                    juce::Justification::centred);

        const auto rowPoint = pointFor (go::index (0, i, n));
        g.drawText (juce::String (n - i),
                    juce::Rectangle<float> (inset() * 0.85f, s)
                        .withCentre ({ boardArea().getX() + inset() * 0.45f, rowPoint.y }),
                    juce::Justification::centred);
    }
}

void BoardComponent::drawStone (juce::Graphics& g, juce::Point<float> centre, float radius,
                                bool black, float alpha)
{
    //  flat discs in fixed colours. An edge goes round whichever stone is close
    //  to the ground it sits on: white on the light scheme, black on the dark one.
    const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    const float edge = juce::jmax (1.0f, radius * 0.08f);

    g.setColour ((black ? theme::stoneBlack : theme::stoneWhite).withMultipliedAlpha (alpha));
    g.fillEllipse (bounds);

    if (black && ! theme::isDark)
        return;

    g.setColour ((black ? theme::dimText : theme::faintText).withMultipliedAlpha (alpha));
    g.drawEllipse (bounds.reduced (edge * 0.5f), edge);
}

void BoardComponent::paintStones (juce::Graphics& g)
{
    const float radius = spacing() * 0.46f;
    const int cells = processor.stepCount();
    const int lastMove = processor.lastMove();

    if (hoverIndex >= 0 && hoverIndex < cells && processor.stoneAt (hoverIndex) == go::Stone::none)
        drawStone (g, pointFor (hoverIndex), radius,
                   processor.colourForNextMove() == go::Stone::black, 0.35f);

    //  a repaint asks for a few cells at a time: the rest are left alone
    const auto clip = g.getClipBounds();

    for (int i = 0; i < cells; ++i)
    {
        const auto stone = processor.stoneAt (i);

        if (stone == go::Stone::none || ! clip.intersects (cellBounds (i)))
            continue;

        //  a spent stone is drawn like the one under the cursor: there, but ghosted
        drawStone (g, pointFor (i), radius, stone == go::Stone::black,
                   processor.stoneIsSpent (i) ? 0.35f : 1.0f);
    }

    if (lastMove >= 0 && lastMove < cells && processor.stoneAt (lastMove) != go::Stone::none)
    {
        const bool black = processor.stoneAt (lastMove) == go::Stone::black;
        g.setColour (black ? theme::stoneWhite : theme::stoneBlack);
        g.drawEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (pointFor (lastMove)), 1.4f);
    }
}

void BoardComponent::paintPlayhead (juce::Graphics& g)
{
    const bool active = processor.isRunning();

    const auto clip = g.getClipBounds();

    auto drawHead = [this, &g, active, clip] (int idx, float weight)
    {
        if (! clip.intersects (cellBounds (idx)))
            return;

        const auto centre = pointFor (idx);
        const float s = spacing();

        g.setColour (theme::accent.withAlpha ((active ? 0.95f : 0.4f) * weight));

        if (processor.stoneAt (idx) != go::Stone::none)
        {
            g.drawEllipse (juce::Rectangle<float> (s * 1.06f, s * 1.06f).withCentre (centre), juce::jmax (1.5f, s * 0.045f));
        }
        else
        {
            g.drawEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.5f).withCentre (centre), juce::jmax (1.2f, s * 0.035f));
            g.setColour (theme::accent.withAlpha ((active ? 0.35f : 0.15f) * weight));
            g.fillEllipse (juce::Rectangle<float> (s * 0.28f, s * 0.28f).withCentre (centre));
        }
    };

    const int heads = processor.headCount();

    if (heads > 1)
    {
        //  the first head is the brightest, the rest fade back a little
        for (int h = 0; h < heads; ++h)
            drawHead (processor.headCellAt (h, processor.headPosition (h)),
                      1.0f - 0.45f * (float) h / (float) juce::jmax (1, heads - 1));

        return;
    }

    const int stepIndex = juce::jlimit (0, juce::jmax (0, processor.stepCount() - 1), processor.currentStep());

    drawHead (processor.spiralAt (stepIndex), 1.0f);
}
