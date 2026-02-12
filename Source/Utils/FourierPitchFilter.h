#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * FFT-based pitch curve filtering.
 *
 * Converts pitch curves to frequency domain, applies bandpass filtering,
 * and returns the filtered curve and spectrum data for visualization.
 */
class FourierPitchFilter {
public:
  struct FilterResult {
    std::vector<float> filteredPitch;
    std::vector<float> magnitudeSpectrum;
    std::vector<float> frequencyBins;
  };

  /**
   * Apply bandpass filtering to a pitch curve.
   *
   * @param deltaPitch Input pitch deviations in semitones.
   * @param lowpassHz Lowpass cutoff in Hz (remove above this frequency).
   * @param highpassHz Highpass cutoff in Hz (remove below this frequency).
   * @param frameRateHz Sample rate of the pitch curve frames in Hz.
   * @return FilterResult containing filtered pitch and spectrum metadata.
   */
  static FilterResult filterPitchCurve(const std::vector<float>& deltaPitch,
                                       float lowpassHz,
                                       float highpassHz,
                                       float frameRateHz);

private:
  static int nextPowerOfTwo(int n);
  static void applyHannWindow(std::vector<float>& data);
  static void applyBandpassFilter(std::vector<float>& real,
                                  std::vector<float>& imag,
                                  float lowpassHz,
                                  float highpassHz,
                                  float frameRateHz);
};
