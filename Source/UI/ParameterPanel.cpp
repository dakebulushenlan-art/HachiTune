#include "ParameterPanel.h"
#include "../Utils/Localization.h"
#include "../Utils/ScaleUtils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace
{
struct ScaleModeOption
{
    ScaleMode mode;
    const char* label;
};

struct DoubleClickSnapOption
{
    DoubleClickSnapMode mode;
    const char* label;
};

struct DetectedScaleCandidate
{
    int root = 0;
    ScaleMode mode = ScaleMode::Major;
    float score = 0.0f;
};

constexpr std::array<ScaleModeOption, 8> kScaleModeOptions {{
    { ScaleMode::None, "-" },
    { ScaleMode::Major, "Major" },
    { ScaleMode::Minor, "Minor" },
    { ScaleMode::Dorian, "Dorian" },
    { ScaleMode::Phrygian, "Phrygian" },
    { ScaleMode::Lydian, "Lydian" },
    { ScaleMode::Mixolydian, "Mixolydian" },
    { ScaleMode::Locrian, "Locrian" }
}};

constexpr std::array<DoubleClickSnapOption, 3> kDoubleClickSnapOptions {{
    { DoubleClickSnapMode::PitchCenter, "Pitch Center" },
    { DoubleClickSnapMode::NearestSemitone, "Nearest Semitone" },
    { DoubleClickSnapMode::NearestScale, "Nearest Scale" }
}};

constexpr std::array<const char*, 12> kScaleRootLabels {{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
}};

constexpr std::array<int, 5> kReferencePresets {{
    415, 432, 435, 440, 442
}};

constexpr std::array<ScaleMode, 7> kDetectedScaleModes {{
    ScaleMode::Major,
    ScaleMode::Minor,
    ScaleMode::Dorian,
    ScaleMode::Phrygian,
    ScaleMode::Lydian,
    ScaleMode::Mixolydian,
    ScaleMode::Locrian
}};

juce::String getScaleModeLabel(ScaleMode mode)
{
    for (const auto& option : kScaleModeOptions)
        if (option.mode == mode)
            return option.label;
    return "-";
}

juce::String getScaleRootLabel(int rootNote)
{
    if (rootNote < 0 || rootNote > 11)
        return "-";
    return kScaleRootLabels[static_cast<size_t>(rootNote)];
}

juce::String getDoubleClickSnapLabel(DoubleClickSnapMode mode)
{
    for (const auto& option : kDoubleClickSnapOptions)
        if (option.mode == mode)
            return option.label;
    return kDoubleClickSnapOptions[0].label;
}

juce::String getDetectedScaleLabel(const DetectedScaleCandidate& candidate)
{
    const int percent = juce::roundToInt(candidate.score * 100.0f);
    return juce::String(getScaleRootLabel(candidate.root))
        + " "
        + getScaleModeLabel(candidate.mode)
        + "  ("
        + juce::String(percent)
        + "%)";
}

std::vector<DetectedScaleCandidate> detectScales(const Project* project)
{
    std::vector<DetectedScaleCandidate> result;
    if (project == nullptr)
        return result;

    std::array<double, 12> pitchClassWeights {};
    double totalWeight = 0.0;

    for (const auto& note : project->getNotes())
    {
        if (note.isRest())
            continue;

        const int pitchClass =
            (static_cast<int>(std::lround(note.getAdjustedMidiNote())) % 12 + 12) % 12;
        const double weight = static_cast<double>(juce::jmax(1, note.getDurationFrames()));
        pitchClassWeights[static_cast<size_t>(pitchClass)] += weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0)
        return result;

    result.reserve(kDetectedScaleModes.size() * 12);
    for (int root = 0; root < 12; ++root)
    {
        for (const auto mode : kDetectedScaleModes)
        {
            double inScaleWeight = 0.0;
            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                if (ScaleUtils::isPitchClassInScale(mode, pitchClass, root))
                    inScaleWeight += pitchClassWeights[static_cast<size_t>(pitchClass)];
            }

            const double rootBoost = pitchClassWeights[static_cast<size_t>(root)] * 0.10;
            result.push_back({
                root,
                mode,
                static_cast<float>((inScaleWeight + rootBoost) / totalWeight)
            });
        }
    }

    std::sort(result.begin(), result.end(),
              [](const DetectedScaleCandidate& a, const DetectedScaleCandidate& b)
              {
                  if (std::abs(a.score - b.score) > 1.0e-6f)
                      return a.score > b.score;
                  if (a.root != b.root)
                      return a.root < b.root;
                  return static_cast<int>(a.mode) < static_cast<int>(b.mode);
              });

    if (result.size() > 8)
        result.resize(8);
    return result;
}

class PitchPopupLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PitchPopupLookAndFeel()
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::PopupMenu::textColourId, APP_COLOR_TEXT_PRIMARY);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, APP_COLOR_PRIMARY.withAlpha(0.25f));
        setColour(juce::PopupMenu::highlightedTextColourId, APP_COLOR_TEXT_PRIMARY);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        const auto area = juce::Rectangle<float>(
            1.0f, 1.0f, static_cast<float>(width - 2), static_cast<float>(height - 2));
        g.setColour(APP_COLOR_SURFACE_RAISED.withMultipliedBrightness(0.96f));
        g.fillRoundedRectangle(area, 9.0f);
        g.setColour(APP_COLOR_BORDER.withAlpha(0.9f));
        g.drawRoundedRectangle(area, 9.0f, 1.0f);
    }
};

PitchPopupLookAndFeel& getPitchPopupLookAndFeel()
{
    static PitchPopupLookAndFeel lookAndFeel;
    return lookAndFeel;
}

class HoverMenuItemComponent final : public juce::PopupMenu::CustomComponent
{
public:
    HoverMenuItemComponent(juce::String text, bool selected, std::function<void()> hoverCallback = {})
        : juce::PopupMenu::CustomComponent(true),
          itemText(std::move(text)),
          isSelected(selected),
          onHover(std::move(hoverCallback))
    {
    }

    void getIdealSize(int& idealWidth, int& idealHeight) override
    {
        idealWidth = 200;
        idealHeight = 26;
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        if (isItemHighlighted())
        {
            g.setColour(APP_COLOR_PRIMARY.withAlpha(0.25f));
            g.fillRoundedRectangle(area.reduced(2.0f, 1.0f), 5.0f);
        }

        if (isSelected)
        {
            g.setColour(APP_COLOR_PRIMARY.withAlpha(0.95f));
            g.fillEllipse(8.0f, area.getCentreY() - 3.5f, 7.0f, 7.0f);
        }

        g.setColour(APP_COLOR_TEXT_PRIMARY);
        g.setFont(AppFont::getFont(14.0f));
        g.drawText(itemText, getLocalBounds().withTrimmedLeft(22),
                   juce::Justification::centredLeft, true);
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        juce::PopupMenu::CustomComponent::mouseEnter(e);
        if (onHover)
            onHover();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        juce::PopupMenu::CustomComponent::mouseMove(e);
        if (onHover)
            onHover();
    }

private:
    juce::String itemText;
    bool isSelected = false;
    std::function<void()> onHover;
};
}

ParameterPanel::ParameterPanel()
{
    addAndMakeVisible(pitchSectionLabel);
    pitchSectionLabel.setColour(juce::Label::textColourId, APP_COLOR_PRIMARY);
    pitchSectionLabel.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));

    for (auto* toggle : { &chromaticToggle, &scaleToggle, &showScaleColorsToggle, &snapToSemitonesToggle })
    {
        addAndMakeVisible(toggle);
        toggle->setClickingTogglesState(true);
        toggle->addListener(this);
    }

    for (auto* label : { &referenceLabel, &scaleRootLabel, &scaleModeLabel, &doubleClickSnapLabel })
    {
        addAndMakeVisible(label);
        label->setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    }

    for (auto* button : { &referenceMenuButton, &scaleRootButton, &scaleModeButton,
                          &showDetectedScalesButton, &doubleClickSnapButton })
    {
        setupTextButton(*button);
        addAndMakeVisible(button);
        button->addListener(this);
    }

    referenceEditor.setInputRestrictions(3, "0123456789");
    referenceEditor.setText(juce::String(pitchReferenceHz), juce::dontSendNotification);
    referenceEditor.setJustification(juce::Justification::centred);
    referenceEditor.setColour(juce::TextEditor::backgroundColourId, APP_COLOR_SURFACE_ALT);
    referenceEditor.setColour(juce::TextEditor::textColourId, APP_COLOR_TEXT_PRIMARY);
    referenceEditor.setColour(juce::TextEditor::outlineColourId, APP_COLOR_BORDER.withAlpha(0.8f));
    referenceEditor.setColour(juce::TextEditor::focusedOutlineColourId, APP_COLOR_PRIMARY.withAlpha(0.85f));
    referenceEditor.addListener(this);
    addAndMakeVisible(referenceEditor);

    scaleRootButton.setButtonText(getScaleRootLabel(selectedScaleRootNote));
    scaleModeButton.setButtonText(getScaleModeLabel(selectedScaleMode));
    doubleClickSnapButton.setButtonText(getDoubleClickSnapLabel(doubleClickSnapMode));

    refreshModeToggles();
    refreshScaleControlEnabling();
}

