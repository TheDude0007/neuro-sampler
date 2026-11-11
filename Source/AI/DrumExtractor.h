#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// AI source separation with Demucs (htdemucs models, MIT) through demucs.cpp (MIT, Eigen): pulls drums (or
// all four stems: drums, bass, other, vocals) out of a full mix. Runs in-process, no Python. Slow (seconds per second of audio): call from a worker thread.
// This header is Eigen-free; the implementation is compiled in its own library with third-party warnings off.
namespace neuro::demucs
{

inline constexpr int sampleRate = 44100; // the model's rate: resample before and after

struct Job
{
    std::atomic<float> progress { 0.0f }; // 0..1
    std::atomic<bool> cancel { false };
};

// Stereo planar input at 44.1 kHz. Long input is split across threads (overlapping, cross-faded). The model file is
// loaded once and cached. Returns false on error or cancel (error says which).
bool extractDrums (const std::string& modelFile, const float* left, const float* right, int64_t numSamples,
                   std::vector<float>& outLeft, std::vector<float>& outRight, Job& job, std::string& error);

// htdemucs 4-source order.
enum Target { drums = 0, bass = 1, other = 2, vocals = 3 };

// The same for any targets: out gets 2 channels (left, right) per requested target, in the order requested.
bool separate (const std::string& modelFile, const float* left, const float* right, int64_t numSamples,
               const std::vector<int>& targets, std::vector<std::vector<float>>& out, Job& job, std::string& error);

} // namespace neuro::demucs
