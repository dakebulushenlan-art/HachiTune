#include "IncrementalSynthesizer.h"
#include "../TensionProcessor.h"
#include "../../Utils/Localization.h"
#include "../../Utils/MelSpectrogram.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

IncrementalSynthesizer::IncrementalSynthesizer() = default;

IncrementalSynthesizer::~IncrementalSynthesizer() { cancel(); }

void IncrementalSynthesizer::cancel() {
  if (cancelFlag)
    cancelFlag->store(true);
}

// ---------------------------------------------------------------------------
// computeSynthesisRange: find voiced segments overlapping dirty range,
// expand to include complete segments + padding.
// ---------------------------------------------------------------------------
std::pair<int, int>
IncrementalSynthesizer::computeSynthesisRange(int dirtyStart, int dirtyEnd) {
  if (!project)
    return {dirtyStart, dirtyEnd};

  auto &voicedMask = project->getAudioData().voicedMask;
  const int totalFrames = static_cast<int>(voicedMask.size());
  if (totalFrames == 0)
    return {dirtyStart, dirtyEnd};

  dirtyStart = std::max(0, dirtyStart);
  dirtyEnd = std::min(totalFrames, dirtyEnd);

  // Give the vocoder enough temporal context to stabilize local phase when
  // doing chunked re-synthesis.
  constexpr int kPadFrames = 24;
  // Bridge short UV gaps so adjacent notes around consonants are synthesized
  // together; this avoids junction phase resets between neighboring notes.
  constexpr int kGapBridgeFrames = 16;

  auto isVoiced = [&](int idx) -> bool {
    return idx >= 0 && idx < totalFrames && static_cast<bool>(voicedMask[idx]);
  };

  // Expand backward to include neighboring voiced segments across short gaps.
  int start = dirtyStart;
  int backGap = 0;
  while (start > 0) {
    if (isVoiced(start - 1)) {
      --start;
      backGap = 0;
      continue;
    }
    if (backGap < kGapBridgeFrames) {
      --start;
      ++backGap;
      continue;
    }
    break;
  }
  start = std::max(0, start - kPadFrames);

  // Expand forward to include neighboring voiced segments across short gaps.
  int end = dirtyEnd;
  int fwdGap = 0;
  while (end < totalFrames) {
    if (isVoiced(end)) {
      ++end;
      fwdGap = 0;
      continue;
    }
    if (fwdGap < kGapBridgeFrames) {
      ++end;
      ++fwdGap;
      continue;
    }
    break;
  }
  end = std::min(totalFrames, end + kPadFrames);

  return {start, end};
}

// ---------------------------------------------------------------------------
// generateBlendMask: per-sample blend factor from voicedMask.
// 1.0 = synthesized, 0.0 = original, smooth ramps at transitions.
// ---------------------------------------------------------------------------
std::vector<float>
IncrementalSynthesizer::generateBlendMask(int startFrame, int endFrame,
                                          int hopSize) {
  auto &voicedMask = project->getAudioData().voicedMask;
  const int totalFrames = static_cast<int>(voicedMask.size());
  const int numFrames = endFrame - startFrame;
  const int numSamples = numFrames * hopSize;

  // Step 1: stability-first frame mask.
  // Default to synthesized audio in the whole region to avoid internal
  // orig/synth combing artifacts at note junctions.
  std::vector<float> frameMask(numFrames, 1.0f);

  // Keep original audio only for long unvoiced runs (e.g. clear breaths/silence),
  // not for short UV gaps between notes.
  constexpr int kKeepOriginalUnvoicedFrames = 24;
  if (numFrames > 0 && totalFrames > 0) {
    int i = 0;
    while (i < numFrames) {
      const int gf = startFrame + i;
      const bool voiced =
          gf >= 0 && gf < totalFrames && static_cast<bool>(voicedMask[gf]);
      if (voiced) {
        ++i;
        continue;
      }

      const int runStart = i;
      while (i < numFrames) {
        const int g = startFrame + i;
        const bool v =
            g >= 0 && g < totalFrames && static_cast<bool>(voicedMask[g]);
        if (v)
          break;
        ++i;
      }
      const int runEnd = i;
      const int runLen = runEnd - runStart;
      if (runLen >= kKeepOriginalUnvoicedFrames) {
        for (int k = runStart; k < runEnd; ++k)
          frameMask[k] = 0.0f;
      }
    }
  }

  // Step 2: expand to per-sample (sample-and-hold)
  std::vector<float> mask(numSamples, 0.0f);
  for (int i = 0; i < numFrames; ++i) {
    int ss = i * hopSize;
    int se = std::min(ss + hopSize, numSamples);
    for (int s = ss; s < se; ++s)
      mask[s] = frameMask[i];
  }

  // Step 3: smooth transitions with linear ramp at frame boundaries
  constexpr int kMinRampSamples = 512;
  const int kRampSamples = std::max(kMinRampSamples, hopSize * 2);
  for (int i = 0; i < numFrames - 1; ++i) {
    if (frameMask[i] == frameMask[i + 1])
      continue;
    // Transition at frame boundary
    int center = (i + 1) * hopSize;
    int rampStart = std::max(0, center - kRampSamples / 2);
    int rampEnd = std::min(numSamples, center + kRampSamples / 2);
    float fromVal = frameMask[i];
    float toVal = frameMask[i + 1];
    for (int s = rampStart; s < rampEnd; ++s) {
      float t = static_cast<float>(s - rampStart) /
                static_cast<float>(rampEnd - rampStart);
      mask[s] = fromVal + (toVal - fromVal) * t;
    }
  }

  return mask;
}

