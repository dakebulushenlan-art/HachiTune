#pragma once

#include "../../JuceHeader.h"
#include "../../Utils/UI/Theme.h"
#include <functional>
#include <memory>

/**
 * Custom styled message box component matching the app's dark theme.
 */
class StyledMessageBox : public juce::Component
{
public:
    enum IconType
    {
        NoIcon,
        InfoIcon,
        WarningIcon,
        ErrorIcon
    };

    StyledMessageBox(const juce::String& title, const juce::String& message, IconType iconTypeValue = NoIcon)
        : titleText(title), messageText(message), iconType(iconTypeValue)
    {
        setOpaque(true);
        
        // Add OK button
        okButton = std::make_unique<juce::TextButton>("OK");
        okButton->setSize(80, 32);
        okButton->onClick = [this] { 
            if (onClose != nullptr)
                onClose();
        };
        addAndMakeVisible(okButton.get());
        
        // Style the button
        okButton->setColour(juce::TextButton::buttonColourId, APP_COLOR_PRIMARY);
        okButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        okButton->setColour(juce::TextButton::buttonOnColourId, APP_COLOR_PRIMARY.brighter(0.2f));
        
        setSize(400, 200);
    }
    
    void resized() override
    {
        if (okButton != nullptr)
        {
            okButton->setCentrePosition(getWidth() / 2, getHeight() - 30);
        }
    }
    
    std::function<void()> onClose;

    void paint(juce::Graphics& g) override
    {
        // Background
        g.fillAll(APP_COLOR_BACKGROUND);

        // Title
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        g.drawText(titleText, 20, 20, getWidth() - 40, 30, juce::Justification::left);

        // Icon (if any)
        int iconX = 20;
        int iconY = 60;
        int iconSize = 32;
        
        if (iconType != NoIcon)
        {
            juce::Colour iconColour;
            if (iconType == InfoIcon)
                iconColour = APP_COLOR_PRIMARY;
            else if (iconType == WarningIcon)
                iconColour = APP_COLOR_ALERT_WARNING;
            else if (iconType == ErrorIcon)
                iconColour = APP_COLOR_ALERT_ERROR;

            g.setColour(iconColour);
            g.fillEllipse(iconX, iconY, iconSize, iconSize);
            g.setColour(APP_COLOR_BACKGROUND);
            g.setFont(juce::Font(
                juce::FontOptions(iconSize * 0.6f, juce::Font::bold)));
            
            juce::String iconChar;
            if (iconType == InfoIcon)
                iconChar = "i";
            else if (iconType == WarningIcon)
                iconChar = "!";
            else if (iconType == ErrorIcon)
                iconChar = "X";

            g.drawText(iconChar, iconX, iconY, iconSize, iconSize, juce::Justification::centred);
            iconX += iconSize + 15;
        }

        // Message text
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::Font(juce::FontOptions(15.0f)));
        g.drawMultiLineText(messageText, iconX, iconY + 5, getWidth() - iconX - 20, juce::Justification::topLeft);
    }

    static void show(juce::Component* parent, const juce::String& title, const juce::String& message, IconType iconType = NoIcon)
    {
        auto* dialog = new StyledMessageDialog(parent, title, message, iconType);
        dialog->enterModalState(true, nullptr, true);
    }

private:
    juce::String titleText;
    juce::String messageText;
    IconType iconType;
    std::unique_ptr<juce::TextButton> okButton;

    class StyledMessageDialog : public juce::DialogWindow
    {
    public:
        StyledMessageDialog(juce::Component* parent, const juce::String& title, const juce::String& message, StyledMessageBox::IconType iconTypeValue)
            : juce::DialogWindow(title, APP_COLOR_BACKGROUND, true)
        {
            setOpaque(true);
            setUsingNativeTitleBar(false);
            setResizable(false, false);
            
            // Remove close button from title bar
            setTitleBarButtonsRequired(0, false);
            
            messageBox = std::make_unique<StyledMessageBox>(title, message, iconTypeValue);
            messageBox->onClose = [this] { closeDialog(); };
            setContentOwned(messageBox.get(), false);
            
            int dialogWidth = 420;
            int dialogHeight = 220;
            setSize(dialogWidth, dialogHeight);
            
            if (parent != nullptr)
                centreAroundComponent(parent, dialogWidth, dialogHeight);
            else
                centreWithSize(dialogWidth, dialogHeight);
        }

        void closeButtonPressed() override
        {
            // This should not be called since we removed the close button
            closeDialog();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(APP_COLOR_BACKGROUND);
        }

    private:
        void closeDialog()
        {
            exitModalState(0);
        }

        std::unique_ptr<StyledMessageBox> messageBox;
    };
};
