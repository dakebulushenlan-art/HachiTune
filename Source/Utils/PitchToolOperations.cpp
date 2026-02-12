#include "PitchToolOperations.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace {

float clamp01(float value) {
  return std::max(0.0f, std::min(1.0f, value));
}

} // namespace

namespace PitchToolOperations {

std::vector<float> tiltDeltaPitch(const std::vector<float>& deltaPitch,
                                  float pivotPosition,
                                  float amount) {
  if (deltaPitch.empty()) {
    return {};
  }

  std::vector<float> result(deltaPitch);
  if (deltaPitch.size() == 1) {
    return result;
  }

  const float clampedPivot = clamp01(pivotPosition);
  const float maxDistance = std::max(clampedPivot, 1.0f - clampedPivot);
  if (maxDistance <= 0.0f) {
    return result;
  }

  const float invLastIndex = 1.0f / static_cast<float>(deltaPitch.size() - 1);
  for (size_t i = 0; i < deltaPitch.size(); ++i) {
    const float normalizedPosition = static_cast<float>(i) * invLastIndex;
    // Normalize by furthest edge distance so `amount` means a full-end shift.
    const float normalizedDistance =
        (normalizedPosition - clampedPivot) / maxDistance;
    result[i] = deltaPitch[i] + normalizedDistance * amount;
  }

  return result;
}

std::vector<float> reduceVariance(const std::vector<float>& deltaPitch,
                                  float factor) {
  if (deltaPitch.empty()) {
    return {};
  }

  const float clampedFactor = clamp01(factor);
  const float mean = computeMean(deltaPitch);

  std::vector<float> result(deltaPitch.size(), 0.0f);
  std::transform(deltaPitch.begin(), deltaPitch.end(), result.begin(),
                 [mean, clampedFactor](float value) {
                   return mean + (value - mean) * clampedFactor;
                 });

  return result;
}

std::vector<float> smoothBoundary(const std::vector<float>& deltaPitch,
                                  int side,
                                  int transitionFrames,
                                  float targetPitch) {
  if (deltaPitch.empty()) {
    return {};
  }

  std::vector<float> result(deltaPitch);
  if (transitionFrames <= 0 || (side != 0 && side != 1)) {
    return result;
  }

  const int clampedFrames = std::max(
      1, std::min(transitionFrames, static_cast<int>(deltaPitch.size())));

  constexpr float pi = 3.14159265358979323846f;

  if (side == 0) {
    const float boundaryPitch = deltaPitch.front();
    for (int i = 0; i < clampedFrames; ++i) {
      const float t = (clampedFrames == 1)
                          ? 1.0f
                          : static_cast<float>(i) /
                                static_cast<float>(clampedFrames - 1);
      // Cosine easing removes hard slope changes at note boundaries.
      const float smoothT = 0.5f * (1.0f - std::cos(t * pi));
      result[static_cast<size_t>(i)] =
          targetPitch + (boundaryPitch - targetPitch) * smoothT;
    }
  } else {
    const float boundaryPitch = deltaPitch.back();
    const size_t startIndex = deltaPitch.size() - static_cast<size_t>(clampedFrames);
    for (int i = 0; i < clampedFrames; ++i) {
      const float t = (clampedFrames == 1)
                          ? 1.0f
                          : static_cast<float>(i) /
                                static_cast<float>(clampedFrames - 1);
      // Match left-side easing so cross-note joins remain symmetric.
      const float smoothT = 0.5f * (1.0f - std::cos(t * pi));
      const size_t index = startIndex + static_cast<size_t>(i);
      result[index] = boundaryPitch + (targetPitch - boundaryPitch) * smoothT;
    }
  }

  return result;
}

float computeMean(const std::vector<float>& deltaPitch) {
  if (deltaPitch.empty()) {
    return 0.0f;
  }

  const float sum =
      std::accumulate(deltaPitch.begin(), deltaPitch.end(), 0.0f);
  return sum / static_cast<float>(deltaPitch.size());
}

} // namespace PitchToolOperations
