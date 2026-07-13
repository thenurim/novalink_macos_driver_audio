// This file is part of NovaLINK.
//
// NovaLINK is free software: you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation, either version 2 of the
// License, or (at your option) any later version.
//
// NovaLINK is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with NovaLINK. If not, see <http://www.gnu.org/licenses/>.

//
//  NovaLINKPlayThroughRTLogger.cpp
//  NovaLINKApp
//
//  Copyright © 2020 Kyle Neideck
//

// Self Include
#include "NovaLINKPlayThroughRTLogger.h"

// Local Includes
#include "NovaLINK_Utils.h"

// PublicUtility Includes
#include "CADebugMacros.h"

// STL Includes
#include <atomic>

// System Includes
#include <CoreAudio/CoreAudio.h>
#include <mach/mach_init.h>
#include <mach/task.h>
#include <unistd.h>


#pragma clang assume_nonnull begin

// Track the number of messages logged when built for the unit tests.
#if NovaLINK_UnitTest
    #define LogSync_Debug(inFormat, ...) do { \
                mNumDebugMessagesLogged++; \
                DebugMsg(inFormat, ## __VA_ARGS__); \
            } while (0)
#else
    #define LogSync_Debug(inFormat, ...) DebugMsg(inFormat, ## __VA_ARGS__)
#endif

#pragma mark Construction/Destruction

NovaLINKPlayThroughRTLogger::NovaLINKPlayThroughRTLogger()
{
    // Create the semaphore we use to wake up the logging thread when it has messages to log.
    mWakeUpLoggingThreadSemaphore = CreateSemaphore();

    // Create the logging thread last because it starts immediately and expects the other member
    // variables to be initialised.
    mLoggingThread = std::thread(&NovaLINKPlayThroughRTLogger::LoggingThreadEntry, this);
}

// static
semaphore_t NovaLINKPlayThroughRTLogger::CreateSemaphore()
{
    // TODO: Make a NovaLINKMachSemaphore class to reduce some of this repetitive semaphore code.

    // Create the semaphore.
    semaphore_t semaphore;
    kern_return_t error =
            semaphore_create(mach_task_self(), &semaphore, SYNC_POLICY_FIFO, 0);

    // Check the error code.
    NovaLINK_Utils::ThrowIfMachError("NovaLINKPlayThroughRTLogger::CreateSemaphore",
                                "semaphore_create",
                                error);
    ThrowIf(semaphore == SEMAPHORE_NULL,
            CAException(kAudioHardwareUnspecifiedError),
            "NovaLINKPlayThroughRTLogger::CreateSemaphore: Failed to create semaphore");

    return semaphore;
}

NovaLINKPlayThroughRTLogger::~NovaLINKPlayThroughRTLogger()
{
    // Stop the logging thread.
    mLoggingThreadShouldExit = true;
    kern_return_t error = semaphore_signal(mWakeUpLoggingThreadSemaphore);

    NovaLINK_Utils::LogIfMachError("NovaLINKPlayThroughRTLogger::~NovaLINKPlayThroughRTLogger",
                              "semaphore_signal",
                              error);

    if(error == KERN_SUCCESS)
    {
        // Wait for it to stop.
        mLoggingThread.join();

        // Destroy the semaphore.
        error = semaphore_destroy(mach_task_self(), mWakeUpLoggingThreadSemaphore);
        NovaLINK_Utils::LogIfMachError("NovaLINKPlayThroughRTLogger::~NovaLINKPlayThroughRTLogger",
                                  "semaphore_destroy",
                                  error);
    }
    else
    {
        // If we couldn't tell it to wake up, it's not safe to wait for it to stop or to destroy the
        // semaphore. We have to detach it so its destructor doesn't cause a crash.
        mLoggingThread.detach();
    }
}

#pragma mark Log Messages

void NovaLINKPlayThroughRTLogger::LogReleasingWaitingThreads()
{
    if(!NovaLINKDebugLoggingIsEnabled())
    {
        return;
    }

    if(!mLogReleasingWaitingThreadsMsg.is_lock_free())
    {
        // Modifying mLogReleasingWaitingThreadsMsg might cause the thread to lock a mutex that
        // isn't safe to lock on a realtime thread, so just give up.
        return;
    }

    // Set the flag that tells the logging thread to log the message.
    mLogReleasingWaitingThreadsMsg = true;

    // Wake the logging thread so it can log the message.
    WakeLoggingThread();
}

