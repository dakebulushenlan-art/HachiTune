#include "StretchHandler.h"
#include "../../PianoRollComponent.h"
#include "../../../Utils/CurveResampler.h"
#include "../../../Utils/PitchCurveProcessor.h"

StretchHandler::StretchHandler(PianoRollComponent &owner)
    : InteractionHandler(owner) {
  centeredMelComputer = std::make_unique<CenteredMelSpectrogram>();
}

bool StretchHandler::mouseDown(const juce::MouseEvent &e, float worldX,
                               float worldY) {
  auto *project = owner_.project;
  if (!project)
    return false;

  int boundaryIndex = findStretchBoundaryIndex(worldX, stretchHandleHitPadding);
  if (boundaryIndex >= 0) {
    auto boundaries = collectStretchBoundaries();
    if (boundaryIndex < static_cast<int>(boundaries.size())) {
      startStretchDrag(boundaries[static_cast<size_t>(boundaryIndex)]);
      owner_.repaint();
      return true;
    }
  }

  // In stretch mode, allow selecting notes but disable pitch dragging.
  Note *note = owner_.findNoteAt(worldX, worldY);
  if (note) {
    project->deselectAllNotes();
    note->setSelected(true);
    owner_.updatePitchToolHandlesFromSelection();
    if (owner_.onNoteSelected)
      owner_.onNoteSelected(note);
    owner_.repaint();
    return true;
  }

  // Box selection fallback
  project->deselectAllNotes();
  owner_.updatePitchToolHandlesFromSelection();
  owner_.boxSelector->startSelection(worldX, worldY);
  owner_.repaint();
  return true;
}

bool StretchHandler::mouseDrag(const juce::MouseEvent &e, float worldX,
                               float worldY) {
  if (!stretchDrag.active)
    return false;

  auto *project = owner_.project;
  if (!project)
    return false;

  const double time = owner_.xToTime(worldX);
  const int targetFrame =
      static_cast<int>(secondsToFrames(static_cast<float>(time)));
  updateStretchDrag(targetFrame);

  // Use the owner's drag repaint throttling
  const auto now = juce::Time::currentTimeMillis();
  const bool shouldRepaint =
      (now - owner_.lastDragRepaintTime) >=
      PianoRollComponent::minDragRepaintInterval;
  if (shouldRepaint) {
    owner_.repaint();
    owner_.lastDragRepaintTime = now;
  }
  return true;
}

bool StretchHandler::mouseUp(const juce::MouseEvent &e, float worldX,
                             float worldY) {
  if (!stretchDrag.active)
    return false;

  finishStretchDrag();
  owner_.repaint();
  return true;
}

void StretchHandler::mouseMove(const juce::MouseEvent &e, float worldX,
                               float worldY) {
  if (e.y >= PianoRollComponent::headerHeight &&
      e.x >= PianoRollComponent::pianoKeysWidth) {
    float adjustedX =
        e.x - PianoRollComponent::pianoKeysWidth +
        static_cast<float>(owner_.scrollX);
    int boundaryIndex =
        findStretchBoundaryIndex(adjustedX, stretchHandleHitPadding);
    hoveredStretchBoundaryIndex = boundaryIndex;
    if (boundaryIndex >= 0) {
      owner_.setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    } else {
      owner_.setMouseCursor(juce::MouseCursor::NormalCursor);
    }
  } else {
    hoveredStretchBoundaryIndex = -1;
  }
  owner_.repaint();
}

void StretchHandler::draw(juce::Graphics &g) {
  auto *project = owner_.project;
  if (!project)
    return;

  auto boundaries = collectStretchBoundaries();
  if (boundaries.empty())
    return;

  const float height =
      (MAX_MIDI_NOTE - MIN_MIDI_NOTE + 1) * owner_.pixelsPerSemitone;

  for (size_t i = 0; i < boundaries.size(); ++i) {
    int frame = boundaries[i].frame;
    const bool active =
        stretchDrag.active &&
        boundaries[i].left == stretchDrag.boundary.left &&
        boundaries[i].right == stretchDrag.boundary.right;
    if (active)
      frame = stretchDrag.currentBoundary;

    float x = framesToSeconds(frame) * owner_.pixelsPerSecond;

    const bool isHovered = static_cast<int>(i) == hoveredStretchBoundaryIndex;
    float alpha = isHovered || active ? 0.8f : 0.35f;
    float thickness = isHovered || active ? 2.0f : 1.0f;

    g.setColour(APP_COLOR_PRIMARY.withAlpha(alpha));
    g.drawLine(x, 0.0f, x, height, thickness);
  }
}

bool StretchHandler::isActive() const { return stretchDrag.active; }

void StretchHandler::cancel() { cancelStretchDrag(); }

// --- Private implementation ---