// ---------------------------------------------------------------------------
// synthesizeRegion: Voiced-Only Blend approach.
// ---------------------------------------------------------------------------
void IncrementalSynthesizer::synthesizeRegion(ProgressCallback onProgress,
                                              CompleteCallback onComplete) {
  if (!project || !vocoder) {
    if (onComplete)
      onComplete(false);
    return;
  }

  auto &audioData = project->getAudioData();
  if (audioData.melSpectrogram.empty() || audioData.f0.empty()) {
    if (onComplete)
      onComplete(false);
    return;
  }

  if (!vocoder->isLoaded()) {
    if (onComplete)
      onComplete(false);
    return;
  }

  if (!project->hasDirtyNotes() && !project->hasF0DirtyRange() &&
      !project->hasParamDirtyRange()) {
    if (onComplete)
      onComplete(false);
    return;
  }

  auto [dirtyStart, dirtyEnd] = project->getDirtyFrameRange();
  if (dirtyStart < 0 || dirtyEnd < 0) {
    if (onComplete)
      onComplete(false);
    return;
  }

  // Compute synthesis range (voiced segments + padding)
  auto [startFrame, endFrame] = computeSynthesisRange(dirtyStart, dirtyEnd);
  startFrame = std::max(0, startFrame);
  endFrame =
      std::min(static_cast<int>(audioData.melSpectrogram.size()), endFrame);

  if (startFrame >= endFrame) {
    if (onComplete)
      onComplete(false);
    return;
  }

  // Generate blend mask before async call (voicedMask is stable here)
  int hopSize = vocoder->getHopSize();
  std::vector<float> blendMask = generateBlendMask(startFrame, endFrame, hopSize);

  // Early exit: if blend mask is all-zero, nothing to synthesize
  bool hasVoiced = std::any_of(blendMask.begin(), blendMask.end(),
                               [](float v) { return v > 0.0f; });
  if (!hasVoiced) {
    project->clearAllDirty();
    if (onComplete)
      onComplete(true);
    return;
  }

  // Copy original waveform segment for blending
  const auto &origWaveform = audioData.originalWaveform.getNumSamples() > 0
                                 ? audioData.originalWaveform
                                 : audioData.waveform;
  int startSample = startFrame * hopSize;
  int numSynthSamples = (endFrame - startFrame) * hopSize;
  int totalOrigSamples = origWaveform.getNumSamples();

  std::vector<float> originalSegment(numSynthSamples, 0.0f);
  {
    const float *origPtr = origWaveform.getReadPointer(0);
    int copyLen = std::min(numSynthSamples,
                           std::max(0, totalOrigSamples - startSample));
    if (copyLen > 0 && startSample >= 0)
      std::copy(origPtr + startSample, origPtr + startSample + copyLen,
                originalSegment.begin());
  }

  // ---------------------------------------------------------------------------
  // HNSep tension processing: replace originalSegment slices for notes that
  // have voicing/breath/tension curves and harmonic/noise clip waveforms.
  // This produces a tension-adjusted "original" signal for blending and also
  // allows recomputing mel spectrograms for the vocoder input.
  // ---------------------------------------------------------------------------
  bool hasAnyHNSepCurves = false;
  {
    const auto &harmonicBuf = audioData.harmonicWaveform;
    const auto &noiseBuf = audioData.noiseWaveform;
    const bool hasGlobalHNSep = harmonicBuf.getNumSamples() > 0
                             && noiseBuf.getNumSamples() > 0;

    if (hasGlobalHNSep) {
      TensionProcessor tensionProc;
      const float *harmonicPtr = harmonicBuf.getReadPointer(0);
      const float *noisePtr = noiseBuf.getReadPointer(0);
      const int totalHarmonicSamples = harmonicBuf.getNumSamples();
      const int totalNoiseSamples = noiseBuf.getNumSamples();

      for (const auto &note : project->getNotes()) {
        if (note.isRest())
          continue;

        // Check if note has any non-default hnsep curves
        const bool hasVoicing = note.hasVoicingCurve();
        const bool hasBreath = note.hasBreathCurve();
        const bool hasTension = note.hasTensionCurve();
        if (!hasVoicing && !hasBreath && !hasTension)
          continue;

        const int noteStart = note.getStartFrame();
        const int noteEnd = note.getEndFrame();
        const int overlapStart = std::max(startFrame, noteStart);
        const int overlapEnd = std::min(endFrame, noteEnd);
        if (overlapEnd <= overlapStart)
          continue;

        hasAnyHNSepCurves = true;

        // Process each frame in the overlap
        const auto &voicingCurve = note.getVoicingCurve();
        const auto &breathCurve = note.getBreathCurve();
        const auto &tensionCurve = note.getTensionCurve();
        const int noteDurFrames = noteEnd - noteStart;

        for (int frame = overlapStart; frame < overlapEnd; ++frame) {
          const int noteLocalFrame = frame - noteStart;
          if (noteLocalFrame < 0 || noteLocalFrame >= noteDurFrames)
            continue;

          // Get per-frame parameter values (default if curve absent/short)
          const float voicingPct = (hasVoicing && noteLocalFrame < static_cast<int>(voicingCurve.size()))
              ? voicingCurve[noteLocalFrame] : 100.0f;
          const float breathPct = (hasBreath && noteLocalFrame < static_cast<int>(breathCurve.size()))
              ? breathCurve[noteLocalFrame] : 100.0f;
          const float tensionVal = (hasTension && noteLocalFrame < static_cast<int>(tensionCurve.size()))
              ? tensionCurve[noteLocalFrame] : 0.0f;

          // Skip frames with default values (no processing needed)
          if (std::abs(voicingPct - 100.0f) < 0.01f
              && std::abs(breathPct - 100.0f) < 0.01f
              && std::abs(tensionVal) < 0.01f)
            continue;

          // Frame sample range in global coordinates
          const int frameSampleStart = frame * hopSize;
          const int frameSampleEnd = frameSampleStart + hopSize;

          // Map to local originalSegment coordinates
          const int localStart = std::max(0, frameSampleStart - startSample);
          const int localEnd = std::min(numSynthSamples, frameSampleEnd - startSample);
          if (localEnd <= localStart)
            continue;

          const int frameNumSamples = localEnd - localStart;

          // Extract harmonic and noise samples for this frame
          std::vector<float> harmonicFrame(frameNumSamples, 0.0f);
          std::vector<float> noiseFrame(frameNumSamples, 0.0f);

          for (int i = 0; i < frameNumSamples; ++i) {
            const int globalSample = (localStart + startSample) + i;
            if (globalSample >= 0 && globalSample < totalHarmonicSamples)
              harmonicFrame[i] = harmonicPtr[globalSample];
            if (globalSample >= 0 && globalSample < totalNoiseSamples)
              noiseFrame[i] = noisePtr[globalSample];
          }

          // Apply tension processing
          tensionProc.processInPlace(
              originalSegment.data() + localStart,
              harmonicFrame.data(), noiseFrame.data(),
              frameNumSamples,
              voicingPct, breathPct, tensionVal);
        }
      }
    }
  }

  // Extract mel + adjusted F0
  // If any notes had HNSep curves, recompute mel spectrograms from the
  // tension-adjusted originalSegment so the vocoder receives adjusted input.
  std::vector<std::vector<float>> melRange;
  if (hasAnyHNSepCurves) {
    MelSpectrogram melComputer(audioData.sampleRate);
    melRange = melComputer.compute(originalSegment.data(), numSynthSamples);
    // Ensure frame count matches expected range
    const int expectedFrames = endFrame - startFrame;
    if (static_cast<int>(melRange.size()) > expectedFrames)
      melRange.resize(expectedFrames);
    else {
      while (static_cast<int>(melRange.size()) < expectedFrames) {
        // Pad with empty frames if mel computation produces fewer
        int numMels = melRange.empty() ? 128 : static_cast<int>(melRange.front().size());
        melRange.emplace_back(numMels, 0.0f);
      }
    }
  } else {
    melRange.assign(
        audioData.melSpectrogram.begin() + startFrame,
        audioData.melSpectrogram.begin() + endFrame);
  }
  std::vector<float> adjustedF0Range =
      project->getAdjustedF0ForRange(startFrame, endFrame);

  if (melRange.empty() || adjustedF0Range.empty()) {
    if (onComplete)
      onComplete(false);
    return;
  }

  if (onProgress)
    onProgress(TR("progress.synthesizing"));

  // Cancel previous job
  if (cancelFlag)
    cancelFlag->store(true);
  cancelFlag = std::make_shared<std::atomic<bool>>(false);
  uint64_t currentJobId = ++jobId;
  isBusy = true;

  int capturedStartFrame = startFrame;
  int capturedEndFrame = endFrame;
  auto capturedCancelFlag = cancelFlag;
  auto capturedProject = project;


  vocoder->inferAsync(
      std::move(melRange), std::move(adjustedF0Range),
      [this, capturedCancelFlag, capturedProject, capturedStartFrame,
       capturedEndFrame, hopSize, currentJobId, onComplete,
       blendMask = std::move(blendMask),
       originalSegment = std::move(originalSegment)](
          std::vector<float> synthesizedAudio) {
        if (currentJobId != jobId.load())
          return;
        if (capturedCancelFlag->load()) {
          isBusy = false;
          if (onComplete)
            onComplete(false);
          return;
        }
        if (synthesizedAudio.empty()) {
          isBusy = false;
          if (onComplete)
            onComplete(false);
          return;
        }

        std::thread([this, capturedCancelFlag, capturedProject,
                     capturedStartFrame, capturedEndFrame, hopSize,
                     currentJobId, onComplete, blendMask, originalSegment,
                     synthesizedAudio = std::move(synthesizedAudio)]() mutable {
          if (currentJobId != jobId.load())
            return;
          if (capturedCancelFlag->load()) {
            isBusy = false;
            if (onComplete)
              juce::MessageManager::callAsync(
                  [onComplete]() { onComplete(false); });
            return;
          }

          auto &audioData = capturedProject->getAudioData();
          int totalSamples = audioData.waveform.getNumSamples();
          int startSample = capturedStartFrame * hopSize;
          int expectedSamples =
              (capturedEndFrame - capturedStartFrame) * hopSize;

          if (expectedSamples <= 0) {
            isBusy = false;
            if (onComplete)
              juce::MessageManager::callAsync(
                  [onComplete]() { onComplete(false); });
            return;
          }

          // Resize synthesized audio to match expected
          synthesizedAudio.resize(static_cast<size_t>(expectedSamples), 0.0f);

          int samplesToWrite =
              std::min(expectedSamples, totalSamples - startSample);
          if (samplesToWrite <= 0) {
            isBusy = false;
            if (onComplete)
              juce::MessageManager::callAsync(
                  [onComplete]() { onComplete(false); });
            return;
          }

          // Build blended target from model/original.
          std::vector<float> targetSegment(samplesToWrite, 0.0f);
          for (int i = 0; i < samplesToWrite; ++i) {
            const float b =
                (i < static_cast<int>(blendMask.size())) ? blendMask[i] : 0.0f;
            const float synth = synthesizedAudio[static_cast<size_t>(i)];
            const float orig = originalSegment[static_cast<size_t>(i)];
            targetSegment[static_cast<size_t>(i)] =
                b * synth + (1.0f - b) * orig;
          }

          // Apply per-note gain on top of the blended target.
          std::vector<float> sampleGain(static_cast<size_t>(samplesToWrite),
                                        1.0f);
          for (const auto &note : capturedProject->getNotes()) {
            if (note.isRest())
              continue;
            if (std::abs(note.getVolumeDb()) < 0.001f)
              continue;

            const int noteStart = note.getStartFrame();
            const int noteEnd = note.getEndFrame();
            const int overlapStart = std::max(capturedStartFrame, noteStart);
            const int overlapEnd = std::min(capturedEndFrame, noteEnd);
            if (overlapEnd <= overlapStart)
              continue;

            const int localStart = (overlapStart - capturedStartFrame) * hopSize;
            const int localEnd = (overlapEnd - capturedStartFrame) * hopSize;
            if (localStart >= samplesToWrite)
              continue;

            const float gain =
                juce::Decibels::decibelsToGain(note.getVolumeDb(), -60.0f);
            const int clampedStart = std::max(0, localStart);
            const int clampedEnd = std::min(samplesToWrite, localEnd);
            for (int i = clampedStart; i < clampedEnd; ++i) {
              sampleGain[static_cast<size_t>(i)] *= gain;
            }
          }
          for (int i = 0; i < samplesToWrite; ++i) {
            targetSegment[static_cast<size_t>(i)] *=
                sampleGain[static_cast<size_t>(i)];
          }

          // Distribute synthesized audio into per-note synthWaveforms.
          // Each note gets the slice of targetSegment corresponding to its
          // output frame range [startFrame, endFrame), PLUS margin samples on
          // each side so that composeGlobalWaveform() can crossfade with real
          // audio instead of held-value extrapolation at note boundaries.
          constexpr int kSynthMarginSamples = 256; // margin each side

          for (auto &note : capturedProject->getNotes()) {
            if (note.isRest())
              continue;

            const int noteStart = note.getStartFrame();
            const int noteEnd = note.getEndFrame();
            const int overlapStart = std::max(capturedStartFrame, noteStart);
            const int overlapEnd = std::min(capturedEndFrame, noteEnd);
            if (overlapEnd <= overlapStart)
              continue;

            // Only update notes that overlap the synthesis range and are dirty
            // (or have no synthWaveform yet).
            // Also resynthesize notes that overlap the param dirty range,
            // since parameter curve edits (voicing/breath/tension) affect
            // the tension-adjusted waveform fed to the vocoder.
            bool paramDirtyOverlap = false;
            if (capturedProject->hasParamDirtyRange()) {
              auto [pStart, pEnd] = capturedProject->getParamDirtyRange();
              paramDirtyOverlap = (note.getStartFrame() < pEnd &&
                                   note.getEndFrame() > pStart);
            }
            if (!note.isDirty() && !note.isSynthDirty() &&
                !paramDirtyOverlap && note.hasSynthWaveform())
              continue;

            // Full note range in samples (the "body")
            const int noteStartSample = noteStart * hopSize;
            const int noteEndSample = noteEnd * hopSize;
            const int noteSamples = noteEndSample - noteStartSample;
            if (noteSamples <= 0)
              continue;

            // Compute margin: how far we can extend into targetSegment
            // beyond the note's body on each side.
            const int targetStartSample = capturedStartFrame * hopSize;
            const int targetEndSample = targetStartSample + samplesToWrite;

            // Left margin: extend before noteStartSample
            const int leftMarginAvail = noteStartSample - targetStartSample;
            const int leftMargin = std::max(0, std::min(kSynthMarginSamples, leftMarginAvail));

            // Right margin: extend after noteEndSample
            const int rightMarginAvail = targetEndSample - noteEndSample;
            const int rightMargin = std::max(0, std::min(kSynthMarginSamples, rightMarginAvail));

            // Total synth vector: [preroll | body | postroll]
            const int totalSynthLen = leftMargin + noteSamples + rightMargin;
            std::vector<float> noteSynth(static_cast<size_t>(totalSynthLen), 0.0f);

            // Copy from targetSegment: the extended region
            // [noteStartSample - leftMargin, noteEndSample + rightMargin) in global coords
            // maps to targetSegment[(noteStartSample - leftMargin - targetStartSample) ..]
            const int extGlobalStart = noteStartSample - leftMargin;
            const int extLocalSrc = extGlobalStart - targetStartSample;

            // The overlap between [extGlobalStart, noteEndSample+rightMargin) and
            // [capturedStartFrame*hopSize, capturedStartFrame*hopSize + samplesToWrite)
            // determines what we can actually copy from targetSegment.
            const int copyStart = std::max(0, extLocalSrc);
            const int copyEnd = std::min(samplesToWrite,
                extLocalSrc + totalSynthLen);
            const int dstOffset = copyStart - extLocalSrc;

            for (int i = copyStart; i < copyEnd; ++i) {
              const int dstIdx = dstOffset + (i - copyStart);
              if (dstIdx >= 0 && dstIdx < totalSynthLen) {
                noteSynth[static_cast<size_t>(dstIdx)] =
                    targetSegment[static_cast<size_t>(i)];
              }
            }

            // For parts of the note body outside the synthesis range, use srcClipWaveform
            // with optional HNSep tension processing applied.
            if (note.hasSrcClipWaveform()) {
              const auto &srcClip = note.getSrcClipWaveform();
              const int srcFrames = note.getSrcEndFrame() - note.getSrcStartFrame();
              const int dstFrames = note.getEndFrame() - note.getStartFrame();
              const int srcSamples = static_cast<int>(srcClip.size());

              // Check if this note has HNSep curves for tension adjustment
              const bool noteHasVoicing = note.hasVoicingCurve();
              const bool noteHasBreath = note.hasBreathCurve();
              const bool noteHasTension = note.hasTensionCurve();
              const bool noteHasHNSep = noteHasVoicing || noteHasBreath || noteHasTension;

              // Get global harmonic/noise buffers for tension processing
              const auto &audioData = capturedProject->getAudioData();
              const bool hasGlobalHNSep = audioData.harmonicWaveform.getNumSamples() > 0
                                       && audioData.noiseWaveform.getNumSamples() > 0;

              for (int i = 0; i < noteSamples; ++i) {
                const int globalSample = noteStartSample + i;
                const int globalFrame = (globalSample / hopSize);
                // Skip samples already covered by synthesis
                if (globalFrame >= overlapStart && globalFrame < overlapEnd)
                  continue;

                // Map destination sample to source sample (handle stretch)
                float srcPos;
                if (dstFrames > 0 && srcFrames > 0) {
                  srcPos = static_cast<float>(i) * static_cast<float>(srcSamples) /
                           static_cast<float>(noteSamples);
                } else {
                  srcPos = static_cast<float>(i);
                }
                int srcIdx = static_cast<int>(srcPos);
                if (srcIdx >= 0 && srcIdx < srcSamples) {
                  float sampleVal = srcClip[static_cast<size_t>(srcIdx)];

                  // Apply per-frame HNSep tension adjustment if curves present
                  if (noteHasHNSep && hasGlobalHNSep) {
                    const int noteLocalFrame = globalFrame - noteStart;
                    const int noteDurFrames = noteEnd - noteStart;
                    if (noteLocalFrame >= 0 && noteLocalFrame < noteDurFrames) {
                      const auto &voicingCurve = note.getVoicingCurve();
                      const auto &breathCurve = note.getBreathCurve();
                      const auto &tensionCurve = note.getTensionCurve();

                      const float vPct = (noteHasVoicing && noteLocalFrame < static_cast<int>(voicingCurve.size()))
                          ? voicingCurve[noteLocalFrame] : 100.0f;
                      const float bPct = (noteHasBreath && noteLocalFrame < static_cast<int>(breathCurve.size()))
                          ? breathCurve[noteLocalFrame] : 100.0f;
                      const float tVal = (noteHasTension && noteLocalFrame < static_cast<int>(tensionCurve.size()))
                          ? tensionCurve[noteLocalFrame] : 0.0f;

                      // Simple voicing/breath mixing for single-sample (no STFT for tension here)
                      if (std::abs(vPct - 100.0f) > 0.01f || std::abs(bPct - 100.0f) > 0.01f) {
                        const float *harmonicPtr = audioData.harmonicWaveform.getReadPointer(0);
                        const float *noisePtr = audioData.noiseWaveform.getReadPointer(0);
                        const int totalH = audioData.harmonicWaveform.getNumSamples();
                        const int totalN = audioData.noiseWaveform.getNumSamples();
                        const float h = (globalSample >= 0 && globalSample < totalH)
                            ? harmonicPtr[globalSample] : 0.0f;
                        const float n = (globalSample >= 0 && globalSample < totalN)
                            ? noisePtr[globalSample] : 0.0f;
                        sampleVal = (bPct / 100.0f) * n + (vPct / 100.0f) * h;
                      }
                      (void)tVal; // Tension requires STFT; applied in bulk pass above
                    }
                  }

                  // Body samples start at offset leftMargin in noteSynth
                  noteSynth[static_cast<size_t>(leftMargin + i)] = sampleVal;
                }
              }
            }

            note.setSynthWaveform(std::move(noteSynth), leftMargin);
          }

          // Compose the global waveform from per-note synthWaveforms
          capturedProject->composeGlobalWaveform();

          isBusy = false;
          juce::MessageManager::callAsync(
              [capturedProject, onComplete]() {
                capturedProject->clearAllDirty();
                if (onComplete) onComplete(true);
              });
        }).detach();
      });
}
