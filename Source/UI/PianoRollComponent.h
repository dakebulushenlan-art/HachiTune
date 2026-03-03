#pragma once

#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Constants.h"
#include "../Utils/BasePitchPreview.h"
#include "../Undo/UndoActions.h"
#include "Commands.h"
#include "PianoRoll/BoxSelector.h"
#include "PianoRoll/CoordinateMapper.h"
#include "PianoRoll/NoteSplitter.h"
#include "PianoRoll/PianoRollRenderer.h"
#include "PianoRoll/PitchEditor.h"
#include "PianoRoll/PitchToolController.h"
#include "PianoRoll/PitchToolHandles.h"
#include "PianoRoll/ScrollZoomController.h"

#include <memory>
#include <optional>

class PitchUndoManager;

// Interaction handler forward declarations
class InteractionHandler;
class LoopDragHandler;
class SelectHandler;
class DrawHandler;
class StretchHandler;
class SplitHandler;

/**
 * Edit mode for the piano roll.
 */
enum class EditMode {
  Select, // Normal selection and dragging
  Stretch, // Stretch note timing
  Draw,   // Pitch drawing mode
  Split   // Note splitting mode
};

/**
 * Piano roll component for displaying and editing notes.
 * Supports DPI-aware scaling for multi-monitor setups.
 */
class PianoRollComponent : public juce::Component,
                           public juce::ScrollBar::Listener,
                           public juce::KeyListener {
  // Interaction handlers need access to private members
  friend class LoopDragHandler;
  friend class SelectHandler;
  friend class DrawHandler;
  friend class StretchHandler;
  friend class SplitHandler;

public:
  using juce::Component::keyPressed;
  PianoRollComponent();
  ~PianoRollComponent() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent &e) override;
  void mouseDrag(const juce::MouseEvent &e) override;
  void mouseUp(const juce::MouseEvent &e) override;
  void mouseMove(const juce::MouseEvent &e) override;
  void mouseDoubleClick(const juce::MouseEvent &e) override;
  void mouseWheelMove(const juce::MouseEvent &e,
                      const juce::MouseWheelDetails &wheel) override;
  void mouseMagnify(const juce::MouseEvent &e, float scaleFactor) override;

  // Focus handling - re-grab focus when lost (important for plugin mode)
  void focusLost(FocusChangeType cause) override;
  void focusGained(FocusChangeType cause) override;

  // KeyListener
  bool keyPressed(const juce::KeyPress &key) override;
  bool keyPressed(const juce::KeyPress &key,
                  juce::Component *originatingComponent) override;

  // ScrollBar::Listener
  void scrollBarMoved(juce::ScrollBar *scrollBar,
                      double newRangeStart) override;

  // Project
  void setProject(Project *proj);
  Project *getProject() const { return project; }
  std::vector<Note *> getSelectedNotes() const;

  // Undo Manager
  void setUndoManager(PitchUndoManager *manager);
  PitchUndoManager *getUndoManager() const { return undoManager; }

  // Cursor
  void setCursorTime(double time);
  double getCursorTime() const { return cursorTime; }

  // Zoom with optional center point
  void setPixelsPerSecond(float pps, bool centerOnCursor = false);
  void setPixelsPerSemitone(float pps);
  float getPixelsPerSecond() const { return pixelsPerSecond; }
  float getPixelsPerSemitone() const { return pixelsPerSemitone; }

  // Scale-grid visualization
  void setScaleMode(ScaleMode mode);
  void setScaleRootNote(int noteInOctave);
  void setScaleRootPreview(std::optional<int> noteInOctave);
  void setScaleModePreview(std::optional<ScaleMode> mode);
  void setShowScaleColors(bool enabled);
  void setSnapToSemitoneDrag(bool enabled);
  void setPitchReferenceHz(int hz);
  void setDoubleClickSnapMode(DoubleClickSnapMode mode);
  void setTimelineDisplayMode(TimelineDisplayMode mode);
  void setTimelineBeatSignature(int numerator, int denominator);
  void setTimelineTempoBpm(double bpm);
  void setTimelineGridDivision(TimelineGridDivision division);
  void setTimelineSnapCycle(bool enabled);

  // Scroll
  void setScrollX(double x);
  double getScrollX() const { return scrollX; }
  void centerOnPitchRange(float minMidi, float maxMidi);
  int getVisibleContentWidth() const;
  int getVisibleContentHeight() const;

  // Edit mode
  void setEditMode(EditMode mode);
  EditMode getEditMode() const { return editMode; }

  // Cancel current drawing operation (used when undo is triggered during
  // drawing)
  void cancelDrawing();

  // View settings
  void setShowDeltaPitch(bool show) {
    showDeltaPitch = show;
    repaint();
  }
  void setShowBasePitch(bool show) {
    showBasePitch = show;
    repaint();
  }
  void setShowSomeSegmentsDebug(bool show) {
    showSomeSegmentsDebug = show;
    repaint();
  }
  void setShowSomeValuesDebug(bool show) {
    showSomeValuesDebug = show;
    repaint();
  }
  void setShowUvInterpolationDebug(bool show) {
    showUvInterpolationDebug = show;
    repaint();
  }
  void setShowActualF0Debug(bool show) {
    showActualF0Debug = show;
    repaint();
  }
  bool getShowDeltaPitch() const { return showDeltaPitch; }
  bool getShowBasePitch() const { return showBasePitch; }

  // Callbacks
  std::function<void(Note *)> onNoteSelected;
  std::function<void()> onPitchEdited;
  std::function<void()> onPitchEditFinished; // Called when dragging ends
  std::function<void(double)> onSeek;
  std::function<void(float)> onZoomChanged;
  std::function<void(double)> onScrollChanged;
  std::function<void(const LoopRange &)> onLoopRangeChanged;
  std::function<void(int, int)>
      onReinterpolateUV; // Called to re-infer UV regions (startFrame, endFrame)

