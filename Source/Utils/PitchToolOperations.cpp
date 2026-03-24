#include "PitchToolOperations.h"

#include <algorithm>
#include <cmath>
#include <numeric>

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

  const float clampedPivot = std::clamp(pivotPosition, 0.0f, 1.0f);
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

  std::vector<float> result(deltaPitch.size(), 0.0f);
  std::transform(deltaPitch.begin(), deltaPitch.end(), result.begin(),
                 [factor](float value) {
                   return value * factor;
                 });

  return result;
}

std::vector<float> trimLinearPitchDrift(const std::vector<float>& deltaPitch,
                                         float amount) {
  if (deltaPitch.empty() || amount <= 0.0001f) {
    return deltaPitch;
  }

  const int n = static_cast<int>(deltaPitch.size());
  if (n <= 1) {
    return deltaPitch;
  }

  const float a = std::clamp(amount, 0.0f, 1.0f);
  float sumTT = 0.0f;
  float sumTY = 0.0f;
  for (int i = 0; i < n; ++i) {
    const float t =
        static_cast<float>(i) / static_cast<float>(n - 1) - 0.5f; // zero-mean abscissa
    const float y = deltaPitch[static_cast<size_t>(i)];
    sumTT += t * t;
    sumTY += t * y;
  }
  if (sumTT <= 1.0e-12f) {
    return deltaPitch;
  }

  const float b = sumTY / sumTT; // slope; trend has zero mean over t
  std::vector<float> result(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float t =
        static_cast<float>(i) / static_cast<float>(n - 1) - 0.5f;
    const float trend = b * t;
    result[static_cast<size_t>(i)] =
        deltaPitch[static_cast<size_t>(i)] - a * trend;
  }
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

  // Gaussian kernel: sigma = transitionFrames / 2.0
  // Weight at boundary = 1.0 (full target), weight at edge of transition = ~0.14
  const float sigma = static_cast<float>(clampedFrames) / 2.0f;
  const float invTwoSigmaSq = 1.0f / (2.0f * sigma * sigma);

  if (side == 0) {
    // Left boundary: blend FROM targetPitch TO note's internal curve
    for (int i = 0; i < clampedFrames; ++i) {
      // Distance from boundary (frame 0)
      const float dist = static_cast<float>(i);
      // Gaussian weight: 1.0 at boundary, decreasing toward interior
      const float gaussWeight = std::exp(-dist * dist * invTwoSigmaSq);
      // Blend: high gaussWeight = more targetPitch, low = more original
      result[static_cast<size_t>(i)] =
          targetPitch * gaussWeight + deltaPitch[static_cast<size_t>(i)] * (1.0f - gaussWeight);
    }
  } else {
    // Right boundary: blend FROM note's internal curve TO targetPitch
    const size_t startIndex = deltaPitch.size() - static_cast<size_t>(clampedFrames);
    for (int i = 0; i < clampedFrames; ++i) {
      // Distance from boundary (last frame)
      const float dist = static_cast<float>(clampedFrames - 1 - i);
      // Gaussian weight: 1.0 at boundary, decreasing toward interior
      const float gaussWeight = std::exp(-dist * dist * invTwoSigmaSq);
      const size_t index = startIndex + static_cast<size_t>(i);
      // Blend: high gaussWeight = more targetPitch, low = more original
      result[index] =
          targetPitch * gaussWeight + deltaPitch[index] * (1.0f - gaussWeight);
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

std::vector<float> applyAllTransformations(const std::vector<float>& originalDelta,
                                           float tiltLeft,
                                           float tiltRight,
                                           float varianceScale,
                                           float pitchDriftTrim,
                                           int smoothLeftFrames,
                                           int smoothRightFrames,
                                           const AdjacentNoteContext& adjacentContext) {
  if (originalDelta.empty()) {
    return {};
  }

  // Start with the original pristine curve
  std::vector<float> result = originalDelta;

  // 1. Apply variance scaling
  // Variance first so that tilt ramp is preserved even at variance=0
  if (std::abs(varianceScale - 1.0f) > 0.001f) {
    result = reduceVariance(result, varianceScale);
  }

  // 2. Reduce slow linear drift (Melodyne / pitch-drift style)
  if (std::abs(pitchDriftTrim) > 0.001f) {
    result = trimLinearPitchDrift(result, pitchDriftTrim);
  }

  // 3. Apply tilt transformations (combined left + right)
  // TiltLeft: pivot at right (1.0), negative amount
  if (std::abs(tiltLeft) > 0.001f) {
    result = tiltDeltaPitch(result, 1.0f, -tiltLeft);
  }
  
  // TiltRight: pivot at left (0.0), positive amount
  if (std::abs(tiltRight) > 0.001f) {
    result = tiltDeltaPitch(result, 0.0f, tiltRight);
  }

  // 4. Apply boundary smoothing
  if (smoothLeftFrames > 0) {
    const float leftTarget = adjacentContext.hasLeft ? adjacentContext.leftBoundaryDelta : 0.0f;
    result = smoothBoundary(result, 0, smoothLeftFrames, leftTarget);
  }
  
  if (smoothRightFrames > 0) {
    const float rightTarget = adjacentContext.hasRight ? adjacentContext.rightBoundaryDelta : 0.0f;
    result = smoothBoundary(result, 1, smoothRightFrames, rightTarget);
  }

  return result;
}

} // namespace PitchToolOperations