std::vector<StretchHandler::StretchBoundary>
StretchHandler::collectStretchBoundaries() const {
  std::vector<StretchBoundary> boundaries;
  auto *project = owner_.project;
  if (!project)
    return boundaries;

  std::vector<Note *> ordered;
  ordered.reserve(project->getNotes().size());
  for (auto &note : project->getNotes()) {
    if (!note.isRest())
      ordered.push_back(&note);
  }

  if (ordered.empty())
    return boundaries;

  std::sort(ordered.begin(), ordered.end(),
            [](const Note *a, const Note *b) {
              return a->getStartFrame() < b->getStartFrame();
            });

  // Gap threshold: if gap between notes > this, treat them as separate segments
  constexpr int gapThreshold = 3; // frames

  for (size_t i = 0; i < ordered.size(); ++i) {
    Note *current = ordered[i];
    Note *prev = (i > 0) ? ordered[i - 1] : nullptr;
    Note *next = (i + 1 < ordered.size()) ? ordered[i + 1] : nullptr;

    // Check if there's a gap before this note
    bool hasGapBefore = true;
    if (prev) {
      int gap = current->getStartFrame() - prev->getEndFrame();
      hasGapBefore = (gap > gapThreshold);
    }

    // Check if there's a gap after this note
    bool hasGapAfter = true;
    if (next) {
      int gap = next->getStartFrame() - current->getEndFrame();
      hasGapAfter = (gap > gapThreshold);
    }

    // Add left boundary if there's a gap before (or it's the first note)
    if (hasGapBefore) {
      boundaries.push_back({nullptr, current, current->getStartFrame()});
    }

    // Add right boundary if there's a gap after (or it's the last note)
    if (hasGapAfter) {
      boundaries.push_back({current, nullptr, current->getEndFrame()});
    }

    // Add boundary between adjacent notes (no gap)
    if (next && !hasGapAfter) {
      boundaries.push_back({current, next, current->getEndFrame()});
    }
  }

  // Sort boundaries by frame position
  std::sort(boundaries.begin(), boundaries.end(),
            [](const StretchBoundary &a, const StretchBoundary &b) {
              return a.frame < b.frame;
            });

  return boundaries;
}

int StretchHandler::findStretchBoundaryIndex(float worldX,
                                             float tolerancePx) const {
  auto boundaries = collectStretchBoundaries();
  int bestIndex = -1;
  float bestDist = tolerancePx;

  for (size_t i = 0; i < boundaries.size(); ++i) {
    float boundaryX =
        framesToSeconds(boundaries[i].frame) * owner_.pixelsPerSecond;
    float dist = std::abs(worldX - boundaryX);
    if (dist <= bestDist) {
      bestIndex = static_cast<int>(i);
      bestDist = dist;
    }
  }

  return bestIndex;
}

