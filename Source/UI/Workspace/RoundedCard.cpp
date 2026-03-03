#include "RoundedCard.h"

RoundedCard::RoundedCard()
{
    setOpaque(false);
}

void RoundedCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Flat background fill (no gradient — modern, clean)
    g.setColour(backgroundColour);
    g.fillRoundedRectangle(bounds, cornerRadius);

    // Single clean border
    g.setColour(borderColour.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

void RoundedCard::paintOverChildren(juce::Graphics& g)
{
    // Clean border on top for consistent rounded edge clipping
    auto bounds = getLocalBounds().toFloat();
    g.setColour(borderColour.withAlpha(0.55f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, 1.0f);
}

void RoundedCard::resized()
{
    if (contentComponent != nullptr)
    {
        contentComponent->setBounds(getLocalBounds().reduced(padding));
    }
}

void RoundedCard::setContentComponent(juce::Component* content)
{
    if (contentComponent != nullptr)
        removeChildComponent(contentComponent);

    contentComponent = content;

    if (contentComponent != nullptr)
    {
        addAndMakeVisible(contentComponent);
        resized();
    }
}
