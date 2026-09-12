#include "BoardComponent.h"

namespace
{
    //  Go coordinates skip the letter I
    const char* const columnNames[] = { "A", "B", "C", "D", "E", "F", "G", "H", "J",
                                        "K", "L", "M", "N" };
}

//==============================================================================
BoardComponent::BoardComponent (GoSequencerProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

void BoardComponent::setShowPath (bool shouldShow)
{
    showPath = shouldShow;
    repaint();
}

//==============================================================================
juce::Rectangle<float> BoardComponent::woodArea() const
{
    auto area = getLocalBounds().toFloat();
    const float side = juce::jmin (area.getWidth(), area.getHeight());

    return juce::Rectangle<float> (side, side).withCentre (area.getCentre());
}

float BoardComponent::inset() const   { return woodArea().getWidth() * 0.075f; }

float BoardComponent::spacing() const
{
    return (woodArea().getWidth() - 2.0f * inset()) / (float) juce::jmax (1, size() - 1);
}

juce::Point<float> BoardComponent::pointFor (int idx) const
{
    const auto wood = woodArea();
    const float s = spacing();
    const int n = size();

    return { wood.getX() + inset() + (float) go::colOf (idx, n) * s,
             wood.getY() + inset() + (float) go::rowOf (idx, n) * s };
}

int BoardComponent::indexFor (juce::Point<float> p) const
{
    const auto wood = woodArea();
    const float s = spacing();
    const int n = size();

    if (s <= 0.0f)
        return -1;

    const int col = juce::roundToInt ((p.x - wood.getX() - inset()) / s);
    const int row = juce::roundToInt ((p.y - wood.getY() - inset()) / s);

    if (col < 0 || col >= n || row < 0 || row >= n)
        return -1;

    const int idx = go::index (col, row, n);

    return pointFor (idx).getDistanceFrom (p) <= s * 0.55f ? idx : -1;
}

//==============================================================================
void BoardComponent::timerCallback()
{
    bool needsRepaint = false;

    const int currentStep = processor.currentStep();

    if (currentStep != lastDrawnStep)
    {
        lastDrawnStep = currentStep;
        needsRepaint = true;
    }

    const int gameMove = processor.gamePosition();

    if (gameMove != lastDrawnMove)
    {
        lastDrawnMove = gameMove;
        needsRepaint = true;
    }

    if (flashAlpha > 0.0f)
    {
        flashAlpha = juce::jmax (0.0f, flashAlpha - 0.06f);

        if (flashAlpha <= 0.0f)
            flashIndex = -1;

        needsRepaint = true;
    }

    if (needsRepaint)
        repaint();
}

void BoardComponent::mouseMove (const juce::MouseEvent& e)
{
    const int idx = indexFor (e.position);

    if (idx != hoverIndex)
    {
        hoverIndex = idx;
        repaint();
    }
}

void BoardComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverIndex >= 0)
    {
        hoverIndex = -1;
        repaint();
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
        processor.eraseStone (idx);
        repaint();
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

    repaint();
}

//==============================================================================
void BoardComponent::paint (juce::Graphics& g)
{
    paintWood (g);
    paintGrid (g);

    if (showPath)
        paintPath (g);

    paintStones (g);
    paintPlayhead (g);

    if (flashIndex >= 0 && flashAlpha > 0.0f)
    {
        const float r = spacing() * 0.5f;
        g.setColour (juce::Colour (0xffd6412f).withAlpha (flashAlpha));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (pointFor (flashIndex)), 2.5f);
    }
}

void BoardComponent::paintWood (juce::Graphics& g)
{
    const auto wood = woodArea();

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (wood.translated (0.0f, 5.0f).expanded (1.0f), 5.0f);

    g.setGradientFill (juce::ColourGradient (theme::woodLight, wood.getTopLeft(),
                                             theme::woodDark, wood.getBottomRight(), false));
    g.fillRoundedRectangle (wood, 5.0f);

    //  a little grain, fixed seed so it does not shimmer between repaints
    juce::Random random (0x60ba17);
    g.saveState();
    g.reduceClipRegion (wood.toNearestInt());

    for (int i = 0; i < 110; ++i)
    {
        const float x = wood.getX() + random.nextFloat() * wood.getWidth();
        const float w = 0.6f + random.nextFloat() * 2.2f;

        g.setColour (juce::Colour (0xff8a5a22).withAlpha (0.02f + random.nextFloat() * 0.035f));
        g.fillRect (x, wood.getY(), w, wood.getHeight());
    }

    g.restoreState();

    g.setColour (juce::Colour (0xff6b4a22).withAlpha (0.55f));
    g.drawRoundedRectangle (wood.reduced (0.5f), 5.0f, 1.0f);
}

