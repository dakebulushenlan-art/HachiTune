#pragma once

#include "../JuceHeader.h"
#include "GPUProvider.h"
#include <array>
#include <memory>
#include <vector>

#ifdef HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#ifdef USE_DIRECTML
#include <dml_provider_factory.h>
#endif
#endif

/**
 * Harmonic-Noise Separation (hnsep) model.
 *
 * Separates an audio waveform into harmonic (voiced/tonal) and noise
 * (breath/aperiodic) components using ONNX Runtime inference.
 *
 * The model operates directly on raw waveforms at 44100 Hz:
 *   Input:  "waveform"  float32[batch_size, n_samples]
 *   Output: harmonic    float32[batch_size, n_samples]
 *           noise       float32[batch_size, n_samples]
 *
 * The two outputs sum to the original input (energy-preserving separation).
 *
 * Long audio is processed in overlapping chunks to stay within GPU memory
 * limits, with crossfade blending at chunk boundaries to avoid artifacts.
 */
class HNSepModel
{
public:
  /// Expected sample rate for the hnsep model.
  static constexpr int SAMPLE_RATE = 44100;

  /// Maximum chunk size for inference (30 seconds at 44100 Hz).
  static constexpr int MAX_CHUNK_SAMPLES = SAMPLE_RATE * 30;

  /// Overlap between adjacent chunks (1 second at 44100 Hz).
  static constexpr int OVERLAP_SAMPLES = SAMPLE_RATE * 1;

  HNSepModel();
  ~HNSepModel();

  /**
   * Load the hnsep ONNX model.
   *
   * @param modelPath  Path to hnsep_VR.onnx
   * @param provider   GPU execution provider (CPU, CUDA, DirectML, CoreML)
   * @param deviceId   GPU device index (0 = first device)
   * @return true on success
   */
  bool loadModel(const juce::File &modelPath,
                 GPUProvider provider = GPUProvider::CPU,
                 int deviceId = 0);

  /** Check whether the model has been successfully loaded. */
  bool isLoaded() const { return loaded; }

  /**
   * Separate audio into harmonic and noise components.
   *
   * For audio longer than MAX_CHUNK_SAMPLES the waveform is processed in
   * overlapping chunks with linear crossfade blending at boundaries.
   *
   * @param audio       Pointer to mono float samples at 44100 Hz
   * @param numSamples  Number of samples
   * @param[out] harmonic  Receives the harmonic component (resized to numSamples)
   * @param[out] noise     Receives the noise component (resized to numSamples)
   * @return true on success
   */
  bool separate(const float *audio, int numSamples,
                std::vector<float> &harmonic,
                std::vector<float> &noise);

  /**
   * Separate with a progress callback (0.0 .. 1.0).
   */
  bool separateWithProgress(const float *audio, int numSamples,
                            std::vector<float> &harmonic,
                            std::vector<float> &noise,
                            std::function<void(double)> progressCallback);

private:
  bool loaded = false;

  /**
   * Run inference on a single contiguous chunk.
   *
   * @param audio       Chunk samples (44100 Hz mono)
   * @param numSamples  Chunk length
   * @param[out] harmonic  Harmonic output for this chunk
   * @param[out] noise     Noise output for this chunk
   * @return true on success
   */
  bool separateChunk(const float *audio, int numSamples,
                     std::vector<float> &harmonic,
                     std::vector<float> &noise);

#ifdef HAVE_ONNXRUNTIME
  std::unique_ptr<Ort::Env> onnxEnv;
  std::unique_ptr<Ort::Session> onnxSession;
  std::unique_ptr<Ort::AllocatorWithDefaultOptions> allocator;

  std::vector<const char *> inputNames;
  std::vector<const char *> outputNames;
  std::vector<std::string> inputNameStrings;
  std::vector<std::string> outputNameStrings;

  /// Reusable shape buffer: [1, n_samples] (batch=1, second dim set per call).
  std::array<int64_t, 2> waveformShapeScratch{1, 0};
#endif

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HNSepModel)
};