void NovaLINKPlayThroughRTLogger::LogIfMachError_ReleaseWaitingThreadsSignal(mach_error_t inError)
{
    if(inError == KERN_SUCCESS)
    {
        // No error.
        return;
    }

    if(!mReleaseWaitingThreadsSignalError.is_lock_free())
    {
        // Modifying mReleaseWaitingThreadsSignalError might cause the thread to lock a mutex that
        // isn't safe to lock on a realtime thread, so just give up.
        return;
    }

    mReleaseWaitingThreadsSignalError = inError;
    WakeLoggingThread();
}

void NovaLINKPlayThroughRTLogger::LogIfDroppedFrames(Float64 inFirstInputSampleTime,
                                                Float64 inLastInputSampleTime)
{
    if(inFirstInputSampleTime == inLastInputSampleTime || !NovaLINKDebugLoggingIsEnabled())
    {
        // Either we didn't drop any initial frames or we don't need to log a message about it.
        return;
    }

    LogAsync(mDroppedFrames, [&]()
    {
        // Store the data to include in the log message.
        mDroppedFrames.firstInputSampleTime = inFirstInputSampleTime;
        mDroppedFrames.lastInputSampleTime = inLastInputSampleTime;
    });
}

void NovaLINKPlayThroughRTLogger::LogNoSamplesReady(CARingBuffer::SampleTime inLastInputSampleTime,
                                               CARingBuffer::SampleTime inReadHeadSampleTime,
                                               Float64 inInToOutSampleOffset)
{
    if(!NovaLINKDebugLoggingIsEnabled())
    {
        return;
    }

    LogAsync(mNoSamplesReady, [&]()
    {
        // Store the data to include in the log message.
        mNoSamplesReady.lastInputSampleTime = inLastInputSampleTime;
        mNoSamplesReady.readHeadSampleTime = inReadHeadSampleTime;
        mNoSamplesReady.inToOutSampleOffset = inInToOutSampleOffset;
    });
}

void NovaLINKPlayThroughRTLogger::LogExceptionStoppingIOProc(const char* inCallerName,
                                                        OSStatus inError,
                                                        bool inErrorKnown)
{
    LogAsync(mExceptionStoppingIOProc, [&]()
    {
        // Store the data to include in the log message.
        mExceptionStoppingIOProc.callerName = inCallerName;
        mExceptionStoppingIOProc.error = inError;
        mExceptionStoppingIOProc.errorKnown = inErrorKnown;
    });
}

void NovaLINKPlayThroughRTLogger::LogUnexpectedIOStateAfterStopping(const char* inCallerName,
                                                               int inIOState)
{
    LogAsync(mUnexpectedIOStateAfterStopping, [&]()
    {
        // Store the data to include in the log message.
        mUnexpectedIOStateAfterStopping.callerName = inCallerName;
        mUnexpectedIOStateAfterStopping.ioState = inIOState;
    });
}

void NovaLINKPlayThroughRTLogger::LogRingBufferUnavailable(const char* inCallerName, bool inGotLock)
{
    LogAsync(mRingBufferUnavailable, [&]()
    {
        // Store the data to include in the log message.
        mRingBufferUnavailable.callerName = inCallerName;
        mRingBufferUnavailable.gotLock = inGotLock;
    });
}

void NovaLINKPlayThroughRTLogger::LogIfRingBufferError(CARingBufferError inError,
                                                  std::atomic<CARingBufferError>& outError)
{
    if(inError == kCARingBufferError_OK)
    {
        // No error.
        return;
    }

    if(!outError.is_lock_free())
    {
        // Modifying outError might cause the thread to lock a mutex that isn't safe to lock on
        // a realtime thread, so just give up.
        return;
    }

    // Store the error.
    outError = inError;

    // Wake the logging thread so it can log the error.
    WakeLoggingThread();
}

template <typename T, typename F>
void NovaLINKPlayThroughRTLogger::LogAsync(T& inMessageData, F&& inStoreMessageData)
{
    if(!inMessageData.shouldLogMessage.is_lock_free())
    {
        // Modifying shouldLogMessage might cause the thread to lock a mutex that isn't safe to
        // lock on a realtime thread, so just give up.
        return;
    }

    if(inMessageData.shouldLogMessage)
    {
        // The logging thread could be reading inMessageData.
        return;
    }

    // Store the data to include in the log message.
    //
    // std::forward lets the compiler treat inStoreMessageData as an rvalue if the caller gave it as
    // an rvalue. No idea if that actually does anything.
    std::forward<F>(inStoreMessageData)();

    // shouldLogMessage is a std::atomic, so this store also makes sure that the non-atomic stores
    // in inStoreMessageData will be visible to the logger thread (since the default memory order is
    // memory_order_seq_cst).
    inMessageData.shouldLogMessage = true;

    WakeLoggingThread();
}

