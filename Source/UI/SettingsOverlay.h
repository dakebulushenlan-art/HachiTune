#pragma once

#include "SettingsComponent.h"

/**
 * Settings overlay panel (in-window modal).
 */
class SettingsOverlay : public juce::Component {
public:
  SettingsOverlay(SettingsManager *settingsManager,
                  juce::AudioDeviceManager *audioDeviceManager = nullptr);
  ~SettingsOverlay() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;
  bool keyPressed(const juce::KeyPress &key) override;

  SettingsComponent *getSettingsComponent() { return settingsComponent.get(); }

  std::function<void()> onClose;

private:
  void closeIfPossible();

  std::unique_ptr<SettingsComponent> settingsComponent;
  juce::TextButton closeButton{"X"};
  juce::Rectangle<int> contentBounds;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

/**
 * Settings dialog window.
 */
class SettingsDialog : public juce::DialogWindow {
public:
  SettingsDialog(SettingsManager *settingsManager,
                 juce::AudioDeviceManager *audioDeviceManager = nullptr);
  ~SettingsDialog() override = default;

  void closeButtonPressed() override;
  void paint(juce::Graphics &g) override;

  SettingsComponent *getSettingsComponent() { return settingsComponent.get(); }

private:
  std::unique_ptr<SettingsComponent> settingsComponent;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};
