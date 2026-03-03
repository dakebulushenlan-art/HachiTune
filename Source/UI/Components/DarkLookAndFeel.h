#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UI/Theme.h"
#include "AppFont.h"

/**
 * Shared dark theme LookAndFeel for the application.
 */
class DarkLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DarkLookAndFeel();

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return AppFont::getFont(15.0f);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return AppFont::getFont(15.0f);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return AppFont::getFont(15.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return AppFont::getFont(15.0f);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                          bool isSeparator, bool isActive, bool isHighlighted,
                          bool isTicked, bool hasSubMenu,
                          const juce::String& text, const juce::String& shortcutKeyText,
                          const juce::Drawable* icon, const juce::Colour* textColour) override;

    void drawTickBox(juce::Graphics& g, juce::Component& component,
                     float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar,
                         int width, int height, double progress,
                         const juce::String& textToShow) override;

    static DarkLookAndFeel& getInstance()
    {
        static DarkLookAndFeel instance;
        return instance;
    }
};