private:
  enum class NoteRenderPass { Body, Overlay };

  void drawBackgroundWaveform(juce::Graphics &g,
                              const juce::Rectangle<int> &visibleArea);
  void drawGrid(juce::Graphics &g);
  void drawTimeline(juce::Graphics &g);
  void drawLoopTimeline(juce::Graphics &g);
  void drawNotes(juce::Graphics &g, NoteRenderPass pass);
  void drawPitchCurves(juce::Graphics &g);
  void drawCursor(juce::Graphics &g);
  void drawPianoKeys(juce::Graphics &g);
  void drawSelectionRect(juce::Graphics &g); // Box selection rectangle
  void drawLoopOverlay(juce::Graphics &g);
  void drawSomeSegmentDebugOverlay(juce::Graphics &g);
  void drawSomeValuesDebugOverlay(juce::Graphics &g);
  void drawStretchGuides(juce::Graphics &g);
  void updatePitchToolHandlesFromSelection();

  float midiToY(float midiNote) const;
  float yToMidi(float y) const;
  float timeToX(double time) const;
  double xToTime(float x) const;
  double getTimelineQuarterNoteSeconds() const;
  double getTimelineBeatSeconds() const;
  double getTimelineBarSeconds() const;
  double getTimelineGridSeconds() const;
  bool shouldSnapCycleToGrid() const;
  double snapTimeToTimelineGrid(double timeSeconds) const;

  Note *findNoteAt(float x, float y);
  void updateScrollBars();
  void updateBasePitchCacheIfNeeded();
  bool nudgeSelectedNotesBySemitones(int semitoneDelta);
  void reapplyBasePitchForNote(
      Note *note); // Recalculate F0 from base pitch + delta after undo/redo

  Project *project = nullptr;
  PitchUndoManager *undoManager = nullptr;

  // New modular components
  std::unique_ptr<CoordinateMapper> coordMapper;
  std::unique_ptr<PianoRollRenderer> renderer;
  std::unique_ptr<ScrollZoomController> scrollZoomController;
  std::unique_ptr<PitchEditor> pitchEditor;
  std::unique_ptr<BoxSelector> boxSelector;
  std::unique_ptr<NoteSplitter> noteSplitter;
  std::unique_ptr<PitchToolHandles> pitchToolHandles;
  std::unique_ptr<PitchToolController> pitchToolController;
  int hoveredPitchToolHandle = -1;

  float pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
  float pixelsPerSemitone = DEFAULT_PIXELS_PER_SEMITONE;

  double cursorTime = 0.0;
  double scrollX = 0.0;
  double scrollY = 0.0;

  // Layout constants
  static constexpr int pianoKeysWidth = 60;
  static constexpr int timelineHeight = 24;
  static constexpr int loopTimelineHeight = 16;
  static constexpr int headerHeight = timelineHeight + loopTimelineHeight;

  // Edit mode
  EditMode editMode = EditMode::Select;

  // View settings
  bool showDeltaPitch = true;
  bool showBasePitch = false;
  bool showSomeSegmentsDebug = false;
  bool showSomeValuesDebug = false;
  bool showUvInterpolationDebug = false;
  bool showActualF0Debug = false;
  bool showScaleColors = true;
  bool snapToSemitoneDrag = false;
  int pitchReferenceHz = 440;
  DoubleClickSnapMode doubleClickSnapMode = DoubleClickSnapMode::PitchCenter;
  TimelineDisplayMode timelineDisplayMode = TimelineDisplayMode::Beats;
  int timelineBeatNumerator = 4;
  int timelineBeatDenominator = 4;
  double timelineTempoBpm = 120.0;
  TimelineGridDivision timelineGridDivision = TimelineGridDivision::Quarter;
  bool timelineSnapCycle = false;
  ScaleMode selectedScaleMode = ScaleMode::None;
  int selectedScaleRootNote = -1;
  std::optional<int> previewScaleRootNote;
  std::optional<ScaleMode> previewScaleMode;

  // Interaction handlers (state machine pattern)
  std::unique_ptr<LoopDragHandler> loopDragHandler_;
  std::unique_ptr<SelectHandler> selectHandler_;
  std::unique_ptr<DrawHandler> drawHandler_;
  std::unique_ptr<StretchHandler> stretchHandler_;
  std::unique_ptr<SplitHandler> splitHandler_;
  InteractionHandler *currentHandler_ = nullptr;

  // Scrollbars
  juce::ScrollBar horizontalScrollBar{false};
  juce::ScrollBar verticalScrollBar{true};

  // Waveform cache for performance
  juce::Image waveformCache;
  double cachedScrollX = -1.0;
  float cachedPixelsPerSecond = -1.0f;
  int cachedWidth = 0;
  int cachedHeight = 0;

  // Base pitch curve cache for performance
  // Only recalculates when notes change, not on every repaint
  std::vector<float> cachedBasePitch;
  size_t cachedNoteCount = 0;
  int cachedTotalFrames = 0;
  bool cacheInvalidated = true; // Start invalidated, force first calculation

public:
  void invalidateBasePitchCache() {
    cacheInvalidated = true;
    cachedNoteCount = 0;
    cachedBasePitch.clear();
    cachedBasePitch.shrink_to_fit(); // Release memory
  }

private:
  // Optional: disable base pitch rendering for performance testing
  static constexpr bool ENABLE_BASE_PITCH_DEBUG =
      true; // Set to false to disable

  // Mouse drag throttling
  juce::int64 lastDragRepaintTime = 0;
  static constexpr juce::int64 minDragRepaintInterval = 16; // ~60fps max
  juce::int64 lastStretchPreviewTime = 0;
  static constexpr juce::int64 minStretchPreviewInterval = 120;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PianoRollComponent)
};
