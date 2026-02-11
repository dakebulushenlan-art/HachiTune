#include "IncrementalSynthesizer.h"
#include "../../Utils/Localization.h"
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace {
std::vector<float> makeDenseF0ForInference(const std::vector<float> &f0) {
  if (f0.empty())
    return {};

  std::vector<float> dense(f0);
  const int n = static_cast<int>(dense.size());

  auto isVoiced = [&](int i) { return dense[static_cast<size_t>(i)] > 0.0f; };

  int lastVoiced = -1;
  int i = 0;
  while (i < n) {
    if (isVoiced(i)) {
      lastVoiced = i;
      ++i;
      continue;
    }

    const int gapStart = i;
    while (i < n && !isVoiced(i))
      ++i;
    const int gapEnd = i; // [gapStart, gapEnd)

    const int nextVoiced = (gapEnd < n) ? gapEnd : -1;
    const float prevVal =
        (lastVoiced >= 0) ? dense[static_cast<size_t>(lastVoiced)] : 0.0f;
    const float nextVal =
        (nextVoiced >= 0) ? dense[static_cast<size_t>(nextVoiced)] : 0.0f;

    if (prevVal <= 0.0f && nextVal <= 0.0f)
      continue;

    if (prevVal <= 0.0f) {
      for (int k = gapStart; k < gapEnd; ++k)
        dense[static_cast<size_t>(k)] = nextVal;
      continue;
    }

    if (nextVal <= 0.0f) {
      for (int k = gapStart; k < gapEnd; ++k)
        dense[static_cast<size_t>(k)] = prevVal;
      continue;
    }

    const float logA = std::log(prevVal);
    const float logB = std::log(nextVal);
    const float denom = static_cast<float>(nextVoiced - lastVoiced);
    for (int k = gapStart; k < gapEnd; ++k) {
      const float t = static_cast<float>(k - lastVoiced) / denom;
      dense[static_cast<size_t>(k)] =
          std::exp(logA * (1.0f - t) + logB * t);
    }
  }

  return dense;
}
} // namespace

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
  constexpr int kGapBridgeFrames = 8;

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
    const bool voiced =
        gf >= 0 && gf < totalFrames && static_cast<bool>(voicedMask[gf]);
    if (voiced)
      frameMask[i] = 1.0f;
  }

  // Remove 1-frame toggles in uv mask to avoid zipper-like stitching artifacts.
  if (numFrames >= 3) {
    std::vector<float> cleaned(frameMask);
    for (int i = 1; i < numFrames - 1; ++i) {
      const float prev = frameMask[i - 1];
      const float cur = frameMask[i];
      const float next = frameMask[i + 1];
      if (cur == 0.0f && prev == 1.0f && next == 1.0f)
        cleaned[i] = 1.0f;
      else if (cur == 1.0f && prev == 0.0f && next == 0.0f)
        cleaned[i] = 0.0f;
    }
    frameMask.swap(cleaned);
  }

  // Bridge short unvoiced holes between voiced regions to keep local phase
  // continuity at note junctions (especially consonant transitions).
  constexpr int kBlendBridgeFrames = 4;
  if (numFrames >= 3) {
    int i = 0;
    while (i < numFrames) {
      if (frameMask[i] > 0.0f) {
        ++i;
        continue;
      }

      const int gapStart = i;
      while (i < numFrames && frameMask[i] <= 0.0f)
        ++i;
      const int gapEnd = i; // [gapStart, gapEnd)
      const int gapLen = gapEnd - gapStart;
      const bool leftVoiced = gapStart > 0 && frameMask[gapStart - 1] > 0.0f;
      const bool rightVoiced = gapEnd < numFrames && frameMask[gapEnd] > 0.0f;

      if (leftVoiced && rightVoiced && gapLen <= kBlendBridgeFrames) {
        for (int k = gapStart; k < gapEnd; ++k)
          frameMask[k] = 1.0f;
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
  std::vector<float> denseF0Range = makeDenseF0ForInference(adjustedF0Range);

  if (melRange.empty() || adjustedF0Range.empty() || denseF0Range.empty()) {
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
      melRange, denseF0Range,
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

          constexpr int kMinBoundaryBlendSamples = 512;
          constexpr int kMaxBoundaryBlendSamples = 4096;
          const int preferredBlend =
              std::max(kMinBoundaryBlendSamples, hopSize * 2);
          const int boundaryBlendLen = std::min(
              std::min(kMaxBoundaryBlendSamples, preferredBlend),
              samplesToWrite / 2);

          // Snapshot current waveform segment so the replacement can be
          // stitched back with guaranteed edge continuity.
          std::vector<std::vector<float>> currentSegment(
              numChannels, std::vector<float>(samplesToWrite, 0.0f));
          for (int ch = 0; ch < numChannels; ++ch) {
            const float *src = audioData.waveform.getReadPointer(ch);
            std::copy(src + startSample, src + startSample + samplesToWrite,
                      currentSegment[ch].begin());
          }

          // Build target from model/original blend once.
          std::vector<float> targetSegment(samplesToWrite, 0.0f);
          for (int i = 0; i < samplesToWrite; ++i) {
            const float b =
                (i < static_cast<int>(blendMask.size())) ? blendMask[i] : 0.0f;
            const float synth = synthesizedAudio[static_cast<size_t>(i)];
            const float orig = originalSegment[static_cast<size_t>(i)];
            targetSegment[static_cast<size_t>(i)] =
                b * synth + (1.0f - b) * orig;
          }

          // Stitch target into current waveform with a smooth edge envelope:
          // final = current + edgeEnv * (target - current)
          for (int ch = 0; ch < numChannels; ++ch) {
            float *dst = audioData.waveform.getWritePointer(ch);
            for (int i = 0; i < samplesToWrite; ++i) {
              float edgeEnv = 1.0f;
              if (boundaryBlendLen > 1) {
                if (i < boundaryBlendLen) {
                  const float t = static_cast<float>(i) /
                                  static_cast<float>(boundaryBlendLen - 1);
                  edgeEnv = t * t * (3.0f - 2.0f * t);
                } else if (i >= samplesToWrite - boundaryBlendLen) {
                  const int fromEnd = samplesToWrite - 1 - i;
                  const float t = static_cast<float>(fromEnd) /
                                  static_cast<float>(boundaryBlendLen - 1);
                  edgeEnv = t * t * (3.0f - 2.0f * t);
                }
              }

              const float cur = currentSegment[ch][static_cast<size_t>(i)];
              const float target = targetSegment[static_cast<size_t>(i)];
              dst[startSample + i] = cur + edgeEnv * (target - cur);
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

