#pragma once

#include "InteractionHandler.h"
#include "../../../Utils/CenteredMelSpectrogram.h"

#include <memory>
#include <vector>

class Note;
enum class StretchMode;

/**
 * Handles note timing stretch interactions in Stretch edit mode.
 * Supports two sub-modes:
 *   - Absorb: adjacent note absorbs length change (zero-sum boundary)
 *   - Ripple: subsequent notes shift to accommodate length change
 *
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
    Note *left = nullptr;  // Note to the left of this boundary (nullptr if free left edge)
    Note *right = nullptr; // Note to the right of this boundary (nullptr if free right edge)
    int frame = 0;
  };

  /**
   * State captured at drag start and updated during drag.
   * Holds all original data needed for resampling and undo.
   */
  struct StretchDragState {
    bool active = false;
    bool changed = false;
    StretchMode mode{}; // Effective mode for this drag (locked at start)

    StretchBoundary boundary;
    int originalBoundary = 0;

    // Original frame positions of the two notes involved
    int originalLeftStart = 0;
    int originalLeftEnd = 0;
    int originalRightStart = 0;
    int originalRightEnd = 0;

    // Drag limits
    int minFrame = 0;
    int maxFrame = 0;
    int currentBoundary = 0;

    // Full affected range for undo/redo
    int rangeStartFull = 0;
    int rangeEndFull = 0;

    // Saved per-note pitch/voiced data (original, before stretch)
    std::vector<float> leftDelta;
    std::vector<float> rightDelta;
    std::vector<bool> leftVoiced;
    std::vector<bool> rightVoiced;

    // Saved clip waveforms (original)
    std::vector<float> originalLeftClip;
    std::vector<float> originalRightClip;

    // Saved full range data for undo
    std::vector<std::vector<float>> originalMelRangeFull;
    std::vector<float> originalDeltaRangeFull;
    std::vector<bool> originalVoicedRangeFull;

    // --- Ripple mode additional state ---
    // All notes after the boundary, ordered by startFrame
    std::vector<Note *> rippleNotes;
    // Original positions of ripple notes (for undo)
    std::vector<int> originalRippleStarts;
    std::vector<int> originalRippleEnds;
    // Per-note voicedMask for each ripple note (for restoring after shift)
    std::vector<std::vector<bool>> rippleVoicedData;
    // Per-note mel for each ripple note (for restoring after shift)
    std::vector<std::vector<std::vector<float>>> rippleMelData;
    // The frame delta applied to ripple notes
    int rippleDelta = 0;
  };

  // Public access for drawStretchGuides in renderer
  std::vector<StretchBoundary> collectStretchBoundaries() const;
  const StretchDragState &getDragState() const { return stretchDrag; }

private:
  int findStretchBoundaryIndex(float worldX, float tolerancePx) const;
  void startStretchDrag(const StretchBoundary &boundary,
                        StretchMode effectiveMode);
  void updateStretchDrag(int targetFrame);
  void finishStretchDrag();
  void cancelStretchDrag();

  /** Collect notes that come after the given frame, sorted by startFrame. */
  std::vector<Note *> collectRippleNotes(int afterFrame) const;

  /** Shift ripple notes by delta frames (positive = later, negative = earlier). */
  void applyRippleShift(int deltaFrames);

  /** Restore ripple notes to their original positions. */
  void restoreRippleNotes();

  StretchDragState stretchDrag;
  int hoveredStretchBoundaryIndex = -1;
  static constexpr float stretchHandleHitPadding = 6.0f;
  static constexpr int minStretchNoteFrames = 3;
  std::unique_ptr<CenteredMelSpectrogram> centeredMelComputer;
};
