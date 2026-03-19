#include "HNSepModel.h"
#include "../Utils/AppLogger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

HNSepModel::HNSepModel() = default;

HNSepModel::~HNSepModel() = default;

bool HNSepModel::loadModel(const juce::File &modelPath,
                           GPUProvider provider, int deviceId)
{
#ifdef HAVE_ONNXRUNTIME
  try
  {
    onnxEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING,
                                         "HNSepModel");

    Ort::SessionOptions sessionOptions;
    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);
    sessionOptions.EnableCpuMemArena();

    if (provider == GPUProvider::CPU)
    {
      const int numThreads =
          std::max(1u, std::thread::hardware_concurrency()) / 2;
      sessionOptions.SetIntraOpNumThreads(std::max(numThreads, 2));
      sessionOptions.EnableMemPattern();
    }
    else
    {
      sessionOptions.SetIntraOpNumThreads(1);
      sessionOptions.SetInterOpNumThreads(1);
    }

    // Configure GPU execution provider
#if defined(_WIN32) && defined(USE_DIRECTML)
    if (provider == GPUProvider::DirectML)
    {
      try
      {
        const OrtApi &ortApi = Ort::GetApi();
        const OrtDmlApi *ortDmlApi = nullptr;
        Ort::ThrowOnError(ortApi.GetExecutionProviderApi(
            "DML", ORT_API_VERSION,
            reinterpret_cast<const void **>(&ortDmlApi)));

        sessionOptions.DisableMemPattern();
        sessionOptions.SetExecutionMode(ORT_SEQUENTIAL);

        Ort::ThrowOnError(ortDmlApi->SessionOptionsAppendExecutionProvider_DML(
            sessionOptions, deviceId));
      }
      catch (const Ort::Exception &e)
      {
        LOG("HNSep: DirectML provider failed, falling back to CPU: " +
            juce::String(e.what()));
      }
    }
    else
#endif
#ifdef USE_CUDA
        if (provider == GPUProvider::CUDA)
    {
      try
      {
        OrtCUDAProviderOptions cudaOptions;
        cudaOptions.device_id = deviceId;
        cudaOptions.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchDefault;
        cudaOptions.arena_extend_strategy = 1;
        sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
      }
      catch (const Ort::Exception &e)
      {
        LOG("HNSep: CUDA provider failed, falling back to CPU: " +
            juce::String(e.what()));
      }
    }
    else
#endif
        if (provider == GPUProvider::CoreML)
    {
      try
      {
        sessionOptions.AppendExecutionProvider("CoreML",
                                               {{"MLComputeUnits", "ALL"}});
      }
      catch (const Ort::Exception &e)
      {
        LOG("HNSep: CoreML provider failed, falling back to CPU: " +
            juce::String(e.what()));
      }
    }
    else
    {
      if (provider != GPUProvider::CPU)
      {
        LOG("HNSep: Unsupported provider, using CPU");
      }
    }

    // Create session
#ifdef _WIN32
    std::wstring modelPathW = modelPath.getFullPathName().toWideCharPointer();
    onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathW.c_str(),
                                                 sessionOptions);
#else
    std::string modelPathStr = modelPath.getFullPathName().toStdString();
    onnxSession = std::make_unique<Ort::Session>(*onnxEnv, modelPathStr.c_str(),
                                                 sessionOptions);
#endif

    allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();

    // Retrieve input/output names dynamically
    size_t numInputs = onnxSession->GetInputCount();
    size_t numOutputs = onnxSession->GetOutputCount();

    inputNameStrings.clear();
    outputNameStrings.clear();
    inputNames.clear();
    outputNames.clear();

    for (size_t i = 0; i < numInputs; ++i)
    {
      auto namePtr = onnxSession->GetInputNameAllocated(i, *allocator);
      inputNameStrings.push_back(namePtr.get());
    }
    for (size_t i = 0; i < numOutputs; ++i)
    {
      auto namePtr = onnxSession->GetOutputNameAllocated(i, *allocator);
      outputNameStrings.push_back(namePtr.get());
    }

    for (const auto &name : inputNameStrings)
      inputNames.push_back(name.c_str());
    for (const auto &name : outputNameStrings)
      outputNames.push_back(name.c_str());

    loaded = true;
    LOG("HNSep model loaded successfully (" +
        juce::String(numInputs) + " inputs, " +
        juce::String(numOutputs) + " outputs)");
    return true;
  }
  catch (const Ort::Exception &e)
  {
    LOG("HNSep model load failed (Ort): " + juce::String(e.what()));
    loaded = false;
    return false;
  }
  catch (const std::exception &e)
  {
    LOG("HNSep model load failed: " + juce::String(e.what()));
    loaded = false;
    return false;
  }
#else
  juce::ignoreUnused(modelPath, provider, deviceId);
  LOG("HNSep: ONNX Runtime not available (HAVE_ONNXRUNTIME not defined)");
  return false;
#endif
}