void NovaLINKPlayThroughRTLogger::LogSync_Warning(const char* inFormat, ...)
{
    va_list args;
    va_start(args, inFormat);

#if NovaLINK_UnitTest
    mNumWarningMessagesLogged++;
#endif

    vLogWarning(inFormat, args);

    va_end(args);
}

void NovaLINKPlayThroughRTLogger::LogSync_Error(const char* inFormat, ...)
{
    va_list args;
    va_start(args, inFormat);

#if NovaLINK_UnitTest
    mNumErrorMessagesLogged++;

    if(!mContinueOnErrorLogged)
    {
        vLogError(inFormat, args);
    }
#else
    vLogError(inFormat, args);
#endif

    va_end(args);
}

#pragma mark Logging Thread

void NovaLINKPlayThroughRTLogger::WakeLoggingThread()
{
    kern_return_t error = semaphore_signal(mWakeUpLoggingThreadSemaphore);

    NovaLINKAssert(error == KERN_SUCCESS, "semaphore_signal (%d)", error);

    // We can't do anything useful with the error in release builds. At least, not easily.
    (void)error;
}

void NovaLINKPlayThroughRTLogger::LogMessages()
{
    // Log the messages/errors from the realtime threads (if any).
    LogSync_ReleasingWaitingThreads();
    LogSync_ReleaseWaitingThreadsSignalError();
    LogSync_DroppedFrames();
    LogSync_NoSamplesReady();
    LogSync_ExceptionStoppingIOProc();
    LogSync_UnexpectedIOStateAfterStopping();
    LogSync_RingBufferUnavailable();
    LogSync_RingBufferError(mRingBufferStoreError, "InputDeviceIOProc");
    LogSync_RingBufferError(mRingBufferFetchError, "OutputDeviceIOProc");
}

