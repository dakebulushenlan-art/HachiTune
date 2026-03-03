#pragma once

#include "InteractionHandler.h"
#include "../../../Utils/CenteredMelSpectrogram.h"

#include <memory>
#include <vector>

class Note;

/**
 * Handles note timing stretch interactions in Stretch edit mode.
 * Manages boundary detection, drag state, delta/voiced/mel resampling.
 */
class StretchHandler : public InteractionHandler {
public:
  explicit StretchHandler(PianoRollComponent &owner);

  bool mouseDown(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseDrag(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseUp(const juce::MouseEvent &e, float worldX,
               float worldY) override;
  void mouseMove(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  void draw(juce::Graphics &g) override;
  bool isActive() const override;
  void cancel() override;

  // Accessors for rendering
  int getHoveredBoundaryIndex() const { return hoveredStretchBoundaryIndex; }

  struct StretchBoundary {
    Note *left = nullptr;
    Note *right = nullptr;
    int frame = 0;
  };

  struct StretchDragState {
    bool active = false;
    bool changed = false;
    StretchBoundary boundary;
    int originalBoundary = 0;
    int originalLeftStart = 0;
    int originalLeftEnd = 0;
    int originalRightStart = 0;
    int originalRightEnd = 0;
    int rangeStartFull = 0;
    int rangeEndFull = 0;
    int rangeStart = 0;
    int rangeEnd = 0;
    int minFrame = 0;
    int maxFrame = 0;
    int currentBoundary = 0;
    std::vector<float> leftDelta;
    std::vector<float> rightDelta;
    std::vector<bool> leftVoiced;
    std::vector<bool> rightVoiced;
    std::vector<float> originalLeftClip;
    std::vector<float> originalRightClip;
    std::vector<std::vector<float>> originalMelRangeFull;
    std::vector<float> originalDeltaRangeFull;
    std::vector<bool> originalVoicedRangeFull;
  };

  // Public access for drawStretchGuides in renderer
  std::vector<StretchBoundary> collectStretchBoundaries() const;
  const StretchDragState &getDragState() const { return stretchDrag; }

private:
  int findStretchBoundaryIndex(float worldX, float tolerancePx) const;
  void startStretchDrag(const StretchBoundary &boundary);
  void updateStretchDrag(int targetFrame);
  void finishStretchDrag();
  void cancelStretchDrag();

  StretchDragState stretchDrag;
  int hoveredStretchBoundaryIndex = -1;
  static constexpr float stretchHandleHitPadding = 6.0f;
  static constexpr int minStretchNoteFrames = 3;
  std::unique_ptr<CenteredMelSpectrogram> centeredMelComputer;
};
