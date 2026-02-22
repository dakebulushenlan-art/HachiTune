#pragma once

#include "../JuceHeader.h"
#include "../Models/Note.h"
#include "../Models/Project.h"
#include "../Utils/UndoManager.h"
#include "../Utils/UI/Theme.h"
#include "StyledComponents.h"
#include <optional>

class ParameterPanel : public juce::Component,
                       public juce::Button::Listener,
                       public juce::TextEditor::Listener
{
public:
    ParameterPanel();
    ~ParameterPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void buttonClicked(juce::Button* button) override;
    void textEditorReturnKeyPressed(juce::TextEditor& editor) override;
    void textEditorFocusLost(juce::TextEditor& editor) override;

    void setProject(Project* proj);
    void setUndoManager(PitchUndoManager* mgr) { juce::ignoreUnused(mgr); }
    void setSelectedNote(Note* note);
    void updateFromNote();
    void updateGlobalSliders();

    int getPreferredHeight() const { return 350; }

    std::function<void()> onParameterChanged;
    std::function<void()> onParameterEditFinished;
    std::function<void(int)> onScaleRootChanged;
    std::function<void(std::optional<int>)> onScaleRootPreviewChanged;
    std::function<void(ScaleMode)> onScaleModeChanged;
    std::function<void(std::optional<ScaleMode>)> onScaleModePreviewChanged;
    std::function<void(bool)> onShowScaleColorsChanged;
    std::function<void(bool)> onSnapToSemitonesChanged;
    std::function<void(int)> onPitchReferenceChanged;
    std::function<void(DoubleClickSnapMode)> onDoubleClickSnapModeChanged;

private:
    void setupTextButton(juce::TextButton& button);
    void showScaleModeMenu();
    void showScaleRootMenu();
    void showDoubleClickSnapMenu();
    void showReferenceMenu();
    void showDetectedScaleMenu();

    void setScaleRootInternal(int rootNote, bool notify);
    void setScaleModeInternal(ScaleMode mode, bool notify);
    void setShowScaleColorsInternal(bool enabled, bool notify);
    void setSnapToSemitonesInternal(bool enabled, bool notify);
    void setPitchReferenceInternal(int hz, bool notify);
    void setDoubleClickSnapModeInternal(DoubleClickSnapMode mode, bool notify);
    void previewScaleRoot(std::optional<int> rootNote);
    void previewScaleMode(std::optional<ScaleMode> mode);
    void refreshModeToggles();
    void refreshScaleControlEnabling();
    void applyReferenceEditorValue(bool notify);

    Project* project = nullptr;
    Note* selectedNote = nullptr;
    bool isUpdating = false;

    juce::Label pitchSectionLabel { {}, "Pitch" };
    juce::Rectangle<int> pitchCardBounds;

    StyledToggleButton chromaticToggle { "Chromatic" };
    StyledToggleButton scaleToggle { "Scale" };

    juce::Label referenceLabel { {}, "Reference (A4)" };
    juce::TextEditor referenceEditor;
    juce::TextButton referenceMenuButton { "<" };

    juce::Label scaleRootLabel { {}, "Root" };
    juce::TextButton scaleRootButton { "-" };
    juce::Label scaleModeLabel { {}, "Mode" };
    juce::TextButton scaleModeButton { "-" };
    juce::TextButton showDetectedScalesButton { "Show Detected Scales" };

    StyledToggleButton showScaleColorsToggle { "Show Scale Colors" };
    StyledToggleButton snapToSemitonesToggle { "Snap To Semitones" };

    juce::Label doubleClickSnapLabel { {}, "Double Click Snap" };
    juce::TextButton doubleClickSnapButton { "Pitch Center" };

    int selectedScaleRootNote = -1;
    ScaleMode selectedScaleMode = ScaleMode::None;
    ScaleMode lastNonChromaticMode = ScaleMode::Major;
    bool showScaleColors = true;
    bool snapToSemitones = false;
    int pitchReferenceHz = 440;
    DoubleClickSnapMode doubleClickSnapMode = DoubleClickSnapMode::PitchCenter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterPanel)
};