void StretchHandler::startStretchDrag(const StretchBoundary &boundary) {
  auto *project = owner_.project;
  if (!project)
    return;

  // At least one note must exist
  if (!boundary.left && !boundary.right)
    return;

  stretchDrag = {};
  stretchDrag.active = true;
  stretchDrag.boundary = boundary;

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.f0.size());
  if (totalFrames <= 0) {
    stretchDrag.active = false;
    return;
  }

  // Determine the boundary frame and limits based on which notes exist
  if (boundary.left && boundary.right) {
    // Both notes exist - stretch boundary between them
    stretchDrag.originalBoundary = boundary.left->getEndFrame();
    stretchDrag.originalLeftStart = boundary.left->getStartFrame();
    stretchDrag.originalLeftEnd = boundary.left->getEndFrame();
    stretchDrag.originalRightStart = boundary.right->getStartFrame();
    stretchDrag.originalRightEnd = boundary.right->getEndFrame();
    stretchDrag.minFrame =
        stretchDrag.originalLeftStart + minStretchNoteFrames;
    stretchDrag.maxFrame =
        stretchDrag.originalRightEnd - minStretchNoteFrames;
  } else if (boundary.right) {
    // Only right note - stretch its left boundary
    stretchDrag.originalBoundary = boundary.right->getStartFrame();
    stretchDrag.originalLeftStart = 0;
    stretchDrag.originalLeftEnd = 0;
    stretchDrag.originalRightStart = boundary.right->getStartFrame();
    stretchDrag.originalRightEnd = boundary.right->getEndFrame();
    stretchDrag.minFrame = 0;
    stretchDrag.maxFrame =
        stretchDrag.originalRightEnd - minStretchNoteFrames;
  } else {
    // Only left note - stretch its right boundary
    stretchDrag.originalBoundary = boundary.left->getEndFrame();
    stretchDrag.originalLeftStart = boundary.left->getStartFrame();
    stretchDrag.originalLeftEnd = boundary.left->getEndFrame();
    stretchDrag.originalRightStart = totalFrames;
    stretchDrag.originalRightEnd = totalFrames;
    stretchDrag.minFrame =
        stretchDrag.originalLeftStart + minStretchNoteFrames;
    stretchDrag.maxFrame = totalFrames;
  }

  stretchDrag.currentBoundary = stretchDrag.originalBoundary;

  // Ensure all notes have clip waveforms
  if (audioData.waveform.getNumSamples() > 0) {
    const float *src = audioData.waveform.getReadPointer(0);
    const int totalSamples = audioData.waveform.getNumSamples();
    for (auto &note : project->getNotes()) {
      if (note.hasClipWaveform())
        continue;
      int startSample = note.getStartFrame() * HOP_SIZE;
      int endSample = note.getEndFrame() * HOP_SIZE;
      startSample = std::max(0, std::min(startSample, totalSamples));
      endSample = std::max(startSample, std::min(endSample, totalSamples));
      std::vector<float> clip;
      clip.reserve(static_cast<size_t>(endSample - startSample));
      for (int i = startSample; i < endSample; ++i)
        clip.push_back(src[i]);
      note.setClipWaveform(std::move(clip));
    }
  }

  if (audioData.deltaPitch.size() < static_cast<size_t>(totalFrames))
    audioData.deltaPitch.resize(static_cast<size_t>(totalFrames), 0.0f);
  if (audioData.voicedMask.size() < static_cast<size_t>(totalFrames))
    audioData.voicedMask.resize(static_cast<size_t>(totalFrames), true);

  if (stretchDrag.maxFrame <= stretchDrag.minFrame) {
    stretchDrag.active = false;
    return;
  }

  // Calculate range for undo/redo - must include all potentially affected frames
  if (boundary.left && boundary.right) {
    stretchDrag.rangeStartFull =
        std::max(0, stretchDrag.originalLeftStart);
    stretchDrag.rangeEndFull =
        std::min(totalFrames, stretchDrag.originalRightEnd);
  } else if (boundary.left) {
    stretchDrag.rangeStartFull =
        std::max(0, stretchDrag.originalLeftStart);
    stretchDrag.rangeEndFull =
        std::min(totalFrames, stretchDrag.maxFrame);
  } else {
    stretchDrag.rangeStartFull = std::max(0, stretchDrag.minFrame);
    stretchDrag.rangeEndFull =
        std::min(totalFrames, stretchDrag.originalRightEnd);
  }

  if (stretchDrag.rangeEndFull <= stretchDrag.rangeStartFull) {
    stretchDrag.active = false;
    return;
  }

  // Save left note data if exists
  if (boundary.left) {
    int leftStart = std::max(0, stretchDrag.originalLeftStart);
    int leftEnd = std::min(stretchDrag.originalLeftEnd, totalFrames);
    if (leftEnd > leftStart) {
      stretchDrag.leftDelta.assign(audioData.deltaPitch.begin() + leftStart,
                                   audioData.deltaPitch.begin() + leftEnd);
      stretchDrag.leftVoiced.assign(audioData.voicedMask.begin() + leftStart,
                                    audioData.voicedMask.begin() + leftEnd);
    }
    if (boundary.left->hasClipWaveform())
      stretchDrag.originalLeftClip = boundary.left->getClipWaveform();
  }

  // Save right note data if exists
  if (boundary.right) {
    int rightStart = std::max(0, stretchDrag.originalRightStart);
    int rightEnd = std::min(stretchDrag.originalRightEnd, totalFrames);
    if (rightEnd > rightStart) {
      stretchDrag.rightDelta.assign(audioData.deltaPitch.begin() + rightStart,
                                    audioData.deltaPitch.begin() + rightEnd);
      stretchDrag.rightVoiced.assign(audioData.voicedMask.begin() + rightStart,
                                     audioData.voicedMask.begin() + rightEnd);
    }
    if (boundary.right->hasClipWaveform())
      stretchDrag.originalRightClip = boundary.right->getClipWaveform();
  }

  // Save full range data for undo
  stretchDrag.originalDeltaRangeFull.assign(
      audioData.deltaPitch.begin() + stretchDrag.rangeStartFull,
      audioData.deltaPitch.begin() + stretchDrag.rangeEndFull);
  stretchDrag.originalVoicedRangeFull.assign(
      audioData.voicedMask.begin() + stretchDrag.rangeStartFull,
      audioData.voicedMask.begin() + stretchDrag.rangeEndFull);

  if (!audioData.melSpectrogram.empty() &&
      stretchDrag.rangeStartFull <
          static_cast<int>(audioData.melSpectrogram.size())) {
    int melEnd = std::min(stretchDrag.rangeEndFull,
                          static_cast<int>(audioData.melSpectrogram.size()));
    stretchDrag.originalMelRangeFull.assign(
        audioData.melSpectrogram.begin() + stretchDrag.rangeStartFull,
        audioData.melSpectrogram.begin() + melEnd);
  }
}