ParameterPanel::~ParameterPanel()
{
}

void ParameterPanel::setupTextButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, APP_COLOR_SURFACE_ALT);
    button.setColour(juce::TextButton::buttonOnColourId, APP_COLOR_SURFACE_RAISED);
    button.setColour(juce::TextButton::textColourOffId, APP_COLOR_TEXT_PRIMARY);
    button.setColour(juce::TextButton::textColourOnId, APP_COLOR_TEXT_PRIMARY);
}

void ParameterPanel::paint(juce::Graphics& g)
{
    if (pitchCardBounds.isEmpty())
        return;

    const float radius = 10.0f;
    g.setColour(APP_COLOR_SURFACE_RAISED);
    g.fillRoundedRectangle(pitchCardBounds.toFloat(), radius);

    juce::Path borderPath;
    borderPath.addRoundedRectangle(pitchCardBounds.toFloat().reduced(0.5f), radius);
    juce::ColourGradient borderGradient(
        APP_COLOR_BORDER_HIGHLIGHT, pitchCardBounds.getX(), pitchCardBounds.getY(),
        APP_COLOR_BORDER.darker(0.3f), pitchCardBounds.getRight(), pitchCardBounds.getBottom(), false);
    g.setGradientFill(borderGradient);
    g.strokePath(borderPath, juce::PathStrokeType(1.1f));

    g.setColour(APP_COLOR_BORDER_SUBTLE.withAlpha(0.45f));
    g.drawRoundedRectangle(pitchCardBounds.toFloat().reduced(1.2f), radius - 1.0f, 0.6f);
}

void ParameterPanel::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    constexpr int pitchCardHeight = 296;
    pitchCardBounds = bounds.removeFromTop(juce::jmin(bounds.getHeight(), pitchCardHeight));
    auto area = pitchCardBounds.reduced(10);
    constexpr int rowGap = 6;
    constexpr int columnGap = 8;

    pitchSectionLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(rowGap);

    auto modeRow = area.removeFromTop(24);
    auto chromaticArea = modeRow.removeFromLeft((modeRow.getWidth() - columnGap) / 2);
    modeRow.removeFromLeft(columnGap);
    chromaticToggle.setBounds(chromaticArea);
    scaleToggle.setBounds(modeRow);

    area.removeFromTop(rowGap + 2);

    auto referenceRow = area.removeFromTop(30);
    referenceLabel.setBounds(referenceRow.removeFromLeft(120));
    auto refMenuArea = referenceRow.removeFromRight(34);
    referenceMenuButton.setBounds(refMenuArea);
    referenceEditor.setBounds(referenceRow.reduced(0, 2));

    area.removeFromTop(rowGap);

    auto scaleLabelRow = area.removeFromTop(16);
    auto rootLabelArea = scaleLabelRow.removeFromLeft((scaleLabelRow.getWidth() - columnGap) / 2);
    scaleLabelRow.removeFromLeft(columnGap);
    scaleRootLabel.setBounds(rootLabelArea);
    scaleModeLabel.setBounds(scaleLabelRow);

    auto scaleValueRow = area.removeFromTop(28);
    auto rootValueArea = scaleValueRow.removeFromLeft((scaleValueRow.getWidth() - columnGap) / 2);
    scaleValueRow.removeFromLeft(columnGap);
    scaleRootButton.setBounds(rootValueArea);
    scaleModeButton.setBounds(scaleValueRow);

    area.removeFromTop(rowGap + 2);
    showDetectedScalesButton.setBounds(area.removeFromTop(30));

    area.removeFromTop(rowGap + 2);
    showScaleColorsToggle.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);
    snapToSemitonesToggle.setBounds(area.removeFromTop(24));

    area.removeFromTop(rowGap + 2);
    auto doubleClickRow = area.removeFromTop(30);
    doubleClickSnapLabel.setBounds(doubleClickRow.removeFromLeft(130));
    doubleClickSnapButton.setBounds(doubleClickRow);
}

