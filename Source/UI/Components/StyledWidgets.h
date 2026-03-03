#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UI/Theme.h"
#include "DarkLookAndFeel.h"

/**
 * Pre-styled slider with dark theme colors.
 */
class StyledSlider : public juce::Slider
{
public:
    StyledSlider()
    {
        setSliderStyle(juce::Slider::LinearHorizontal);
        setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::Slider::backgroundColourId, APP_COLOR_SURFACE_ALT);
        setColour(juce::Slider::trackColourId, APP_COLOR_PRIMARY.withAlpha(0.75f));
        setColour(juce::Slider::thumbColourId, APP_COLOR_PRIMARY);
        setColour(juce::Slider::textBoxTextColourId, APP_COLOR_TEXT_PRIMARY);
        setColour(juce::Slider::textBoxBackgroundColourId, APP_COLOR_SURFACE_ALT);
        setColour(juce::Slider::textBoxOutlineColourId, APP_COLOR_BORDER_SUBTLE);
    }
};

/**
 * Pre-styled combo box with dark theme colors.
 */
class StyledComboBox : public juce::ComboBox
{
public:
    StyledComboBox()
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ComboBox::backgroundColourId, APP_COLOR_SURFACE);
        setColour(juce::ComboBox::textColourId, APP_COLOR_TEXT_PRIMARY);
        setColour(juce::ComboBox::outlineColourId, APP_COLOR_BORDER);
        setColour(juce::ComboBox::arrowColourId, APP_COLOR_PRIMARY);
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledComboBox() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled toggle button with custom checkbox.
 */
class StyledToggleButton : public juce::ToggleButton
{
public:
    StyledToggleButton(const juce::String& buttonText = {}) : juce::ToggleButton(buttonText)
    {
        applyStyle();
    }

    void applyStyle()
    {
        setColour(juce::ToggleButton::textColourId, APP_COLOR_TEXT_PRIMARY);
        setLookAndFeel(&DarkLookAndFeel::getInstance());
    }

    ~StyledToggleButton() override
    {
        setLookAndFeel(nullptr);
    }
};

/**
 * Pre-styled label with light grey text.
 */
class StyledLabel : public juce::Label
{
public:
    StyledLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    }
};

/**
 * Section header label with primary color.
 */
class SectionLabel : public juce::Label
{
public:
    SectionLabel(const juce::String& text = {})
    {
        setText(text, juce::dontSendNotification);
        setColour(juce::Label::textColourId, APP_COLOR_PRIMARY);
        setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    }
};