void StretchHandler::updateStretchDrag(int targetFrame) {
  if (!stretchDrag.active || !owner_.project)
    return;

  // At least one note must exist
  if (!stretchDrag.boundary.left && !stretchDrag.boundary.right)
    return;

  int previewRangeStart = -1;
  int previewRangeEnd = -1;

  targetFrame =
      juce::jlimit(stretchDrag.minFrame, stretchDrag.maxFrame, targetFrame);
  if (targetFrame == stretchDrag.currentBoundary)
    return;

  // Calculate new lengths based on which notes exist
  int newLeftLength = 0;
  int newRightLength = 0;

  if (stretchDrag.boundary.left && stretchDrag.boundary.right) {
    newLeftLength = targetFrame - stretchDrag.originalLeftStart;
    newRightLength = stretchDrag.originalRightEnd - targetFrame;
    if (newLeftLength < minStretchNoteFrames ||
        newRightLength < minStretchNoteFrames)
      return;
  } else if (stretchDrag.boundary.right) {
    newRightLength = stretchDrag.originalRightEnd - targetFrame;
    if (newRightLength < minStretchNoteFrames)
      return;
  } else {
    newLeftLength = targetFrame - stretchDrag.originalLeftStart;
    if (newLeftLength < minStretchNoteFrames)
      return;
  }

  stretchDrag.currentBoundary = targetFrame;
  stretchDrag.changed = true;

  auto &audioData = owner_.project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  if (audioData.deltaPitch.size() < static_cast<size_t>(totalFrames))
    audioData.deltaPitch.resize(static_cast<size_t>(totalFrames), 0.0f);
  if (audioData.voicedMask.size() < static_cast<size_t>(totalFrames))
    audioData.voicedMask.resize(static_cast<size_t>(totalFrames), true);

  // Restore original region to avoid cumulative errors during drag.
  if (!stretchDrag.originalDeltaRangeFull.empty() &&
      !stretchDrag.originalVoicedRangeFull.empty()) {
    for (int i = stretchDrag.rangeStartFull; i < stretchDrag.rangeEndFull;
         ++i) {
      int idx = i - stretchDrag.rangeStartFull;
      audioData.deltaPitch[static_cast<size_t>(i)] =
          stretchDrag.originalDeltaRangeFull[static_cast<size_t>(idx)];
      audioData.voicedMask[static_cast<size_t>(i)] =
          stretchDrag.originalVoicedRangeFull[static_cast<size_t>(idx)];
    }
  }
  if (!stretchDrag.originalMelRangeFull.empty() &&
      audioData.melSpectrogram.size() >=
          static_cast<size_t>(stretchDrag.rangeStartFull +
                              stretchDrag.originalMelRangeFull.size())) {
    for (size_t i = 0; i < stretchDrag.originalMelRangeFull.size(); ++i)
      audioData
          .melSpectrogram[static_cast<size_t>(stretchDrag.rangeStartFull) +
                          i] = stretchDrag.originalMelRangeFull[i];
  }

  auto smoothResampledVoiced = [](std::vector<bool> &mask) {
    if (mask.size() < 3)
      return;
    std::vector<bool> smoothed(mask);
    for (size_t i = 1; i + 1 < mask.size(); ++i) {
      const bool p = mask[i - 1];
      const bool c = mask[i];
      const bool n = mask[i + 1];
      if (!c && p && n)
        smoothed[i] = true;
      else if (c && !p && !n)
        smoothed[i] = false;
    }
    mask.swap(smoothed);
  };

  // Update left note if exists
  if (stretchDrag.boundary.left && newLeftLength > 0 &&
      !stretchDrag.leftDelta.empty()) {
    const int leftStart = stretchDrag.originalLeftStart;
    auto newLeftDelta =
        CurveResampler::resampleLinear(stretchDrag.leftDelta, newLeftLength);
    auto newLeftVoiced =
        CurveResampler::resampleNearest(stretchDrag.leftVoiced, newLeftLength);
    smoothResampledVoiced(newLeftVoiced);

    for (int i = 0; i < newLeftLength; ++i) {
      audioData.deltaPitch[static_cast<size_t>(leftStart + i)] =
          newLeftDelta[static_cast<size_t>(i)];
      audioData.voicedMask[static_cast<size_t>(leftStart + i)] =
          newLeftVoiced[static_cast<size_t>(i)];
    }

    if (!stretchDrag.originalLeftClip.empty()) {
      const int newLeftSamples = std::max(0, newLeftLength * HOP_SIZE);
      auto newLeftClip = CurveResampler::resampleLinear(
          stretchDrag.originalLeftClip, newLeftSamples);
      stretchDrag.boundary.left->setClipWaveform(std::move(newLeftClip));
    }

    stretchDrag.boundary.left->setEndFrame(targetFrame);
    stretchDrag.boundary.left->markDirty();
  }

  // Update right note if exists
  if (stretchDrag.boundary.right && newRightLength > 0 &&
      !stretchDrag.rightDelta.empty()) {
    auto newRightDelta =
        CurveResampler::resampleLinear(stretchDrag.rightDelta, newRightLength);
    auto newRightVoiced = CurveResampler::resampleNearest(
        stretchDrag.rightVoiced, newRightLength);
    smoothResampledVoiced(newRightVoiced);

    for (int i = 0; i < newRightLength; ++i) {
      audioData.deltaPitch[static_cast<size_t>(targetFrame + i)] =
          newRightDelta[static_cast<size_t>(i)];
      audioData.voicedMask[static_cast<size_t>(targetFrame + i)] =
          newRightVoiced[static_cast<size_t>(i)];
    }

    if (!stretchDrag.originalRightClip.empty()) {
      const int newRightSamples = std::max(0, newRightLength * HOP_SIZE);
      auto newRightClip = CurveResampler::resampleLinear(
          stretchDrag.originalRightClip, newRightSamples);
      stretchDrag.boundary.right->setClipWaveform(std::move(newRightClip));
    }

    stretchDrag.boundary.right->setStartFrame(targetFrame);
    stretchDrag.boundary.right->setEndFrame(stretchDrag.originalRightEnd);
    stretchDrag.boundary.right->markDirty();
  }

  // Update mel spectrogram using fast nearest neighbor during drag
  // (High-quality centered STFT is computed in finishStretchDrag)
  if (!audioData.melSpectrogram.empty() &&
      stretchDrag.rangeStartFull <
          static_cast<int>(audioData.melSpectrogram.size())) {
    const int melSize = static_cast<int>(audioData.melSpectrogram.size());
    int rangeStart = stretchDrag.rangeStartFull;
    int rangeEnd = stretchDrag.rangeEndFull;

    // Adjust range based on which notes exist
    if (stretchDrag.boundary.left && !stretchDrag.boundary.right) {
      rangeEnd = targetFrame;
    } else if (!stretchDrag.boundary.left && stretchDrag.boundary.right) {
      rangeStart = targetFrame;
    }

    rangeStart = std::clamp(rangeStart, 0, melSize);
    rangeEnd = std::clamp(rangeEnd, 0, melSize);

    std::vector<std::vector<float>> newMel;
    if (rangeEnd > rangeStart) {
      // Use fast nearest neighbor resampling for drag preview
      std::vector<std::vector<float>> newLeftMel;
      if (stretchDrag.boundary.left && newLeftLength > 0) {
        const int leftOffset =
            stretchDrag.originalLeftStart - stretchDrag.rangeStartFull;
        if (leftOffset >= 0 &&
            leftOffset + (stretchDrag.originalLeftEnd -
                          stretchDrag.originalLeftStart) <=
                static_cast<int>(
                    stretchDrag.originalMelRangeFull.size())) {
          std::vector<std::vector<float>> leftMel(
              stretchDrag.originalMelRangeFull.begin() + leftOffset,
              stretchDrag.originalMelRangeFull.begin() + leftOffset +
                  (stretchDrag.originalLeftEnd -
                   stretchDrag.originalLeftStart));
          newLeftMel =
              CurveResampler::resampleNearest2D(leftMel, newLeftLength);
        }
      }

      std::vector<std::vector<float>> newRightMel;
      if (stretchDrag.boundary.right && newRightLength > 0) {
        const int rightOffset =
            stretchDrag.originalRightStart - stretchDrag.rangeStartFull;
        if (rightOffset >= 0 &&
            rightOffset + (stretchDrag.originalRightEnd -
                           stretchDrag.originalRightStart) <=
                static_cast<int>(
                    stretchDrag.originalMelRangeFull.size())) {
          std::vector<std::vector<float>> rightMel(
              stretchDrag.originalMelRangeFull.begin() + rightOffset,
              stretchDrag.originalMelRangeFull.begin() + rightOffset +
                  (stretchDrag.originalRightEnd -
                   stretchDrag.originalRightStart));
          newRightMel =
              CurveResampler::resampleNearest2D(rightMel, newRightLength);
        }
      }

      // Combine mel spectrograms
      if (stretchDrag.boundary.left && stretchDrag.boundary.right) {
        newMel.reserve(
            static_cast<size_t>(newLeftLength + newRightLength));
        newMel.insert(newMel.end(), newLeftMel.begin(), newLeftMel.end());
        newMel.insert(newMel.end(), newRightMel.begin(),
                      newRightMel.end());
      } else if (stretchDrag.boundary.left) {
        newMel = std::move(newLeftMel);
      } else {
        newMel = std::move(newRightMel);
      }
    }

    if (!newMel.empty() &&
        static_cast<int>(newMel.size()) == (rangeEnd - rangeStart)) {
      for (int i = rangeStart; i < rangeEnd; ++i)
        audioData.melSpectrogram[static_cast<size_t>(i)] =
            newMel[static_cast<size_t>(i - rangeStart)];
      previewRangeStart = rangeStart;
      previewRangeEnd = rangeEnd;
    }
  }

  PitchCurveProcessor::rebuildBaseFromNotes(*owner_.project);
  owner_.invalidateBasePitchCache();

  if (owner_.onPitchEdited)
    owner_.onPitchEdited();

  // Mark dirty range for synthesis when drag finishes (not during drag)
  if (previewRangeStart >= 0 && previewRangeEnd > previewRangeStart) {
    const int f0Size = static_cast<int>(audioData.f0.size());
    int smoothStart = std::max(0, previewRangeStart - 60);
    int smoothEnd = std::min(f0Size, previewRangeEnd + 60);
    owner_.project->setF0DirtyRange(smoothStart, smoothEnd);
  }
}

