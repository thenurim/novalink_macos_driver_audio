#include "NovaLINK_AudioCapture.h"

#include <cstring>

#include <mach/mach.h>
#include <pthread/qos.h>

NovaLINK_AudioCapture::NovaLINK_AudioCapture() {
    kern_return_t err = semaphore_create(mach_task_self(), &mDataAvailable, SYNC_POLICY_FIFO, 0);
    if(err != KERN_SUCCESS) {
        mDataAvailable = SEMAPHORE_NULL;
    }
}

NovaLINK_AudioCapture::~NovaLINK_AudioCapture() {
    StopCapture();
    if(mDataAvailable != SEMAPHORE_NULL) {
        semaphore_destroy(mach_task_self(), mDataAvailable);
        mDataAvailable = SEMAPHORE_NULL;
    }
}

void NovaLINK_AudioCapture::StartCapture(AudioDataCallback callback) {
    if(mIsCapturing.load(std::memory_order_acquire)) {
        return;
    }

    mCallback = std::move(callback);
    mWriteSeq.store(0, std::memory_order_relaxed);
    mReadSeq.store(0, std::memory_order_relaxed);

    mIsCapturing.store(true, std::memory_order_release);

    mProcessingThread = std::thread([this]() {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
        ProcessAudioData();
    });
}

void NovaLINK_AudioCapture::StopCapture() {
    if(!mIsCapturing.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if(mDataAvailable != SEMAPHORE_NULL) {
        semaphore_signal(mDataAvailable);
    }

    if(mProcessingThread.joinable()) {
        mProcessingThread.join();
    }

    mCallback = nullptr;
    mWriteSeq.store(0, std::memory_order_relaxed);
    mReadSeq.store(0, std::memory_order_relaxed);
}

void NovaLINK_AudioCapture::EnqueueAudioData(const Float32* buffer, UInt32 frameCount) {
    if(!mIsCapturing.load(std::memory_order_acquire) || buffer == nullptr || frameCount == 0) {
        return;
    }

    const UInt32 framesToCopy = frameCount < static_cast<UInt32>(kMaxFramesPerChunk)
            ? frameCount
            : static_cast<UInt32>(kMaxFramesPerChunk);
    const size_t byteCount = static_cast<size_t>(framesToCopy) * sizeof(Float32) * 2;

    const uint32_t writeSeq = mWriteSeq.load(std::memory_order_relaxed);
    const uint32_t readSeq = mReadSeq.load(std::memory_order_acquire);

    // Queue full: drop this chunk so latency stays bounded (consumer owns mReadSeq).
    if(writeSeq - readSeq >= kSlotCount) {
        return;
    }

    const uint32_t slotIndex = writeSeq % kSlotCount;
    Slot& slot = mSlots[slotIndex];

    std::memcpy(slot.samples, buffer, byteCount);
    slot.frameCount.store(framesToCopy, std::memory_order_release);
    mWriteSeq.store(writeSeq + 1, std::memory_order_release);

    if(mDataAvailable != SEMAPHORE_NULL) {
        semaphore_signal(mDataAvailable);
    }
}

void NovaLINK_AudioCapture::DrainAvailableChunks() {
    const uint32_t readSeq = mReadSeq.load(std::memory_order_relaxed);
    const uint32_t writeSeq = mWriteSeq.load(std::memory_order_acquire);

    if(readSeq >= writeSeq) {
        return;
    }

    // If we fell behind, deliver only the newest chunk to minimize end-to-end latency.
    const uint32_t deliverSeq = writeSeq - 1;
    const uint32_t slotIndex = deliverSeq % kSlotCount;
    const Slot& slot = mSlots[slotIndex];
    const UInt32 frameCount = slot.frameCount.load(std::memory_order_acquire);

    if(frameCount > 0 && mCallback) {
        mCallback(slot.samples, frameCount);
    }

    mReadSeq.store(writeSeq, std::memory_order_release);
}

void NovaLINK_AudioCapture::ProcessAudioData() {
    while(mIsCapturing.load(std::memory_order_acquire)) {
        if(mDataAvailable != SEMAPHORE_NULL) {
            semaphore_wait(mDataAvailable);
        } else {
            std::this_thread::yield();
        }

        if(!mIsCapturing.load(std::memory_order_acquire)) {
            break;
        }

        DrainAvailableChunks();
    }

    DrainAvailableChunks();
}