void ParameterPanel::buttonClicked(juce::Button* button)
{
    if (isUpdating)
        return;

    if (button == &scaleRootButton)
    {
        showScaleRootMenu();
        return;
    }
    if (button == &scaleModeButton)
    {
        showScaleModeMenu();
        return;
    }
    if (button == &referenceMenuButton)
    {
        showReferenceMenu();
        return;
    }
    if (button == &doubleClickSnapButton)
    {
        showDoubleClickSnapMenu();
        return;
    }
    if (button == &showDetectedScalesButton)
    {
        showDetectedScaleMenu();
        return;
    }
    if (button == &showScaleColorsToggle)
    {
        setShowScaleColorsInternal(showScaleColorsToggle.getToggleState(), true);
        if (onParameterChanged)
            onParameterChanged();
        return;
    }
    if (button == &snapToSemitonesToggle)
    {
        setSnapToSemitonesInternal(snapToSemitonesToggle.getToggleState(), true);
        if (onParameterChanged)
            onParameterChanged();
        return;
    }
    if (button == &chromaticToggle)
    {
        if (chromaticToggle.getToggleState())
        {
            setScaleModeInternal(ScaleMode::Chromatic, true);
        }
        else if (!scaleToggle.getToggleState())
        {
            setScaleModeInternal(ScaleMode::None, true);
        }

        if (onParameterChanged)
            onParameterChanged();
        return;
    }
    if (button == &scaleToggle)
    {
        if (scaleToggle.getToggleState())
        {
            ScaleMode target = selectedScaleMode;
            if (target == ScaleMode::None || target == ScaleMode::Chromatic)
                target = (lastNonChromaticMode == ScaleMode::None ||
                          lastNonChromaticMode == ScaleMode::Chromatic)
                             ? ScaleMode::Major
                             : lastNonChromaticMode;
            setScaleModeInternal(target, true);
        }
        else if (!chromaticToggle.getToggleState())
        {
            setScaleModeInternal(ScaleMode::None, true);
        }

        if (onParameterChanged)
            onParameterChanged();
    }
}

void ParameterPanel::textEditorReturnKeyPressed(juce::TextEditor& editor)
{
    if (&editor == &referenceEditor)
        applyReferenceEditorValue(true);
}

void ParameterPanel::textEditorFocusLost(juce::TextEditor& editor)
{
    if (&editor == &referenceEditor)
        applyReferenceEditorValue(true);
}

void ParameterPanel::setProject(Project* proj)
{
    project = proj;
    updateGlobalSliders();
}

void ParameterPanel::setSelectedNote(Note* note)
{
    selectedNote = note;
    updateFromNote();
}

void ParameterPanel::updateFromNote()
{
    juce::ignoreUnused(selectedNote);
}