bool HNSepModel::separateChunk(const float *audio, int numSamples,
                                std::vector<float> &harmonic,
                                std::vector<float> &noise)
{
#ifdef HAVE_ONNXRUNTIME
  if (!onnxSession || numSamples <= 0)
    return false;

  // Build input tensor [1, numSamples]
  waveformShapeScratch[1] = static_cast<int64_t>(numSamples);

  static const auto memoryInfo = Ort::MemoryInfo::CreateCpu(
      OrtAllocatorType::OrtArenaAllocator, OrtMemType::OrtMemTypeDefault);
  static const Ort::RunOptions runOptions{nullptr};

  std::vector<Ort::Value> inputTensors;
  inputTensors.emplace_back(Ort::Value::CreateTensor<float>(
      memoryInfo, const_cast<float *>(audio),
      static_cast<size_t>(numSamples),
      waveformShapeScratch.data(), waveformShapeScratch.size()));

  auto t0 = std::chrono::high_resolution_clock::now();
  auto outputs = onnxSession->Run(
      runOptions, inputNames.data(), inputTensors.data(),
      inputTensors.size(), outputNames.data(), outputNames.size());
  auto t1 = std::chrono::high_resolution_clock::now();
  LOG("HNSep chunk inference: " +
      juce::String(std::chrono::duration<double, std::milli>(t1 - t0).count(), 1) +
      " ms (" + juce::String(numSamples) + " samples)");

  if (outputs.size() < 2)
  {
    LOG("HNSep: unexpected output count: " + juce::String(outputs.size()));
    return false;
  }

  // Output tensors: [1, n_samples] each
  float *harmonicPtr = outputs[0].GetTensorMutableData<float>();
  float *noisePtr = outputs[1].GetTensorMutableData<float>();

  auto harmonicShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
  int outSamples = static_cast<int>(harmonicShape.back());

  // The model may return the same or slightly different length; use min
  int copyLen = std::min(numSamples, outSamples);
  harmonic.assign(harmonicPtr, harmonicPtr + copyLen);
  noise.assign(noisePtr, noisePtr + copyLen);

  // Pad to exact numSamples if shorter
  harmonic.resize(numSamples, 0.0f);
  noise.resize(numSamples, 0.0f);

  return true;
#else
  juce::ignoreUnused(audio, numSamples, harmonic, noise);
  return false;
#endif
}

bool HNSepModel::separate(const float *audio, int numSamples,
                           std::vector<float> &harmonic,
                           std::vector<float> &noise)
{
  return separateWithProgress(audio, numSamples, harmonic, noise, nullptr);
}

bool HNSepModel::separateWithProgress(
    const float *audio, int numSamples,
    std::vector<float> &harmonic,
    std::vector<float> &noise,
    std::function<void(double)> progressCallback)
{
#ifdef HAVE_ONNXRUNTIME
  if (!loaded || numSamples <= 0)
    return false;

  if (progressCallback)
    progressCallback(0.0);

  // Short audio — process in one pass
  if (numSamples <= MAX_CHUNK_SAMPLES)
  {
    bool ok = separateChunk(audio, numSamples, harmonic, noise);
    if (progressCallback)
      progressCallback(1.0);
    return ok;
  }

  // Long audio — chunked with crossfade overlap
  harmonic.resize(numSamples, 0.0f);
  noise.resize(numSamples, 0.0f);

  int pos = 0;
  int chunkIndex = 0;
  const int stride = MAX_CHUNK_SAMPLES - OVERLAP_SAMPLES;

  while (pos < numSamples)
  {
    int chunkEnd = std::min(pos + MAX_CHUNK_SAMPLES, numSamples);
    int chunkSize = chunkEnd - pos;

    std::vector<float> chunkHarm, chunkNoise;
    if (!separateChunk(audio + pos, chunkSize, chunkHarm, chunkNoise))
      return false;

    if (chunkIndex == 0)
    {
      // First chunk: copy entirely
      std::copy(chunkHarm.begin(), chunkHarm.end(), harmonic.begin() + pos);
      std::copy(chunkNoise.begin(), chunkNoise.end(), noise.begin() + pos);
    }
    else
    {
      // Overlap region: linear crossfade between previous chunk tail and
      // this chunk head. Beyond the overlap, copy directly.
      int overlapSamples = std::min(OVERLAP_SAMPLES, chunkSize);
      for (int i = 0; i < overlapSamples; ++i)
      {
        float alpha = static_cast<float>(i) / static_cast<float>(overlapSamples);
        int destIdx = pos + i;
        harmonic[destIdx] = harmonic[destIdx] * (1.0f - alpha) +
                            chunkHarm[i] * alpha;
        noise[destIdx] = noise[destIdx] * (1.0f - alpha) +
                         chunkNoise[i] * alpha;
      }
      // Non-overlap part: direct copy
      if (overlapSamples < chunkSize)
      {
        std::copy(chunkHarm.begin() + overlapSamples, chunkHarm.end(),
                  harmonic.begin() + pos + overlapSamples);
        std::copy(chunkNoise.begin() + overlapSamples, chunkNoise.end(),
                  noise.begin() + pos + overlapSamples);
      }
    }

    pos += stride;
    ++chunkIndex;

    if (progressCallback)
    {
      double progress = static_cast<double>(std::min(pos, numSamples)) /
                        static_cast<double>(numSamples);
      progressCallback(progress);
    }
  }

  if (progressCallback)
    progressCallback(1.0);

  return true;
#else
  juce::ignoreUnused(audio, numSamples, harmonic, noise, progressCallback);
  return false;
#endif
}
