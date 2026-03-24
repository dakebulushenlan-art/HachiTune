#pragma once

#include "InteractionHandler.h"
#include "../../../Undo/F0FrameEdit.h"
#include "../../../Utils/PitchCurveProcessor.h"
#include "../../../Utils/UI/DrawCurve.h"

#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * Handles pitch curve drawing interactions in Draw edit mode.
 * Manages freehand drawing state and applies pitch edits to F0 data.
 */
class DrawHandler : public InteractionHandler {
public:
  explicit DrawHandler(PianoRollComponent &owner);

  bool mouseDown(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseDrag(const juce::MouseEvent &e, float worldX,
                 float worldY) override;
  bool mouseUp(const juce::MouseEvent &e, float worldX,
               float worldY) override;
  bool isActive() const override;
  void cancel() override;

  // Accessors for rendering
  bool getIsDrawing() const { return isDrawing; }
  const std::deque<std::unique_ptr<DrawCurve>> &getDrawCurves() const {
    return drawCurves;
  }

private:
  void applyPitchDrawing(float x, float y);
  void commitPitchDrawing();
  void applyPitchPoint(int frameIndex, int midiCents);
  void startNewPitchCurve(int frameIndex, int midiCents);
  void snapshotNoteBeforeLocalClearIfNeeded(std::size_t noteIndex);

  bool isDrawing = false;
  bool isPendingDraw = false;
  float pendingDrawStartX = 0.0f;
  float pendingDrawStartY = 0.0f;
  std::vector<F0FrameEdit> drawingEdits;
  std::unordered_map<int, size_t> drawingEditIndexByFrame;
  int lastDrawFrame = -1;
  int lastDrawValueCents = 0;
  DrawCurve *activeDrawCurve = nullptr;
  std::deque<std::unique_ptr<DrawCurve>> drawCurves;

  /** Pre-clear state for undo: drawing clears per-note deltas before commit. */
  std::vector<NotePitchUndoSnapshot> drawSessionNoteSnapshots;
  std::unordered_set<std::size_t> drawSessionSnapshottedNoteIndices;
};