void BoardComponent::paintGrid (juce::Graphics& g)
{
    const int n = size();
    const float s = spacing();
    const float lineWidth = juce::jmax (1.0f, s * 0.028f);

    const auto topLeft     = pointFor (go::index (0, 0, n));
    const auto bottomRight = pointFor (go::index (n - 1, n - 1, n));

    g.setColour (theme::lines.withAlpha (0.9f));

    for (int i = 0; i < n; ++i)
    {
        const float y = topLeft.y + (float) i * s;
        const float x = topLeft.x + (float) i * s;
        const bool edge = (i == 0 || i == n - 1);
        const float w = edge ? lineWidth * 1.8f : lineWidth;

        g.fillRect (topLeft.x - w * 0.5f, y - w * 0.5f, bottomRight.x - topLeft.x + w, w);
        g.fillRect (x - w * 0.5f, topLeft.y - w * 0.5f, w, bottomRight.y - topLeft.y + w);
    }

    const auto stars = go::starPoints (n);
    const float starRadius = juce::jmax (1.6f, s * 0.085f);

    g.setColour (theme::lines);

    for (int star : stars)
        g.fillEllipse (juce::Rectangle<float> (starRadius * 2.0f, starRadius * 2.0f).withCentre (pointFor (star)));

    g.setColour (theme::lines.withAlpha (0.5f));
    g.setFont (juce::jmax (8.0f, s * 0.26f));

    for (int i = 0; i < n; ++i)
    {
        const auto columnPoint = pointFor (go::index (i, 0, n));
        g.drawText (columnNames[i],
                    juce::Rectangle<float> (s, inset() * 0.8f)
                        .withCentre ({ columnPoint.x, woodArea().getY() + inset() * 0.45f }),
                    juce::Justification::centred);

        const auto rowPoint = pointFor (go::index (0, i, n));
        g.drawText (juce::String (n - i),
                    juce::Rectangle<float> (inset() * 0.85f, s)
                        .withCentre ({ woodArea().getX() + inset() * 0.45f, rowPoint.y }),
                    juce::Justification::centred);
    }
}

void BoardComponent::paintPath (juce::Graphics& g)
{
    const float stroke = juce::jmax (1.0f, spacing() * 0.05f);

    if (processor.isQuads())
    {
        //  four spirals, one per quadrant, drawn where they actually run
        const int steps = go::quadSteps (size());
        const float dot = spacing() * 0.2f;

        for (int q = 0; q < go::quadCount; ++q)
        {
            juce::Path path;
            path.startNewSubPath (pointFor (processor.quadCellAt (q, 0)));

            for (int i = 1; i < steps; ++i)
                path.lineTo (pointFor (processor.quadCellAt (q, i)));

            g.setColour (theme::accent.withAlpha (0.20f));
            g.strokePath (path, juce::PathStrokeType (stroke,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));

            //  where this quadrant's walk begins
            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f)
                               .withCentre (pointFor (processor.quadCellAt (q, 0))));
        }

        return;
    }

    if (processor.isPolyrhythm())
    {
        const int n = size();
        const float dot = spacing() * 0.16f;

        for (int r = 0; r < processor.ringCount(); ++r)
        {
            const auto topLeft     = pointFor (go::index (r, r, n));
            const auto bottomRight = pointFor (go::index (n - 1 - r, n - 1 - r, n));

            g.setColour (theme::accent.withAlpha (0.24f));
            g.drawRect (juce::Rectangle<float> (topLeft, bottomRight), stroke);

            //  every ring starts at its own top left corner and runs clockwise
            g.setColour (theme::accent.withAlpha (0.55f));
            g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (topLeft));
        }

        return;
    }

    const int steps = processor.stepCount();

    juce::Path path;
    path.startNewSubPath (pointFor (processor.spiralAt (0)));

    for (int i = 1; i < steps; ++i)
        path.lineTo (pointFor (processor.spiralAt (i)));

    g.setColour (theme::accent.withAlpha (0.20f));
    g.strokePath (path, juce::PathStrokeType (stroke,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));

    //  mark where the walk begins
    const auto start = pointFor (processor.spiralAt (0));
    const float r = spacing() * 0.22f;

    juce::Path arrow;
    arrow.addTriangle (start.x - r, start.y - r * 1.6f,
                       start.x + r, start.y - r * 1.6f,
                       start.x, start.y - r * 0.5f);
    g.setColour (theme::accent.withAlpha (0.55f));
    g.fillPath (arrow);
}

