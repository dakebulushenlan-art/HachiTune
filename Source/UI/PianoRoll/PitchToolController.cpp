#include "PitchToolController.h"

#include <algorithm>

PitchToolController::PitchToolController()
{
}

bool PitchToolController::mouseDown(const juce::MouseEvent& e,
                                    const PitchToolHandles& handles,
                                    const std::vector<Note*>& selectedNotes,
                                    const CoordinateMapper& mapper)
{
  juce::ignoreUnused(mapper);

  DBG("PitchTool mouseDown: pos=(" << e.position.x << ", " << e.position.y 
      << "), selectedNotes=" << selectedNotes.size() 
      << ", totalHandles=" << handles.getHandles().size());

  const int hitIndex = handles.hitTest(e.position.x, e.position.y);
  
  DBG("  hitTest result: hitIndex=" << hitIndex);
  
  if (hitIndex < 0 || selectedNotes.empty()) {
    DBG("  → REJECTED (hitIndex<0 or no notes)");
    return false;
  }

  activeHandleType = handles.getHandle(hitIndex).type;
  
  DBG("  → HIT! handleType=" << static_cast<int>(activeHandleType));
  
  affectedNotes = selectedNotes;
  originalPitchCurves.clear();
  originalPitchCurves.reserve(affectedNotes.size());
  for (auto* note : affectedNotes)
  {
    if (note)
      originalPitchCurves.push_back(note->getDeltaPitch());
    else
      originalPitchCurves.emplace_back();
  }

  dragStartPos = e.position;
  dragging = true;
  return true;
}

bool PitchToolController::mouseDrag(const juce::MouseEvent& e,
                                    std::vector<Note*>& selectedNotes,
                                    const CoordinateMapper& mapper)
{
  juce::ignoreUnused(selectedNotes);

  if (!dragging)
    return false;

  const float deltaX = e.position.x - dragStartPos.x;
  const float deltaY = e.position.y - dragStartPos.y;

  applyOperation(affectedNotes, activeHandleType, deltaX, deltaY, mapper);
  return true;
}

bool PitchToolController::mouseUp(const juce::MouseEvent& e,
                                  PitchUndoManager* undoManager,
                                  std::function<void(int, int)> onRangeChanged)
{
  juce::ignoreUnused(e);

  if (!dragging)
    return false;

  std::vector<std::vector<float>> newPitchCurves;
  newPitchCurves.reserve(affectedNotes.size());
  for (auto* note : affectedNotes)
  {
    if (note)
      newPitchCurves.push_back(note->getDeltaPitch());
    else
      newPitchCurves.emplace_back();
  }

  auto action = std::make_unique<PitchToolAction>(
      project, affectedNotes, originalPitchCurves, newPitchCurves, onRangeChanged);

  if (undoManager)
    undoManager->addAction(std::move(action));

  if (onRangeChanged)
  {
    for (auto* note : affectedNotes)
    {
      if (note)
        onRangeChanged(note->getStartFrame(), note->getEndFrame());
    }
  }

  dragging = false;
  activeHandleType = PitchToolHandles::HandleType::None;
  affectedNotes.clear();
  originalPitchCurves.clear();
  return true;
}

