#pragma once

#include "../JuceHeader.h"
#include <vector>
#include <cmath>
#include <complex>

/**
 * TensionProcessor: STFT-based spectral re-emphasis for vocal tension adjustment.
 *
 * This module implements non-destructive tension control by mixing
 * harmonic and noise waveform components with adjustable energy levels
 * (voicing, breath) and applying a frequency-dependent spectral filter
 * (tension) to the harmonic part.
 *
 * Processing formula:
 *   if tension != 0:
 *     wave = (breath/100)*noise + preEmphasisBaseTension((voicing/100)*harmonic, -tension/50)
 *   else:
 *     wave = (breath/100)*noise + (voicing/100)*harmonic
 *
 * The preEmphasisBaseTension function applies STFT, multiplies by a
 * frequency-dependent linear ramp filter, normalizes amplitude, and
 * performs inverse STFT.
 *
 * STFT Config: n_fft=2048, hop_size=512, win_size=2048, sample_rate=44100
 *
 * Usage:
 *   TensionProcessor proc;
 *   auto result = proc.process(harmonic, noise, numSamples,
 *                              voicingDb, breathDb, tensionVal);
 *
 * All parameters are per-sample (constant across the segment) or can
 * be extended to per-frame in future iterations.
 */
class TensionProcessor
{
public:
  TensionProcessor();
  ~TensionProcessor() = default;

  /**
   * Process a segment of separated harmonic + noise waveforms with
   * voicing, breath, and tension parameters.
   *
   * @param harmonicData  Pointer to harmonic waveform samples
   * @param noiseData     Pointer to noise waveform samples
   * @param numSamples    Number of samples in each buffer
   * @param voicingPct    Voicing level as percentage (0 to maxPct, default 100)
   * @param breathPct     Breath level as percentage (0 to maxPct, default 100)
   * @param tension       Tension value (-100 to 100, 0 = no change)
   * @return              Mixed output waveform
   */
  std::vector<float> process(const float *harmonicData,
                             const float *noiseData,
                             int numSamples,
                             float voicingPct,
                             float breathPct,
                             float tension) const;

  /**
   * Process in-place into a destination buffer.
   * @param dest          Output buffer (must have numSamples capacity)
   * @param harmonicData  Pointer to harmonic waveform samples
   * @param noiseData     Pointer to noise waveform samples
   * @param numSamples    Number of samples
   * @param voicingPct    Voicing percentage
   * @param breathPct     Breath percentage
   * @param tension       Tension value (-100 to 100)
   */
  void processInPlace(float *dest,
                      const float *harmonicData,
                      const float *noiseData,
                      int numSamples,
                      float voicingPct,
                      float breathPct,
                      float tension) const;

  /**
   * Apply pre-emphasis base tension filter via STFT.
   *
   * Steps:
   * 1. Forward STFT with Hann window
   * 2. Apply frequency-dependent linear ramp filter:
   *    filter[k] = clamp((-b / x0) * k + b, -2, 2)
   *    where x0 = fftBin / (sampleRate/2 / 1500), b = tensionParam
   * 3. Multiply each frequency bin's magnitude by 10^(filter[k]/20)
   * 4. Amplitude normalization:
   *    scale = (originalMax / filteredMax) * (clamp(b/-15, 0, 0.33) + 1)
   * 5. Inverse STFT with overlap-add
   *
   * @param signal      Input signal buffer (modified version of harmonic)
   * @param numSamples  Number of samples
   * @param b           Tension parameter (= -tension/50 from the user-facing value)
   * @return            Filtered signal
   */
  std::vector<float> preEmphasisBaseTension(const float *signal,
                                            int numSamples,
                                            float b) const;

private:
  // STFT parameters
  static constexpr int kFFTSize = 2048;
  static constexpr int kHopSize = 512;
  static constexpr int kWinSize = 2048;
  static constexpr int kSampleRate = 44100;
  static constexpr int kFFTBin = kFFTSize / 2 + 1;

  // Pre-computed Hann window
  std::vector<float> hannWindow;

  // Pre-computed twiddle factors for DFT/IDFT
  // We use a simple radix-2 Cooley-Tukey FFT

  /**
   * Compute forward FFT of a real-valued windowed frame.
   * @param frame     Input frame (kFFTSize samples)
   * @param outReal   Real part of FFT (kFFTBin values)
   * @param outImag   Imaginary part of FFT (kFFTBin values)
   */
  void forwardFFT(const float *frame,
                  float *outReal, float *outImag) const;

  /**
   * Compute inverse FFT from complex spectrum back to real signal.
   * @param inReal    Real part of spectrum (kFFTBin values)
   * @param inImag    Imaginary part of spectrum (kFFTBin values)
   * @param outFrame  Output frame (kFFTSize samples)
   */
  void inverseFFT(const float *inReal, const float *inImag,
                  float *outFrame) const;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TensionProcessor)
};