void BoardComponent::drawStone (juce::Graphics& g, juce::Point<float> centre, float radius,
                                bool black, float alpha)
{
    const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    juce::Graphics::ScopedSaveState state (g);

    if (alpha < 1.0f)
        g.setOpacity (alpha);

    g.setColour (juce::Colours::black.withAlpha (0.3f * alpha));
    g.fillEllipse (bounds.translated (radius * 0.09f, radius * 0.13f));

    juce::ColourGradient gradient (black ? juce::Colour (0xff5c5c62) : juce::Colour (0xfffffdf6),
                                   centre.x - radius * 0.35f, centre.y - radius * 0.42f,
                                   black ? juce::Colour (0xff08080b) : juce::Colour (0xffc8bfa9),
                                   centre.x + radius * 0.7f, centre.y + radius * 0.8f,
                                   true);
    g.setGradientFill (gradient);
    g.setOpacity (alpha);
    g.fillEllipse (bounds);

    g.setColour ((black ? juce::Colours::black : juce::Colour (0xff9c9382)).withAlpha (0.45f * alpha));
    g.drawEllipse (bounds.reduced (0.4f), 0.8f);

    g.setColour (juce::Colours::white.withAlpha ((black ? 0.16f : 0.55f) * alpha));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.5f, radius * 0.34f)
                       .withCentre (centre.translated (-radius * 0.3f, -radius * 0.42f)));
}

void BoardComponent::paintStones (juce::Graphics& g)
{
    const float radius = spacing() * 0.46f;
    const int cells = processor.stepCount();
    const int lastMove = processor.lastMove();

    if (hoverIndex >= 0 && hoverIndex < cells && processor.stoneAt (hoverIndex) == go::Stone::none)
        drawStone (g, pointFor (hoverIndex), radius,
                   processor.colourForNextMove() == go::Stone::black, 0.35f);

    for (int i = 0; i < cells; ++i)
    {
        const auto stone = processor.stoneAt (i);

        if (stone == go::Stone::none)
            continue;

        //  a spent stone is drawn like the one under the cursor: there, but ghosted
        drawStone (g, pointFor (i), radius, stone == go::Stone::black,
                   processor.stoneIsSpent (i) ? 0.35f : 1.0f);
    }

    if (lastMove >= 0 && lastMove < cells && processor.stoneAt (lastMove) != go::Stone::none)
    {
        const bool black = processor.stoneAt (lastMove) == go::Stone::black;
        g.setColour ((black ? juce::Colours::white : juce::Colours::black).withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (radius * 0.7f, radius * 0.7f).withCentre (pointFor (lastMove)), 1.6f);
    }
}

void BoardComponent::paintPlayhead (juce::Graphics& g)
{
    const bool active = processor.isRunning();

    auto drawHead = [this, &g, active] (int idx, float weight)
    {
        const auto centre = pointFor (idx);
        const float s = spacing();

        g.setColour (theme::accent.withAlpha ((active ? 0.95f : 0.4f) * weight));

        if (processor.stoneAt (idx) != go::Stone::none)
        {
            g.drawEllipse (juce::Rectangle<float> (s * 1.06f, s * 1.06f).withCentre (centre), juce::jmax (1.5f, s * 0.055f));
        }
        else
        {
            g.drawEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.5f).withCentre (centre), juce::jmax (1.2f, s * 0.04f));
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
