#include "TensionProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TensionProcessor::TensionProcessor()
{
  // Pre-compute Hann window: w[n] = 0.5 * (1 - cos(2*pi*n / N))
  hannWindow.resize(static_cast<size_t>(kWinSize));
  const double twoPi = 2.0 * 3.14159265358979323846;
  for (int n = 0; n < kWinSize; ++n)
  {
    hannWindow[static_cast<size_t>(n)] =
        static_cast<float>(0.5 * (1.0 - std::cos(twoPi * n / kWinSize)));
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<float> TensionProcessor::process(const float *harmonicData,
                                             const float *noiseData,
                                             int numSamples,
                                             float voicingPct,
                                             float breathPct,
                                             float tension) const
{
  std::vector<float> result(static_cast<size_t>(numSamples), 0.0f);
  processInPlace(result.data(), harmonicData, noiseData,
                 numSamples, voicingPct, breathPct, tension);
  return result;
}

void TensionProcessor::processInPlace(float *dest,
                                      const float *harmonicData,
                                      const float *noiseData,
                                      int numSamples,
                                      float voicingPct,
                                      float breathPct,
                                      float tension) const
{
  if (numSamples <= 0)
    return;

  const float voicingScale = voicingPct / 100.0f;
  const float breathScale = breathPct / 100.0f;

  if (std::abs(tension) < 0.001f)
  {
    // No tension adjustment — simple linear mix
    for (int i = 0; i < numSamples; ++i)
    {
      dest[i] = breathScale * noiseData[i]
              + voicingScale * harmonicData[i];
    }
  }
  else
  {
    // Clamp tension to [-100, 100]
    tension = juce::jlimit(-100.0f, 100.0f, tension);

    // Scale harmonic by voicing first
    std::vector<float> scaledHarmonic(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
      scaledHarmonic[static_cast<size_t>(i)] = voicingScale * harmonicData[i];

    // Apply pre-emphasis base tension filter
    // The parameter b = -tension / 50
    float b = -tension / 50.0f;
    auto filteredHarmonic = preEmphasisBaseTension(
        scaledHarmonic.data(), numSamples, b);

    // Mix: breath*noise + filtered_harmonic
    for (int i = 0; i < numSamples; ++i)
    {
      dest[i] = breathScale * noiseData[i]
              + filteredHarmonic[static_cast<size_t>(i)];
    }
  }
}

// ---------------------------------------------------------------------------
// Pre-Emphasis Base Tension (STFT-based spectral filter)
// ---------------------------------------------------------------------------

std::vector<float> TensionProcessor::preEmphasisBaseTension(
    const float *signal, int numSamples, float b) const
{
  if (numSamples <= 0)
    return {};

  // Compute original max amplitude for normalization
  float originalMax = 0.0f;
  for (int i = 0; i < numSamples; ++i)
    originalMax = std::max(originalMax, std::abs(signal[i]));

  if (originalMax < 1e-10f)
    return std::vector<float>(signal, signal + numSamples);

  // Build the frequency-dependent filter
  // x0 = fftBin / (sampleRate/2 / 1500)
  // filter[k] = clamp((-b/x0) * k + b, -2, 2)
  // Then convert dB to linear gain: gain[k] = 10^(filter[k] / 20)
  const float nyquist = static_cast<float>(kSampleRate) / 2.0f;
  const float x0 = static_cast<float>(kFFTBin) / (nyquist / 1500.0f);

  std::vector<float> filterGain(static_cast<size_t>(kFFTBin));
  for (int k = 0; k < kFFTBin; ++k)
  {
    float filterDb = (-b / x0) * static_cast<float>(k) + b;
    filterDb = juce::jlimit(-2.0f, 2.0f, filterDb);
    filterGain[static_cast<size_t>(k)] =
        std::pow(10.0f, filterDb / 20.0f);
  }

  // STFT analysis + filter + synthesis via overlap-add
  // Pad signal for full STFT coverage
  int numFrames = (numSamples + kHopSize - 1) / kHopSize;
  int paddedLen = numFrames * kHopSize + kWinSize;
  std::vector<float> padded(static_cast<size_t>(paddedLen), 0.0f);
  // Center the signal with half-window offset
  int offset = kWinSize / 2;
  for (int i = 0; i < numSamples; ++i)
    padded[static_cast<size_t>(offset + i)] = signal[i];

  // Output accumulator and window normalization
  std::vector<float> output(static_cast<size_t>(paddedLen), 0.0f);
  std::vector<float> windowSum(static_cast<size_t>(paddedLen), 0.0f);

  // Temporary buffers for FFT
  std::vector<float> frame(static_cast<size_t>(kFFTSize));
  std::vector<float> fftReal(static_cast<size_t>(kFFTBin));
  std::vector<float> fftImag(static_cast<size_t>(kFFTBin));
  std::vector<float> outFrame(static_cast<size_t>(kFFTSize));

  // Process each frame
  for (int f = 0; f < numFrames; ++f)
  {
    int frameStart = f * kHopSize;

    // Apply analysis window
    for (int n = 0; n < kFFTSize; ++n)
    {
      int idx = frameStart + n;
      if (idx < paddedLen)
        frame[static_cast<size_t>(n)] =
            padded[static_cast<size_t>(idx)]
            * hannWindow[static_cast<size_t>(n)];
      else
        frame[static_cast<size_t>(n)] = 0.0f;
    }

    // Forward FFT
    forwardFFT(frame.data(), fftReal.data(), fftImag.data());

    // Apply frequency filter (multiply magnitude, preserve phase)
    for (int k = 0; k < kFFTBin; ++k)
    {
      fftReal[static_cast<size_t>(k)] *= filterGain[static_cast<size_t>(k)];
      fftImag[static_cast<size_t>(k)] *= filterGain[static_cast<size_t>(k)];
    }

    // Inverse FFT
    inverseFFT(fftReal.data(), fftImag.data(), outFrame.data());

    // Overlap-add with synthesis window
    for (int n = 0; n < kFFTSize; ++n)
    {
      int idx = frameStart + n;
      if (idx < paddedLen)
      {
        float w = hannWindow[static_cast<size_t>(n)];
        output[static_cast<size_t>(idx)] += outFrame[static_cast<size_t>(n)] * w;
        windowSum[static_cast<size_t>(idx)] += w * w;
      }
    }
  }

  // Normalize by window sum (COLA condition)
  for (int i = 0; i < paddedLen; ++i)
  {
    if (windowSum[static_cast<size_t>(i)] > 1e-8f)
      output[static_cast<size_t>(i)] /= windowSum[static_cast<size_t>(i)];
  }

  // Extract result (remove padding)
  std::vector<float> result(static_cast<size_t>(numSamples));
  for (int i = 0; i < numSamples; ++i)
    result[static_cast<size_t>(i)] = output[static_cast<size_t>(offset + i)];

  // Amplitude normalization:
  // filteredMax = max(abs(result))
  // scale = (originalMax / filteredMax) * (clamp(b / -15, 0, 0.33) + 1)
  float filteredMax = 0.0f;
  for (int i = 0; i < numSamples; ++i)
    filteredMax = std::max(filteredMax, std::abs(result[static_cast<size_t>(i)]));

  if (filteredMax > 1e-10f)
  {
    float ampCorrection = juce::jlimit(0.0f, 0.33f, b / -15.0f) + 1.0f;
    float scale = (originalMax / filteredMax) * ampCorrection;
    for (int i = 0; i < numSamples; ++i)
      result[static_cast<size_t>(i)] *= scale;
  }

  return result;
}

// ---------------------------------------------------------------------------
// Simple DFT / IDFT (radix-2 Cooley-Tukey FFT)
// ---------------------------------------------------------------------------

/**
 * Bit-reverse an index for FFT butterfly operations.
 */
static int bitReverse(int x, int log2n)
{
  int result = 0;
  for (int i = 0; i < log2n; ++i)
  {
    result = (result << 1) | (x & 1);
    x >>= 1;
  }
  return result;
}

void TensionProcessor::forwardFFT(const float *frame,
                                  float *outReal, float *outImag) const
{
  // In-place radix-2 FFT on kFFTSize points.
  // We work in full complex arrays of size kFFTSize, then output
  // only the first kFFTBin (positive frequencies).

  const int N = kFFTSize;
  const int log2N = static_cast<int>(std::round(std::log2(static_cast<double>(N))));

  // Bit-reversal permutation
  std::vector<float> re(static_cast<size_t>(N));
  std::vector<float> im(static_cast<size_t>(N), 0.0f);

  for (int i = 0; i < N; ++i)
  {
    int j = bitReverse(i, log2N);
    re[static_cast<size_t>(j)] = frame[i];
  }

  // Butterfly stages
  for (int s = 1; s <= log2N; ++s)
  {
    int m = 1 << s;
    int halfM = m >> 1;
    double angle = -2.0 * 3.14159265358979323846 / m;
    float wRe = static_cast<float>(std::cos(angle));
    float wIm = static_cast<float>(std::sin(angle));

    for (int k = 0; k < N; k += m)
    {
      float tRe = 1.0f, tIm = 0.0f; // twiddle factor
      for (int j = 0; j < halfM; ++j)
      {
        size_t u = static_cast<size_t>(k + j);
        size_t v = static_cast<size_t>(k + j + halfM);

        // Butterfly: X[u], X[v] = X[u] + t*X[v], X[u] - t*X[v]
        float tmpRe = tRe * re[v] - tIm * im[v];
        float tmpIm = tRe * im[v] + tIm * re[v];

        re[v] = re[u] - tmpRe;
        im[v] = im[u] - tmpIm;
        re[u] = re[u] + tmpRe;
        im[u] = im[u] + tmpIm;

        // Advance twiddle
        float newTRe = tRe * wRe - tIm * wIm;
        float newTIm = tRe * wIm + tIm * wRe;
        tRe = newTRe;
        tIm = newTIm;
      }
    }
  }

  // Copy positive frequencies
  for (int k = 0; k < kFFTBin; ++k)
  {
    outReal[k] = re[static_cast<size_t>(k)];
    outImag[k] = im[static_cast<size_t>(k)];
  }
}

void TensionProcessor::inverseFFT(const float *inReal, const float *inImag,
                                  float *outFrame) const
{
  // Reconstruct full spectrum from positive frequencies (Hermitian symmetry).
  // Then run forward FFT with conjugate input, divide by N.

  const int N = kFFTSize;
  const int log2N = static_cast<int>(std::round(std::log2(static_cast<double>(N))));

  std::vector<float> re(static_cast<size_t>(N));
  std::vector<float> im(static_cast<size_t>(N));

  // Fill positive frequencies
  for (int k = 0; k < kFFTBin; ++k)
  {
    re[static_cast<size_t>(k)] = inReal[k];
    im[static_cast<size_t>(k)] = -inImag[k]; // conjugate for IFFT via FFT
  }

  // Fill negative frequencies via Hermitian symmetry: X[N-k] = conj(X[k])
  for (int k = 1; k < kFFTBin - 1; ++k)
  {
    re[static_cast<size_t>(N - k)] = inReal[k];
    im[static_cast<size_t>(N - k)] = inImag[k]; // conj of conjugate = original
  }

  // Bit-reversal permutation
  std::vector<float> reP(static_cast<size_t>(N));
  std::vector<float> imP(static_cast<size_t>(N));
  for (int i = 0; i < N; ++i)
  {
    int j = bitReverse(i, log2N);
    reP[static_cast<size_t>(j)] = re[static_cast<size_t>(i)];
    imP[static_cast<size_t>(j)] = im[static_cast<size_t>(i)];
  }

  // Butterfly stages (same as forward FFT)
  for (int s = 1; s <= log2N; ++s)
  {
    int m = 1 << s;
    int halfM = m >> 1;
    double angle = -2.0 * 3.14159265358979323846 / m;
    float wRe = static_cast<float>(std::cos(angle));
    float wIm = static_cast<float>(std::sin(angle));

    for (int k = 0; k < N; k += m)
    {
      float tRe = 1.0f, tIm = 0.0f;
      for (int j = 0; j < halfM; ++j)
      {
        size_t u = static_cast<size_t>(k + j);
        size_t v = static_cast<size_t>(k + j + halfM);

        float tmpRe = tRe * reP[v] - tIm * imP[v];
        float tmpIm = tRe * imP[v] + tIm * reP[v];

        reP[v] = reP[u] - tmpRe;
        imP[v] = imP[u] - tmpIm;
        reP[u] = reP[u] + tmpRe;
        imP[u] = imP[u] + tmpIm;

        float newTRe = tRe * wRe - tIm * wIm;
        float newTIm = tRe * wIm + tIm * wRe;
        tRe = newTRe;
        tIm = newTIm;
      }
    }
  }

  // Divide by N and take real part
  float invN = 1.0f / static_cast<float>(N);
  for (int i = 0; i < N; ++i)
    outFrame[i] = reP[static_cast<size_t>(i)] * invN;
}