void ParameterPanel::updateGlobalSliders()
{
    isUpdating = true;

    if (project != nullptr)
    {
        selectedScaleRootNote = project->getScaleRootNote();
        selectedScaleMode = project->getScaleMode();
        if (selectedScaleMode != ScaleMode::None &&
            selectedScaleMode != ScaleMode::Chromatic)
            lastNonChromaticMode = selectedScaleMode;
        showScaleColors = project->getShowScaleColors();
        snapToSemitones = project->getSnapToSemitones();
        pitchReferenceHz = project->getPitchReferenceHz();
        doubleClickSnapMode = project->getDoubleClickSnapMode();
    }
    else
    {
        selectedScaleRootNote = -1;
        selectedScaleMode = ScaleMode::None;
        showScaleColors = true;
        snapToSemitones = false;
        pitchReferenceHz = 440;
        doubleClickSnapMode = DoubleClickSnapMode::PitchCenter;
    }

    scaleRootButton.setButtonText(getScaleRootLabel(selectedScaleRootNote));
    scaleModeButton.setButtonText(
        selectedScaleMode == ScaleMode::Chromatic ? "-" : getScaleModeLabel(selectedScaleMode));
    referenceEditor.setText(juce::String(pitchReferenceHz), juce::dontSendNotification);
    showScaleColorsToggle.setToggleState(showScaleColors, juce::dontSendNotification);
    snapToSemitonesToggle.setToggleState(snapToSemitones, juce::dontSendNotification);
    doubleClickSnapButton.setButtonText(getDoubleClickSnapLabel(doubleClickSnapMode));

    refreshModeToggles();
    refreshScaleControlEnabling();
    isUpdating = false;
}

void ParameterPanel::refreshModeToggles()
{
    const bool isChromatic = selectedScaleMode == ScaleMode::Chromatic;
    const bool isScale =
        selectedScaleMode != ScaleMode::None && selectedScaleMode != ScaleMode::Chromatic;

    chromaticToggle.setToggleState(isChromatic, juce::dontSendNotification);
    scaleToggle.setToggleState(isScale, juce::dontSendNotification);
}

void ParameterPanel::refreshScaleControlEnabling()
{
    const bool scaleEnabled = scaleToggle.getToggleState();
    scaleRootButton.setEnabled(scaleEnabled);
    scaleModeButton.setEnabled(scaleEnabled);
    scaleRootLabel.setEnabled(scaleEnabled);
    scaleModeLabel.setEnabled(scaleEnabled);

    bool hasAnyNote = false;
    if (project != nullptr)
    {
        for (const auto& note : project->getNotes())
        {
            if (!note.isRest())
            {
                hasAnyNote = true;
                break;
            }
        }
    }
    showDetectedScalesButton.setEnabled(hasAnyNote);
}

void ParameterPanel::setScaleRootInternal(int rootNote, bool notify)
{
    const int normalized = juce::jlimit(-1, 11, rootNote);
    const bool changed = selectedScaleRootNote != normalized;
    if (!changed && !notify)
        return;

    selectedScaleRootNote = normalized;
    scaleRootButton.setButtonText(getScaleRootLabel(selectedScaleRootNote));

    if (project != nullptr && changed)
        project->setScaleRootNote(normalized);

    if (notify && changed && onScaleRootChanged)
        onScaleRootChanged(normalized);
}

void ParameterPanel::setScaleModeInternal(ScaleMode mode, bool notify)
{
    const bool changed = selectedScaleMode != mode;
    if (!changed && !notify)
        return;

    selectedScaleMode = mode;
    if (mode != ScaleMode::None && mode != ScaleMode::Chromatic)
        lastNonChromaticMode = mode;

    scaleModeButton.setButtonText(
        mode == ScaleMode::Chromatic ? "-" : getScaleModeLabel(mode));
    refreshModeToggles();
    refreshScaleControlEnabling();

    if (project != nullptr && changed)
        project->setScaleMode(mode);

    if (notify && changed && onScaleModeChanged)
        onScaleModeChanged(mode);
}

void ParameterPanel::setShowScaleColorsInternal(bool enabled, bool notify)
{
    const bool changed = showScaleColors != enabled;
    showScaleColors = enabled;
    showScaleColorsToggle.setToggleState(enabled, juce::dontSendNotification);

    if (project != nullptr && changed)
        project->setShowScaleColors(enabled);

    if (notify && changed && onShowScaleColorsChanged)
        onShowScaleColorsChanged(enabled);
}

void ParameterPanel::setSnapToSemitonesInternal(bool enabled, bool notify)
{
    const bool changed = snapToSemitones != enabled;
    snapToSemitones = enabled;
    snapToSemitonesToggle.setToggleState(enabled, juce::dontSendNotification);

    if (project != nullptr && changed)
        project->setSnapToSemitones(enabled);

    if (notify && changed && onSnapToSemitonesChanged)
        onSnapToSemitonesChanged(enabled);
}

