#include "PitchToolController.h"
#include "../../Utils/PitchCurveProcessor.h"

#include <algorithm>
#include <limits>

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
  
  // Capture original transformation parameters (not curves)
  originalParams.clear();
  originalParams.reserve(affectedNotes.size());
  for (auto* note : affectedNotes)
  {
    if (note)
    {
      TransformParams params;
      params.tiltLeft = note->getTiltLeft();
      params.tiltRight = note->getTiltRight();
      params.varianceScale = note->getVarianceScale();
      params.smoothLeftFrames = note->getSmoothLeftFrames();
      params.smoothRightFrames = note->getSmoothRightFrames();
      params.midiNote = note->getMidiNote();
      originalParams.push_back(params);
    }
    else
    {
      originalParams.emplace_back();  // Default params for null note
    }
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

  // Capture new transformation parameters (not curves)
  std::vector<TransformParams> newParams;
  newParams.reserve(affectedNotes.size());
  for (auto* note : affectedNotes)
  {
    if (note)
    {
      TransformParams params;
       params.tiltLeft = note->getTiltLeft();
       params.tiltRight = note->getTiltRight();
       params.varianceScale = note->getVarianceScale();
       params.smoothLeftFrames = note->getSmoothLeftFrames();
       params.smoothRightFrames = note->getSmoothRightFrames();
       newParams.push_back(params);
    }
    else
    {
      newParams.emplace_back();  // Default params for null note
    }
  }

  auto action = std::make_unique<PitchToolAction>(
      project, affectedNotes, originalParams, newParams, onRangeChanged);

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
  originalParams.clear();
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

  for (size_t i = 0; i < notes.size(); ++i)
  {
    auto* note = notes[i];
    if (!note || i >= originalParams.size())
      continue;

    // Restore original parameters before applying new transformation
    const auto& origParams = originalParams[i];
    note->setMidiNote(origParams.midiNote);
    note->setTiltLeft(origParams.tiltLeft);
    note->setTiltRight(origParams.tiltRight);
    note->setVarianceScale(origParams.varianceScale);
    note->setSmoothLeftFrames(origParams.smoothLeftFrames);
    note->setSmoothRightFrames(origParams.smoothRightFrames);

    // Apply new transformation by updating the appropriate parameter
    switch (type)
    {
      case PitchToolHandles::HandleType::TiltLeft:
      {
        // Drag UP = positive semitoneDelta = left edge goes UP
        const float amount = semitoneDelta;
        DBG("  TiltLeft: amount=" << amount << " semitones");
        note->setTiltLeft(origParams.tiltLeft + amount);
        
        // Calculate tilt mean shift
        const float newTiltMean = (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
        note->setMidiNote(origParams.midiNote + newTiltMean);
        break;
      }
      case PitchToolHandles::HandleType::TiltRight:
      {
        const float amount = semitoneDelta;
        DBG("  TiltRight: amount=" << amount << " semitones");
        note->setTiltRight(origParams.tiltRight + amount);
        
        // Calculate tilt mean shift
        const float newTiltMean = (note->getTiltLeft() + note->getTiltRight()) / 2.0f;
        note->setMidiNote(origParams.midiNote + newTiltMean);
        break;
      }
      case PitchToolHandles::HandleType::ReduceVariance:
      {
        // Additive accumulation from original value (consistent with tilt handles)
        // Drag UP (negative Y) = increase variance, drag DOWN (positive Y) = decrease
        const float dragDelta = -dragDeltaY / 100.0f;
        const float newScale = origParams.varianceScale + dragDelta;
        DBG("  ReduceVariance: newScale=" << newScale << " (orig=" << origParams.varianceScale 
            << ", delta=" << dragDelta << ")");
        note->setVarianceScale(newScale);
        break;
      }
      case PitchToolHandles::HandleType::SmoothLeft:
      {
        // Drag RIGHT (positive X) into note = more smoothing
        // 5px per additional frame, range [5, 50]
        const int transitionFrames = juce::jlimit(5, 50, 5 + static_cast<int>(dragDeltaX / 5.0f));
        DBG("  SmoothLeft: transitionFrames=" << transitionFrames << " (dragX=" << dragDeltaX << ")");
        note->setSmoothLeftFrames(transitionFrames);
        break;
      }
      case PitchToolHandles::HandleType::SmoothRight:
      {
        // Drag LEFT (negative X) into note = more smoothing
        // Use -dragDeltaX so LEFT drag increases frames
        const int transitionFrames = juce::jlimit(5, 50, 5 + static_cast<int>(-dragDeltaX / 5.0f));
        DBG("  SmoothRight: transitionFrames=" << transitionFrames << " (dragX=" << dragDeltaX << ")");
        note->setSmoothRightFrames(transitionFrames);
        break;
      }
      case PitchToolHandles::HandleType::None:
      default:
        continue;  // Skip this note
    }

    note->markDirty();
  }

  // NON-DESTRUCTIVE: Recompose audioData from Note properties
  // This reads originalDeltaPitch and applies all transformation parameters
  PitchCurveProcessor::rebuildBaseFromNotes(*project);
  PitchCurveProcessor::composeF0InPlace(*project, /*applyUvMask=*/false);
  
  // Mark dirty range for synthesis
  if (!notes.empty())
  {
    int minFrame = std::numeric_limits<int>::max();
    int maxFrame = std::numeric_limits<int>::min();
    for (const auto* note : notes)
    {
      minFrame = std::min(minFrame, note->getStartFrame());
      maxFrame = std::max(maxFrame, note->getEndFrame());
    }
    project->setF0DirtyRange(minFrame, maxFrame);
  }

  // Trigger visual update
  if (onPitchEdited)
    onPitchEdited();
}

void PitchToolController::cancel()
{
  if (!dragging)
    return;

  // Restore original transformation parameters
  for (size_t i = 0; i < affectedNotes.size(); ++i)
  {
    if (i < originalParams.size() && affectedNotes[i])
    {
      const auto& params = originalParams[i];
      affectedNotes[i]->setMidiNote(params.midiNote);
      affectedNotes[i]->setTiltLeft(params.tiltLeft);
      affectedNotes[i]->setTiltRight(params.tiltRight);
      affectedNotes[i]->setVarianceScale(params.varianceScale);
      affectedNotes[i]->setSmoothLeftFrames(params.smoothLeftFrames);
      affectedNotes[i]->setSmoothRightFrames(params.smoothRightFrames);
    }
  }

  dragging = false;
  activeHandleType = PitchToolHandles::HandleType::None;
  affectedNotes.clear();
  originalParams.clear();
}
