#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include <CoreAudio/CoreAudioTypes.h>
#include <mach/semaphore.h>

// Low-latency capture path from the driver's real-time IO thread.
// RT thread only memcpy's into pre-allocated slots; a dedicated thread invokes the callback.
class NovaLINK_AudioCapture {
public:
    using AudioDataCallback = std::function<void(const Float32* samples, UInt32 frameCount)>;

    enum {
        kMaxFramesPerChunk = 4096,  // max HAL IO buffer (frames); larger buffers are truncated
        kSlotCount = 2              // double-buffer; consumer keeps only the newest chunk
    };

    NovaLINK_AudioCapture();
    ~NovaLINK_AudioCapture();

    void StartCapture(AudioDataCallback callback);
    void StopCapture();

    // Called from the driver's real-time IO path — must not allocate or block.
    void EnqueueAudioData(const Float32* buffer, UInt32 frameCount);

    bool IsCapturing() const { return mIsCapturing.load(std::memory_order_acquire); }

private:
    struct Slot {
        std::atomic<UInt32> frameCount{0};
        Float32 samples[kMaxFramesPerChunk * 2];
    };

    void ProcessAudioData();
    void DrainAvailableChunks();

    AudioDataCallback mCallback;
    Slot mSlots[kSlotCount];
    std::atomic<uint32_t> mWriteSeq{0};
    std::atomic<uint32_t> mReadSeq{0};
    std::atomic<bool> mIsCapturing{false};
    semaphore_t mDataAvailable = SEMAPHORE_NULL;
    std::thread mProcessingThread;
};