void PitchToolController::applyOperation(std::vector<Note*>& notes,
                                         PitchToolHandles::HandleType type,
                                         float dragDeltaX,
                                         float dragDeltaY,
                                         const CoordinateMapper& mapper)
{
  if (!project)
    return;  // Cannot proceed without project reference

  const float pixelsPerSemitone = juce::jmax(1.0f, mapper.getPixelsPerSemitone());
  const float semitoneDelta = -dragDeltaY / pixelsPerSemitone;

  // Debug logging
  DBG("PitchTool applyOperation: type=" << static_cast<int>(type) 
      << ", deltaX=" << dragDeltaX << ", deltaY=" << dragDeltaY 
      << ", semitoneDelta=" << semitoneDelta << ", notes=" << notes.size());

  auto& audioData = project->getAudioData();

  for (size_t i = 0; i < notes.size(); ++i)
  {
    auto* note = notes[i];
    if (!note || i >= originalPitchCurves.size())
      continue;

    const auto& originalCurve = originalPitchCurves[i];
    std::vector<float> newCurve;

    switch (type)
    {
      case PitchToolHandles::HandleType::TiltLeft:
      {
        // Negate amount so drag UP moves left edge UP (positive direction)
        const float amount = -semitoneDelta;
        DBG("  TiltLeft: amount=" << amount << " semitones, pivot=1.0 (RIGHT fixed)");
        newCurve = PitchToolOperations::tiltDeltaPitch(originalCurve, 1.0f, amount);
        break;
      }
      case PitchToolHandles::HandleType::TiltRight:
      {
        const float amount = semitoneDelta;
        DBG("  TiltRight: amount=" << amount << " semitones, pivot=0.0 (LEFT fixed)");
        newCurve = PitchToolOperations::tiltDeltaPitch(originalCurve, 0.0f, amount);
        break;
      }
      case PitchToolHandles::HandleType::ReduceVariance:
      {
        // 100px drag DOWN = factor 0.0 (fully flatten)
        // 100px drag UP = factor 1.0+ (restore/enhance)
        const float factor = juce::jlimit(0.0f, 1.0f, 1.0f - dragDeltaY / 100.0f);
        DBG("  ReduceVariance: factor=" << factor << " (1.0=unchanged, 0.0=flat)");
        newCurve = PitchToolOperations::reduceVariance(originalCurve, factor);
        break;
      }
      case PitchToolHandles::HandleType::SmoothLeft:
      {
        // Drag RIGHT (positive X) into note = more smoothing
        // 5px per additional frame, range [5, 50]
        const int transitionFrames = juce::jlimit(5, 50, 5 + static_cast<int>(dragDeltaX / 5.0f));
        const float targetPitch = 0.0f;
        DBG("  SmoothLeft: transitionFrames=" << transitionFrames << " (dragX=" << dragDeltaX << ")");
        newCurve = PitchToolOperations::smoothBoundary(originalCurve, 0, transitionFrames, targetPitch);
        break;
      }
      case PitchToolHandles::HandleType::SmoothRight:
      {
        // Drag LEFT (negative X) into note = more smoothing
        // Use -dragDeltaX so LEFT drag increases frames
        const int transitionFrames = juce::jlimit(5, 50, 5 + static_cast<int>(-dragDeltaX / 5.0f));
        const float targetPitch = 0.0f;
        DBG("  SmoothRight: transitionFrames=" << transitionFrames << " (dragX=" << dragDeltaX << ")");
        newCurve = PitchToolOperations::smoothBoundary(originalCurve, 1, transitionFrames, targetPitch);
        break;
      }
      case PitchToolHandles::HandleType::None:
      default:
        continue;  // Skip this note
    }

    // Update Note object
    note->setDeltaPitch(newCurve);
    note->markDirty();

    // CRITICAL: Propagate to audioData.deltaPitch for visual updates
    const int startFrame = note->getStartFrame();
    const int endFrame = note->getEndFrame();
    const int numFrames = endFrame - startFrame;

    // Ensure audioData.deltaPitch is sized correctly
    if (audioData.deltaPitch.size() < audioData.f0.size())
      audioData.deltaPitch.resize(audioData.f0.size(), 0.0f);

    // Write the modified curve to global audioData
    for (int frameIdx = 0; frameIdx < numFrames && frameIdx < static_cast<int>(newCurve.size()); ++frameIdx)
    {
      const int globalFrameIdx = startFrame + frameIdx;
      if (globalFrameIdx >= 0 && globalFrameIdx < static_cast<int>(audioData.deltaPitch.size()))
        audioData.deltaPitch[static_cast<size_t>(globalFrameIdx)] = newCurve[static_cast<size_t>(frameIdx)];
    }
  }

  // Trigger visual update
  if (onPitchEdited)
    onPitchEdited();
}

void PitchToolController::cancel()
{
  if (!dragging)
    return;

  for (size_t i = 0; i < affectedNotes.size(); ++i)
  {
    if (i < originalPitchCurves.size() && affectedNotes[i])
      affectedNotes[i]->setDeltaPitch(originalPitchCurves[i]);
  }

  dragging = false;
  activeHandleType = PitchToolHandles::HandleType::None;
  affectedNotes.clear();
  originalPitchCurves.clear();
}