void ParameterPanel::setPitchReferenceInternal(int hz, bool notify)
{
    const int normalized = juce::jlimit(380, 480, hz);
    const bool changed = pitchReferenceHz != normalized;
    pitchReferenceHz = normalized;
    referenceEditor.setText(juce::String(normalized), juce::dontSendNotification);

    if (project != nullptr && changed)
        project->setPitchReferenceHz(normalized);

    if (notify && changed && onPitchReferenceChanged)
        onPitchReferenceChanged(normalized);
}

void ParameterPanel::setDoubleClickSnapModeInternal(DoubleClickSnapMode mode, bool notify)
{
    const bool changed = doubleClickSnapMode != mode;
    doubleClickSnapMode = mode;
    doubleClickSnapButton.setButtonText(getDoubleClickSnapLabel(mode));

    if (project != nullptr && changed)
        project->setDoubleClickSnapMode(mode);

    if (notify && changed && onDoubleClickSnapModeChanged)
        onDoubleClickSnapModeChanged(mode);
}

void ParameterPanel::previewScaleRoot(std::optional<int> rootNote)
{
    if (onScaleRootPreviewChanged)
        onScaleRootPreviewChanged(rootNote);
}

void ParameterPanel::previewScaleMode(std::optional<ScaleMode> mode)
{
    if (onScaleModePreviewChanged)
        onScaleModePreviewChanged(mode);
}

void ParameterPanel::applyReferenceEditorValue(bool notify)
{
    const int parsed = referenceEditor.getText().getIntValue();
    setPitchReferenceInternal(parsed <= 0 ? pitchReferenceHz : parsed, notify);
}

void ParameterPanel::showScaleRootMenu()
{
    constexpr int menuBaseId = 7000;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getPitchPopupLookAndFeel());

    auto hoverNoneCallback = [safeThis = juce::Component::SafePointer<ParameterPanel>(this)]()
    {
        if (safeThis != nullptr)
            safeThis->previewScaleRoot(-1);
    };
    menu.addCustomItem(menuBaseId,
                       std::make_unique<HoverMenuItemComponent>(
                           "-", selectedScaleRootNote < 0, std::move(hoverNoneCallback)),
                       nullptr, "-");

    for (int i = 0; i < static_cast<int>(kScaleRootLabels.size()); ++i)
    {
        const juce::String label = kScaleRootLabels[static_cast<size_t>(i)];
        auto hoverCallback =
            [safeThis = juce::Component::SafePointer<ParameterPanel>(this), i]()
            {
                if (safeThis != nullptr)
                    safeThis->previewScaleRoot(i);
            };

        menu.addCustomItem(menuBaseId + i + 1,
                           std::make_unique<HoverMenuItemComponent>(
                               label, i == selectedScaleRootNote, std::move(hoverCallback)),
                           nullptr, label);
    }

    auto options = juce::PopupMenu::Options()
        .withTargetComponent(&scaleRootButton)
        .withParentComponent(this)
        .withMinimumWidth(juce::jmax(200, scaleRootButton.getWidth()));

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<ParameterPanel>(this)](int result)
                       {
                           if (safeThis == nullptr)
                               return;

                           safeThis->previewScaleRoot(std::nullopt);
                           if (result == 0)
                               return;

                           constexpr int menuBaseId = 7000;
                           const int idx = result - menuBaseId;
                           if (idx == 0)
                               safeThis->setScaleRootInternal(-1, true);
                           else
                               safeThis->setScaleRootInternal(idx - 1, true);
                       });
}

