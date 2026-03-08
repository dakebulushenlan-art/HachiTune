#pragma once

// GPU execution provider types for ONNX Runtime inference
enum class GPUProvider
{
    CPU = 0,
    CUDA,     // NVIDIA GPU
    DirectML, // Windows DirectX 12 (AMD/Intel/NVIDIA)
    CoreML    // Apple Neural Engine / GPU (macOS/iOS)
};