void StretchHandler::finishStretchDrag() {
  auto *project = owner_.project;
  if (!stretchDrag.active || !project) {
    stretchDrag = {};
    return;
  }

  if (!stretchDrag.changed) {
    cancelStretchDrag();
    return;
  }

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  const int currentBoundary = stretchDrag.currentBoundary;
  int rangeStart = std::clamp(stretchDrag.rangeStartFull, 0, totalFrames);
  int rangeEnd = std::clamp(stretchDrag.rangeEndFull, 0, totalFrames);
  if (rangeEnd <= rangeStart) {
    cancelStretchDrag();
    return;
  }

  std::vector<float> newDelta(audioData.deltaPitch.begin() + rangeStart,
                              audioData.deltaPitch.begin() + rangeEnd);
  std::vector<bool> newVoiced(audioData.voicedMask.begin() + rangeStart,
                              audioData.voicedMask.begin() + rangeEnd);
  std::vector<std::vector<float>> newMel;
  if (!audioData.melSpectrogram.empty() &&
      rangeEnd <= static_cast<int>(audioData.melSpectrogram.size()) &&
      audioData.waveform.getNumSamples() > 0 && centeredMelComputer) {
    // Only compute length for notes that actually exist
    const int leftLen = stretchDrag.boundary.left
                            ? (currentBoundary - stretchDrag.originalLeftStart)
                            : 0;
    const int rightLen =
        stretchDrag.boundary.right
            ? (stretchDrag.originalRightEnd - currentBoundary)
            : 0;

    // Use CenteredMelSpectrogram for high-quality time stretching
    const float *globalAudio = audioData.waveform.getReadPointer(0);
    const int numSamples = audioData.waveform.getNumSamples();

    std::vector<std::vector<float>> newLeftMel;
    std::vector<std::vector<float>> newRightMel;

    if (leftLen > 0) {
      centeredMelComputer->computeTimeStretched(
          globalAudio, numSamples, stretchDrag.originalLeftStart,
          stretchDrag.originalLeftEnd, leftLen, newLeftMel);
    }

    if (rightLen > 0) {
      centeredMelComputer->computeTimeStretched(
          globalAudio, numSamples, stretchDrag.originalRightStart,
          stretchDrag.originalRightEnd, rightLen, newRightMel);
    }

    // Fallback to nearest neighbor if centered mel computation failed
    if (newLeftMel.empty() && leftLen > 0) {
      const int leftOffset =
          stretchDrag.originalLeftStart - stretchDrag.rangeStartFull;
      std::vector<std::vector<float>> leftMel;
      if (leftOffset >= 0 &&
          leftOffset + (stretchDrag.originalLeftEnd -
                        stretchDrag.originalLeftStart) <=
              static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
        leftMel.assign(
            stretchDrag.originalMelRangeFull.begin() + leftOffset,
            stretchDrag.originalMelRangeFull.begin() + leftOffset +
                (stretchDrag.originalLeftEnd -
                 stretchDrag.originalLeftStart));
      }
      newLeftMel = CurveResampler::resampleNearest2D(leftMel, leftLen);
    }

    if (newRightMel.empty() && rightLen > 0) {
      const int rightOffset =
          stretchDrag.originalRightStart - stretchDrag.rangeStartFull;
      std::vector<std::vector<float>> rightMel;
      if (rightOffset >= 0 &&
          rightOffset + (stretchDrag.originalRightEnd -
                         stretchDrag.originalRightStart) <=
              static_cast<int>(stretchDrag.originalMelRangeFull.size())) {
        rightMel.assign(
            stretchDrag.originalMelRangeFull.begin() + rightOffset,
            stretchDrag.originalMelRangeFull.begin() + rightOffset +
                (stretchDrag.originalRightEnd -
                 stretchDrag.originalRightStart));
      }
      newRightMel = CurveResampler::resampleNearest2D(rightMel, rightLen);
    }

    newMel.reserve(static_cast<size_t>(leftLen + rightLen));
    newMel.insert(newMel.end(), newLeftMel.begin(), newLeftMel.end());
    newMel.insert(newMel.end(), newRightMel.begin(), newRightMel.end());

    if (!newMel.empty() &&
        static_cast<int>(newMel.size()) == (rangeEnd - rangeStart)) {
      for (int i = rangeStart; i < rangeEnd; ++i)
        audioData.melSpectrogram[static_cast<size_t>(i)] =
            newMel[static_cast<size_t>(i - rangeStart)];
    } else {
      newMel.clear();
    }
  }

  int newLeftStart = stretchDrag.boundary.left
                         ? stretchDrag.boundary.left->getStartFrame()
                         : 0;
  int newLeftEnd = stretchDrag.boundary.left
                       ? stretchDrag.boundary.left->getEndFrame()
                       : 0;
  std::vector<float> newLeftClip;
  if (stretchDrag.boundary.left)
    newLeftClip = stretchDrag.boundary.left->getClipWaveform();
  std::vector<float> newRightClip;
  if (stretchDrag.boundary.right)
    newRightClip = stretchDrag.boundary.right->getClipWaveform();

  if (owner_.undoManager) {
    int capturedRangeStart = rangeStart;
    int capturedRangeEnd = rangeEnd;
    std::vector<float> oldDelta;
    std::vector<bool> oldVoiced;
    std::vector<std::vector<float>> oldMel;
    if (!stretchDrag.originalDeltaRangeFull.empty() &&
        !stretchDrag.originalVoicedRangeFull.empty()) {
      int offset = rangeStart - stretchDrag.rangeStartFull;
      int count = rangeEnd - rangeStart;
      if (offset >= 0 &&
          offset + count <=
              static_cast<int>(
                  stretchDrag.originalDeltaRangeFull.size())) {
        oldDelta.assign(
            stretchDrag.originalDeltaRangeFull.begin() + offset,
            stretchDrag.originalDeltaRangeFull.begin() + offset + count);
        oldVoiced.assign(
            stretchDrag.originalVoicedRangeFull.begin() + offset,
            stretchDrag.originalVoicedRangeFull.begin() + offset + count);
      }
    }
    if (!stretchDrag.originalMelRangeFull.empty()) {
      int offset = rangeStart - stretchDrag.rangeStartFull;
      int count = rangeEnd - rangeStart;
      if (offset >= 0 &&
          offset + count <=
              static_cast<int>(
                  stretchDrag.originalMelRangeFull.size())) {
        oldMel.assign(
            stretchDrag.originalMelRangeFull.begin() + offset,
            stretchDrag.originalMelRangeFull.begin() + offset + count);
      }
    }

    // Capture owner_ pointer for the lambda
    auto *ownerPtr = &owner_;
    auto action = std::make_unique<NoteTimingStretchAction>(
        stretchDrag.boundary.left, stretchDrag.boundary.right,
        &audioData.deltaPitch, &audioData.voicedMask,
        &audioData.melSpectrogram, capturedRangeStart, capturedRangeEnd,
        stretchDrag.originalLeftStart, stretchDrag.originalLeftEnd,
        stretchDrag.originalRightStart, stretchDrag.originalRightEnd,
        newLeftStart, newLeftEnd, currentBoundary,
        stretchDrag.originalRightEnd, stretchDrag.originalLeftClip,
        newLeftClip, stretchDrag.originalRightClip, newRightClip,
        std::move(oldDelta), std::move(newDelta), std::move(oldVoiced),
        std::move(newVoiced), std::move(oldMel), std::move(newMel),
        [ownerPtr](int startFrame, int endFrame) {
          if (!ownerPtr->project)
            return;
          PitchCurveProcessor::rebuildBaseFromNotes(*ownerPtr->project);
          ownerPtr->invalidateBasePitchCache();
          const int f0Size =
              static_cast<int>(ownerPtr->project->getAudioData().f0.size());
          int smoothStart = std::max(0, startFrame - 60);
          int smoothEnd = std::min(f0Size, endFrame + 60);
          ownerPtr->project->setF0DirtyRange(smoothStart, smoothEnd);
        });
    owner_.undoManager->addAction(std::move(action));
  }

  // Restore waveform regions outside current note boundaries to original audio.
  if (audioData.waveform.getNumSamples() > 0) {
    const int totalSamples = audioData.waveform.getNumSamples();
    const int numChannels = audioData.waveform.getNumChannels();
    const auto &origWave =
        audioData.originalWaveform.getNumSamples() > 0
            ? audioData.originalWaveform
            : audioData.waveform;
    const int origChannels = origWave.getNumChannels();
    const int origSamples = origWave.getNumSamples();

    // Calculate the sample range that should remain as note audio
    int noteStartSample, noteEndSample;
    if (stretchDrag.boundary.left && stretchDrag.boundary.right) {
      noteStartSample = stretchDrag.rangeStartFull * HOP_SIZE;
      noteEndSample = stretchDrag.rangeEndFull * HOP_SIZE;
    } else if (stretchDrag.boundary.left) {
      noteStartSample = stretchDrag.originalLeftStart * HOP_SIZE;
      noteEndSample = currentBoundary * HOP_SIZE;
    } else {
      noteStartSample = currentBoundary * HOP_SIZE;
      noteEndSample = stretchDrag.originalRightEnd * HOP_SIZE;
    }

    // Restore waveform outside the note boundaries
    const int rangeStartSample = stretchDrag.rangeStartFull * HOP_SIZE;
    const int rangeEndSample = stretchDrag.rangeEndFull * HOP_SIZE;

    for (int ch = 0; ch < numChannels; ++ch) {
      float *dst = audioData.waveform.getWritePointer(ch);
      const float *srcOrig = origWave.getReadPointer(
          std::min(ch, std::max(0, origChannels - 1)));

      // Restore before note start (if within our range)
      if (rangeStartSample < noteStartSample) {
        const int silenceEnd = std::min(noteStartSample, totalSamples);
        for (int i = std::max(0, rangeStartSample); i < silenceEnd; ++i) {
          if (i < origSamples)
            dst[i] = srcOrig[i];
        }
      }

      // Restore after note end (if within our range)
      if (rangeEndSample > noteEndSample) {
        const int silenceStart = std::max(0, noteEndSample);
        const int silenceEnd = std::min(rangeEndSample, totalSamples);
        for (int i = silenceStart; i < silenceEnd; ++i) {
          if (i < origSamples)
            dst[i] = srcOrig[i];
        }
      }
    }
  }

  const int f0Size = static_cast<int>(audioData.f0.size());
  int smoothStart = std::max(0, rangeStart - 60);
  int smoothEnd = std::min(f0Size, rangeEnd + 60);
  project->setF0DirtyRange(smoothStart, smoothEnd);

  if (owner_.onPitchEditFinished)
    owner_.onPitchEditFinished();

  stretchDrag = {};
}