void ParameterPanel::showScaleModeMenu()
{
    constexpr int menuBaseId = 7100;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getPitchPopupLookAndFeel());

    for (size_t i = 0; i < kScaleModeOptions.size(); ++i)
    {
        const auto mode = kScaleModeOptions[i].mode;
        const juce::String label = kScaleModeOptions[i].label;
        auto hoverCallback =
            [safeThis = juce::Component::SafePointer<ParameterPanel>(this), mode]()
            {
                if (safeThis != nullptr)
                    safeThis->previewScaleMode(mode);
            };

        menu.addCustomItem(menuBaseId + static_cast<int>(i),
                           std::make_unique<HoverMenuItemComponent>(
                               label, selectedScaleMode == mode, std::move(hoverCallback)),
                           nullptr, label);
    }

    auto options = juce::PopupMenu::Options()
        .withTargetComponent(&scaleModeButton)
        .withParentComponent(this)
        .withMinimumWidth(juce::jmax(200, scaleModeButton.getWidth()));

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<ParameterPanel>(this)](int result)
                       {
                           if (safeThis == nullptr)
                               return;

                           safeThis->previewScaleMode(std::nullopt);
                           if (result == 0)
                               return;

                           constexpr int menuBaseId = 7100;
                           const int idx = result - menuBaseId;
                           if (idx >= 0 && idx < static_cast<int>(kScaleModeOptions.size()))
                               safeThis->setScaleModeInternal(
                                   kScaleModeOptions[static_cast<size_t>(idx)].mode, true);
                       });
}

void ParameterPanel::showDoubleClickSnapMenu()
{
    constexpr int menuBaseId = 7200;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getPitchPopupLookAndFeel());

    for (size_t i = 0; i < kDoubleClickSnapOptions.size(); ++i)
    {
        const auto option = kDoubleClickSnapOptions[i];
        menu.addCustomItem(menuBaseId + static_cast<int>(i),
                           std::make_unique<HoverMenuItemComponent>(
                               option.label, doubleClickSnapMode == option.mode),
                           nullptr, option.label);
    }

    auto options = juce::PopupMenu::Options()
        .withTargetComponent(&doubleClickSnapButton)
        .withParentComponent(this)
        .withMinimumWidth(juce::jmax(220, doubleClickSnapButton.getWidth()));

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<ParameterPanel>(this)](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           constexpr int menuBaseId = 7200;
                           const int idx = result - menuBaseId;
                           if (idx >= 0 && idx < static_cast<int>(kDoubleClickSnapOptions.size()))
                               safeThis->setDoubleClickSnapModeInternal(
                                   kDoubleClickSnapOptions[static_cast<size_t>(idx)].mode, true);
                       });
}

void ParameterPanel::showReferenceMenu()
{
    constexpr int menuBaseId = 7300;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getPitchPopupLookAndFeel());

    for (size_t i = 0; i < kReferencePresets.size(); ++i)
    {
        const int preset = kReferencePresets[i];
        menu.addItem(menuBaseId + static_cast<int>(i), juce::String(preset), true,
                     preset == pitchReferenceHz);
    }

    auto options = juce::PopupMenu::Options()
        .withTargetComponent(&referenceMenuButton)
        .withParentComponent(this)
        .withMinimumWidth(120);

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<ParameterPanel>(this)](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           constexpr int menuBaseId = 7300;
                           const int idx = result - menuBaseId;
                           if (idx >= 0 && idx < static_cast<int>(kReferencePresets.size()))
                               safeThis->setPitchReferenceInternal(
                                   kReferencePresets[static_cast<size_t>(idx)], true);
                       });
}

void ParameterPanel::showDetectedScaleMenu()
{
    constexpr int menuBaseId = 7400;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getPitchPopupLookAndFeel());

    const auto candidates = detectScales(project);
    if (candidates.empty())
    {
        menu.addItem(1, "No scale detected", false, false);
    }
    else
    {
        for (size_t i = 0; i < candidates.size(); ++i)
            menu.addItem(menuBaseId + static_cast<int>(i), getDetectedScaleLabel(candidates[i]));
    }

    auto options = juce::PopupMenu::Options()
        .withTargetComponent(&showDetectedScalesButton)
        .withParentComponent(this)
        .withMinimumWidth(240);

    menu.showMenuAsync(options,
                       [safeThis = juce::Component::SafePointer<ParameterPanel>(this),
                        candidates](int result)
                       {
                           if (safeThis == nullptr || result == 0)
                               return;

                           constexpr int menuBaseId = 7400;
                           const int idx = result - menuBaseId;
                           if (idx >= 0 && idx < static_cast<int>(candidates.size()))
                           {
                               const auto& candidate = candidates[static_cast<size_t>(idx)];
                               safeThis->setScaleRootInternal(candidate.root, true);
                               safeThis->setScaleModeInternal(candidate.mode, true);
                           }
                       });
}
