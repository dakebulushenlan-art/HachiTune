#include "FourierPitchFilter.h"

#include <algorithm>
#include <cmath>

int FourierPitchFilter::nextPowerOfTwo(int n) {
  if (n <= 1)
    return 1;

  int power = 1;
  while (power < n)
    power <<= 1;

  return power;
}

void FourierPitchFilter::applyHannWindow(std::vector<float>& data) {
  const int size = static_cast<int>(data.size());
  if (size <= 1)
    return;

  const float denominator = static_cast<float>(size - 1);
  for (int i = 0; i < size; ++i) {
    const float phase = juce::MathConstants<float>::twoPi *
                        static_cast<float>(i) / denominator;
    const float window = 0.5f * (1.0f - std::cos(phase));
    data[static_cast<size_t>(i)] *= window;
  }
}

void FourierPitchFilter::applyBandpassFilter(std::vector<float>& real,
                                             std::vector<float>& imag,
                                             float lowpassHz,
                                             float highpassHz,
                                             float frameRateHz) {
  const int fftSize = static_cast<int>(real.size());
  if (fftSize == 0 || imag.size() != real.size() || frameRateHz <= 0.0f)
    return;

  const float nyquistHz = frameRateHz * 0.5f;
  float clampedLowpassHz = juce::jlimit(0.0f, nyquistHz, lowpassHz);
  float clampedHighpassHz = juce::jlimit(0.0f, nyquistHz, highpassHz);

  if (clampedLowpassHz < clampedHighpassHz)
    std::swap(clampedLowpassHz, clampedHighpassHz);

  // Keep DC untouched to preserve mean pitch offset.
  for (int bin = 1; bin <= fftSize / 2; ++bin) {
    const float frequencyHz = (static_cast<float>(bin) * frameRateHz) /
                              static_cast<float>(fftSize);
    const bool isInsidePassband = frequencyHz >= clampedHighpassHz &&
                                  frequencyHz <= clampedLowpassHz;

    if (isInsidePassband)
      continue;

    real[static_cast<size_t>(bin)] = 0.0f;
    imag[static_cast<size_t>(bin)] = 0.0f;

    const int mirroredBin = (fftSize - bin) % fftSize;
    real[static_cast<size_t>(mirroredBin)] = 0.0f;
    imag[static_cast<size_t>(mirroredBin)] = 0.0f;
  }
}

FourierPitchFilter::FilterResult
FourierPitchFilter::filterPitchCurve(const std::vector<float>& deltaPitch,
                                     float lowpassHz,
                                     float highpassHz,
                                     float frameRateHz) {
  FilterResult result;

  if (deltaPitch.empty())
    return result;

  if (frameRateHz <= 0.0f) {
    result.filteredPitch = deltaPitch;
    return result;
  }

  const int inputSize = static_cast<int>(deltaPitch.size());
  const int fftSize = nextPowerOfTwo(inputSize);

  int fftOrder = 0;
  while ((1 << fftOrder) < fftSize)
    ++fftOrder;

  // 1) Copy and zero-pad to FFT size.
  std::vector<float> windowedPitch(static_cast<size_t>(fftSize), 0.0f);
  std::copy(deltaPitch.begin(), deltaPitch.end(), windowedPitch.begin());

  // 2) Window in time domain to reduce spectral leakage.
  applyHannWindow(windowedPitch);

  // 3) Pack real samples for JUCE real-only FFT.
  std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);
  std::copy(windowedPitch.begin(), windowedPitch.end(), fftData.begin());

  juce::dsp::FFT fft(fftOrder);
  fft.performRealOnlyForwardTransform(fftData.data());

  // 4) Unpack complex bins and precompute spectrum for visualization.
  std::vector<float> realBins(static_cast<size_t>(fftSize), 0.0f);
  std::vector<float> imagBins(static_cast<size_t>(fftSize), 0.0f);

  for (int bin = 0; bin < fftSize; ++bin) {
    realBins[static_cast<size_t>(bin)] = fftData[static_cast<size_t>(2 * bin)];
    imagBins[static_cast<size_t>(bin)] =
        fftData[static_cast<size_t>(2 * bin + 1)];
  }

  const int nonNegativeBinCount = fftSize / 2 + 1;
  result.magnitudeSpectrum.resize(static_cast<size_t>(nonNegativeBinCount),
                                  0.0f);
  result.frequencyBins.resize(static_cast<size_t>(nonNegativeBinCount), 0.0f);

  for (int bin = 0; bin < nonNegativeBinCount; ++bin) {
    const float realValue = realBins[static_cast<size_t>(bin)];
    const float imagValue = imagBins[static_cast<size_t>(bin)];
    result.magnitudeSpectrum[static_cast<size_t>(bin)] =
        std::sqrt(realValue * realValue + imagValue * imagValue);
    result.frequencyBins[static_cast<size_t>(bin)] =
        (static_cast<float>(bin) * frameRateHz) / static_cast<float>(fftSize);
  }

  // 5) Apply frequency-domain bandpass and repack for inverse FFT.
  applyBandpassFilter(realBins, imagBins, lowpassHz, highpassHz, frameRateHz);

  for (int bin = 0; bin < fftSize; ++bin) {
    fftData[static_cast<size_t>(2 * bin)] = realBins[static_cast<size_t>(bin)];
    fftData[static_cast<size_t>(2 * bin + 1)] =
        imagBins[static_cast<size_t>(bin)];
  }

  // 6) Convert back to time domain and trim zero-padding.
  fft.performRealOnlyInverseTransform(fftData.data());

  result.filteredPitch.resize(deltaPitch.size(), 0.0f);
  for (int i = 0; i < inputSize; ++i)
    result.filteredPitch[static_cast<size_t>(i)] =
        fftData[static_cast<size_t>(i)];

  return result;
}