void StretchHandler::cancelStretchDrag() {
  auto *project = owner_.project;
  if (!stretchDrag.active || !project) {
    stretchDrag = {};
    return;
  }

  auto &audioData = project->getAudioData();
  const int totalFrames = static_cast<int>(audioData.deltaPitch.size());
  int rangeStart = std::clamp(stretchDrag.rangeStartFull, 0, totalFrames);
  int rangeEnd = std::clamp(stretchDrag.rangeEndFull, 0, totalFrames);

  if (rangeEnd > rangeStart &&
      stretchDrag.originalDeltaRangeFull.size() ==
          static_cast<size_t>(rangeEnd - rangeStart)) {
    for (int i = rangeStart; i < rangeEnd; ++i)
      audioData.deltaPitch[static_cast<size_t>(i)] =
          stretchDrag.originalDeltaRangeFull[static_cast<size_t>(
              i - rangeStart)];
  }

  if (rangeEnd > rangeStart &&
      stretchDrag.originalVoicedRangeFull.size() ==
          static_cast<size_t>(rangeEnd - rangeStart)) {
    for (int i = rangeStart; i < rangeEnd; ++i)
      audioData.voicedMask[static_cast<size_t>(i)] =
          stretchDrag.originalVoicedRangeFull[static_cast<size_t>(
              i - rangeStart)];
  }

  if (!stretchDrag.originalMelRangeFull.empty() && rangeStart < rangeEnd &&
      audioData.melSpectrogram.size() >=
          static_cast<size_t>(
              rangeStart + stretchDrag.originalMelRangeFull.size())) {
    for (size_t i = 0; i < stretchDrag.originalMelRangeFull.size(); ++i)
      audioData.melSpectrogram[static_cast<size_t>(rangeStart) + i] =
          stretchDrag.originalMelRangeFull[i];
  }

  if (stretchDrag.boundary.left) {
    stretchDrag.boundary.left->setStartFrame(stretchDrag.originalLeftStart);
    stretchDrag.boundary.left->setEndFrame(stretchDrag.originalLeftEnd);
    stretchDrag.boundary.left->markDirty();
    if (!stretchDrag.originalLeftClip.empty())
      stretchDrag.boundary.left->setClipWaveform(
          stretchDrag.originalLeftClip);
  }
  if (stretchDrag.boundary.right) {
    stretchDrag.boundary.right->setStartFrame(
        stretchDrag.originalRightStart);
    stretchDrag.boundary.right->setEndFrame(stretchDrag.originalRightEnd);
    stretchDrag.boundary.right->markDirty();
    if (!stretchDrag.originalRightClip.empty())
      stretchDrag.boundary.right->setClipWaveform(
          stretchDrag.originalRightClip);
  }

  PitchCurveProcessor::rebuildBaseFromNotes(*project);
  owner_.invalidateBasePitchCache();

  if (owner_.onPitchEdited)
    owner_.onPitchEdited();

  stretchDrag = {};
}