void NovaLINKPlayThroughRTLogger::LogSync_ReleasingWaitingThreads()
{
    if(mLogReleasingWaitingThreadsMsg)
    {
        LogSync_Debug("NovaLINKPlayThrough::ReleaseThreadsWaitingForOutputToStart: "
                      "Releasing waiting threads");
        // Reset it.
        mLogReleasingWaitingThreadsMsg = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_ReleaseWaitingThreadsSignalError()
{
    if(mReleaseWaitingThreadsSignalError != KERN_SUCCESS)
    {
        NovaLINK_Utils::LogIfMachError("NovaLINKPlayThrough::ReleaseThreadsWaitingForOutputToStart",
                                  "semaphore_signal_all",
                                  mReleaseWaitingThreadsSignalError);
        // Reset it.
        mReleaseWaitingThreadsSignalError = KERN_SUCCESS;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_DroppedFrames()
{
    if(mDroppedFrames.shouldLogMessage)
    {
        LogSync_Debug("NovaLINKPlayThrough::OutputDeviceIOProc: "
                      "Dropped %f frames before output started. %s%f %s%f",
                      (mDroppedFrames.lastInputSampleTime - mDroppedFrames.firstInputSampleTime),
                      "mFirstInputSampleTime=",
                      mDroppedFrames.firstInputSampleTime,
                      "mLastInputSampleTime=",
                      mDroppedFrames.lastInputSampleTime);
        mDroppedFrames.shouldLogMessage = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_NoSamplesReady()
{
    if(mNoSamplesReady.shouldLogMessage)
    {
        LogSync_Debug("NovaLINKPlayThrough::OutputDeviceIOProc: "
                      "No input samples ready at output sample time. %s%lld %s%lld %s%f",
                      "lastInputSampleTime=", mNoSamplesReady.lastInputSampleTime,
                      "readHeadSampleTime=", mNoSamplesReady.readHeadSampleTime,
                      "mInToOutSampleOffset=", mNoSamplesReady.inToOutSampleOffset);
        mNoSamplesReady.shouldLogMessage = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_ExceptionStoppingIOProc()
{
    if(mExceptionStoppingIOProc.shouldLogMessage)
    {
        const char error4CC[5] = CA4CCToCString(mExceptionStoppingIOProc.error);
        LogSync_Error("NovaLINKPlayThrough::UpdateIOProcState: "
                      "Exception while stopping IOProc %s: %s (%d)",
                      mExceptionStoppingIOProc.callerName,
                      mExceptionStoppingIOProc.errorKnown ? error4CC : "unknown",
                      mExceptionStoppingIOProc.error);
        mExceptionStoppingIOProc.shouldLogMessage = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_UnexpectedIOStateAfterStopping()
{
    if(mUnexpectedIOStateAfterStopping.shouldLogMessage)
    {
        LogSync_Warning("NovaLINKPlayThrough::UpdateIOProcState: "
                        "%s IO state changed since last read. state = %d",
                        mUnexpectedIOStateAfterStopping.callerName,
                        mUnexpectedIOStateAfterStopping.ioState);
        mUnexpectedIOStateAfterStopping.shouldLogMessage = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_RingBufferUnavailable()
{
    if(mRingBufferUnavailable.shouldLogMessage)
    {
        LogSync_Warning("NovaLINKPlayThrough::%s: Ring buffer unavailable. %s",
                        mRingBufferUnavailable.callerName,
                        mRingBufferUnavailable.gotLock ?
                            "No buffer currently allocated." :
                            "Buffer locked for allocation/deallocation by another thread.");
        mRingBufferUnavailable.shouldLogMessage = false;
    }
}

void NovaLINKPlayThroughRTLogger::LogSync_RingBufferError(
        std::atomic<CARingBufferError>& ioRingBufferError,
        const char* inMethodName)
{
    CARingBufferError error = ioRingBufferError;

    switch(error)
    {
        case kCARingBufferError_OK:
            // No error.
            return;
        case kCARingBufferError_CPUOverload:
            // kCARingBufferError_CPUOverload might not be our fault, so just log a warning.
            LogSync_Warning("NovaLINKPlayThrough::%s: Ring buffer error: "
                            "kCARingBufferError_CPUOverload (%d)",
                            inMethodName,
                            error);
            break;
        default:
            // Other types of CARingBuffer errors should never occur. This will crash debug builds.
            LogSync_Error("NovaLINKPlayThrough::%s: Ring buffer error: %s (%d)",
                          inMethodName,
                          (error == kCARingBufferError_TooMuch ?
                                  "kCARingBufferError_TooMuch" :
                                  "unknown error"),
                          error);
            break;
    };

    // Reset it.
    ioRingBufferError = kCARingBufferError_OK;
}

// static
void* __nullable NovaLINKPlayThroughRTLogger::LoggingThreadEntry(NovaLINKPlayThroughRTLogger* inRefCon)
{
    DebugMsg("NovaLINKPlayThroughRTLogger::IOProcLoggingThreadEntry: "
             "Starting the IOProc logging thread");

    while(!inRefCon->mLoggingThreadShouldExit)
    {
        // Log the messages, if there are any to log.
        inRefCon->LogMessages();

        // Wait until woken up.
        kern_return_t error = semaphore_wait(inRefCon->mWakeUpLoggingThreadSemaphore);
        NovaLINK_Utils::LogIfMachError("NovaLINKPlayThroughRTLogger::IOProcLoggingThreadEntry",
                                  "semaphore_wait",
                                  error);
    }

    DebugMsg("NovaLINKPlayThroughRTLogger::IOProcLoggingThreadEntry: IOProc logging thread exiting");

    return nullptr;
}

#if NovaLINK_UnitTest

#pragma mark Test Helpers

bool NovaLINKPlayThroughRTLogger::WaitUntilLoggerThreadIdle()
{
    int msWaited = 0;

    while(mLogReleasingWaitingThreadsMsg ||
            mReleaseWaitingThreadsSignalError != KERN_SUCCESS ||
            mDroppedFrames.shouldLogMessage ||
            mNoSamplesReady.shouldLogMessage ||
            mUnexpectedIOStateAfterStopping.shouldLogMessage ||
            mRingBufferUnavailable.shouldLogMessage ||
            mExceptionStoppingIOProc.shouldLogMessage ||
            mRingBufferStoreError != kCARingBufferError_OK ||
            mRingBufferFetchError != kCARingBufferError_OK)
    {
        // Poll until the logger thread has nothing left to log. (Ideally we'd use a semaphore
        // instead of polling, but it isn't worth the effort at this point.)
        usleep(10 * 1000);
        msWaited += 10;

        // Time out after 5 seconds.
        if(msWaited > 5000)
        {
            return false;
        }
    }

    return true;
}

#endif /* NovaLINK_UnitTest */

#pragma clang assume_nonnull end

