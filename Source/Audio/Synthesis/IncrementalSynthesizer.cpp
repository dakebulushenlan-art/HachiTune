#include "IncrementalSynthesizer.h"
#include "../../Utils/Localization.h"
#include <algorithm>

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

  constexpr int kPadFrames = 3;

  // Expand backward to include the complete voiced segment at dirtyStart
  int start = dirtyStart;
  while (start > 0 && static_cast<bool>(voicedMask[start - 1]))
    --start;
  start = std::max(0, start - kPadFrames);

  // Expand forward to include the complete voiced segment at dirtyEnd
  int end = dirtyEnd;
  while (end < totalFrames && static_cast<bool>(voicedMask[end]))
    ++end;
  end = std::min(totalFrames, end + kPadFrames);

  DBG("computeSynthesisRange: [" << dirtyStart << ", " << dirtyEnd
                                  << "] -> [" << start << ", " << end << "]");
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

  // Step 1: per-frame binary mask
  std::vector<float> frameMask(numFrames, 0.0f);
  for (int i = 0; i < numFrames; ++i) {
    int gf = startFrame + i;
    if (gf >= 0 && gf < totalFrames && static_cast<bool>(voicedMask[gf]))
      frameMask[i] = 1.0f;
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
  constexpr int kRampSamples = 128; // ~3ms at 44.1kHz
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

  if (!project->hasDirtyNotes() && !project->hasF0DirtyRange()) {
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

  // Extract mel + adjusted F0
  std::vector<std::vector<float>> melRange(
      audioData.melSpectrogram.begin() + startFrame,
      audioData.melSpectrogram.begin() + endFrame);
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

  DBG("synthesizeRegion: frames [" << startFrame << ", " << endFrame << "]");

  vocoder->inferAsync(
      melRange, adjustedF0Range,
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
          int numChannels = audioData.waveform.getNumChannels();
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

          constexpr int kBoundaryXfadeSamples = 256;
          const int xfadeLen =
              std::min(kBoundaryXfadeSamples, samplesToWrite / 2);

          // Save boundary samples from current waveform for edge crossfade
          std::vector<std::vector<float>> oldStart(
              numChannels, std::vector<float>(xfadeLen));
          std::vector<std::vector<float>> oldEnd(
              numChannels, std::vector<float>(xfadeLen));
          for (int ch = 0; ch < numChannels; ++ch) {
            const float *src = audioData.waveform.getReadPointer(ch);
            for (int i = 0; i < xfadeLen; ++i) {
              oldStart[ch][i] = src[startSample + i];
              oldEnd[ch][i] =
                  src[startSample + samplesToWrite - xfadeLen + i];
            }
          }

          // === CORE BLEND ===
          // output = blend * synthesized + (1 - blend) * original
          for (int ch = 0; ch < numChannels; ++ch) {
            float *dst = audioData.waveform.getWritePointer(ch);
            for (int i = 0; i < samplesToWrite; ++i) {
              float b = blendMask[i];
              float synth = synthesizedAudio[i];
              float orig = originalSegment[i];
              dst[startSample + i] = b * synth + (1.0f - b) * orig;
            }
          }

          // === BOUNDARY CROSSFADE ===
          for (int ch = 0; ch < numChannels; ++ch) {
            float *dst = audioData.waveform.getWritePointer(ch);
            // Start: old → blended
            for (int i = 0; i < xfadeLen; ++i) {
              float t =
                  static_cast<float>(i) / static_cast<float>(xfadeLen);
              dst[startSample + i] =
                  oldStart[ch][i] * (1.0f - t) + dst[startSample + i] * t;
            }
            // End: blended → old
            int endOff = startSample + samplesToWrite - xfadeLen;
            for (int i = 0; i < xfadeLen; ++i) {
              float t =
                  static_cast<float>(i) / static_cast<float>(xfadeLen);
              dst[endOff + i] =
                  dst[endOff + i] * (1.0f - t) + oldEnd[ch][i] * t;
            }
          }

          DBG("synthesizeRegion: blended " << samplesToWrite
                                            << " samples at " << startSample);

          capturedProject->clearAllDirty();
          isBusy = false;
          if (onComplete)
            juce::MessageManager::callAsync(
                [onComplete]() { onComplete(true); });
        }).detach();
      });
}