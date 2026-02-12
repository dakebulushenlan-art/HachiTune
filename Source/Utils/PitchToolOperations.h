#pragma once

#include <vector>

namespace PitchToolOperations {

/**
 * Applies a linear tilt around a pivot position.
 *
 * The value at the pivot stays unchanged, and the contour is shifted
 * linearly across the note. The furthest end from the pivot reaches
 * the full `amount` in semitones.
 */
std::vector<float> tiltDeltaPitch(const std::vector<float>& deltaPitch,
                                  float pivotPosition,
                                  float amount);

/**
 * Reduces deviations from the mean pitch value.
 *
 * `factor = 0` fully flattens to the mean and `factor = 1` keeps
 * the original contour unchanged.
 */
std::vector<float> reduceVariance(const std::vector<float>& deltaPitch,
                                  float factor);

/**
 * Smooths one boundary to connect with adjacent pitch context.
 *
 * For left side, fades from `targetPitch` to the note boundary.
 * For right side, fades from the note boundary to `targetPitch`.
 * Cosine interpolation is used to avoid abrupt slope changes.
 */
std::vector<float> smoothBoundary(const std::vector<float>& deltaPitch,
                                  int side,
                                  int transitionFrames,
                                  float targetPitch);

/**
 * Computes the arithmetic mean of a pitch contour.
 * Returns 0 when the input is empty.
 */
float computeMean(const std::vector<float>& deltaPitch);

} // namespace PitchToolOperations
