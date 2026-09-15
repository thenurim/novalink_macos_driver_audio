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
//  NovaLINKPlayThrough.cpp
//  NovaLINKApp
//
//  Copyright © 2016, 2017, 2020 Kyle Neideck
//

// Self Include
#include "NovaLINKPlayThrough.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Utils.h"
#include "NovaLINKMicrophoneAccess.h"

// PublicUtility Includes
#include "CAHALAudioSystemObject.h"
#include "CAPropertyAddress.h"

// STL Includes
#include <algorithm>  // For std::max
#include <cmath>

// System Includes
#include <mach/mach_init.h>
#include <mach/mach_time.h>
#include <mach/task.h>


// The number of IO cycles (roughly) to wait for our IOProcs to stop themselves before assuming something
// went wrong. If that happens, we try to stop them from a non-IO thread and continue anyway. 
static const UInt32 kStopIOProcTimeoutInIOCycles = 600;

#pragma mark Construction/Destruction

NovaLINKPlayThrough::NovaLINKPlayThrough(NovaLINKAudioDevice inInputDevice, NovaLINKAudioDevice inOutputDevice)
:
    mInputDevice(inInputDevice),
    mOutputDevice(inOutputDevice)
{
    Init(inInputDevice, inOutputDevice);
}

NovaLINKPlayThrough::~NovaLINKPlayThrough()
{
    CAMutex::Locker stateLocker(mStateMutex);

    NovaLINKLogAndSwallowExceptionsMsg("NovaLINKPlayThrough::~NovaLINKPlayThrough", "Deactivate", [&]() {
        Deactivate();
    });

    // If one of the IOProcs failed to stop, CoreAudio could (at least in theory) still call it
    // after this point. This isn't a solution, but calling DeallocateBuffer instead of letting it
    // deallocate itself should at least make the error less likely to cause a segfault, since
    // DeallocateBuffer takes the buffer locks and sets mBuffer to null.
    //
    // TODO: It probably wouldn't be too hard to fix this properly by giving the IOProcs weak refs
    //       to the NovaLINKPlayThrough object instead of raw pointers.
    DeallocateBuffer();
    
    if(mOutputDeviceIOProcSemaphore != SEMAPHORE_NULL)
    {
        kern_return_t theError = semaphore_destroy(mach_task_self(), mOutputDeviceIOProcSemaphore);
        NovaLINK_Utils::LogIfMachError("NovaLINKPlayThrough::~NovaLINKPlayThrough", "semaphore_destroy", theError);
    }
}

void    NovaLINKPlayThrough::Init(NovaLINKAudioDevice inInputDevice, NovaLINKAudioDevice inOutputDevice)
{
    NovaLINKAssert(mInputDeviceIOProcState.is_lock_free(),
              "NovaLINKPlayThrough::NovaLINKPlayThrough: !mInputDeviceIOProcState.is_lock_free()");
    NovaLINKAssert(mOutputDeviceIOProcState.is_lock_free(),
              "NovaLINKPlayThrough::NovaLINKPlayThrough: !mOutputDeviceIOProcState.is_lock_free()");
    NovaLINKAssert(!mActive, "NovaLINKPlayThrough::NovaLINKPlayThrough: Can't init while active.");
    
    mInputDevice = inInputDevice;
    mOutputDevice = inOutputDevice;
    
    AllocateBuffer();
    
    try
    {
        // Init the semaphore for the output IOProc.
        if(mOutputDeviceIOProcSemaphore == SEMAPHORE_NULL)
        {
            kern_return_t theError = semaphore_create(mach_task_self(), &mOutputDeviceIOProcSemaphore, SYNC_POLICY_FIFO, 0);
            NovaLINK_Utils::ThrowIfMachError("NovaLINKPlayThrough::NovaLINKPlayThrough", "semaphore_create", theError);
            
            ThrowIf(mOutputDeviceIOProcSemaphore == SEMAPHORE_NULL,
                    CAException(kAudioHardwareUnspecifiedError),
                    "NovaLINKPlayThrough::NovaLINKPlayThrough: Could not create semaphore");
        }
    }
    catch (...)
    {
        // Clean up.
        DeallocateBuffer();
        throw;
    }
}

void    NovaLINKPlayThrough::Activate()
{
    CAMutex::Locker stateLocker(mStateMutex);
    
    if(!mActive)
    {
        DebugMsg("NovaLINKPlayThrough::Activate: Activating playthrough");
        
        CreateIOProcIDs();
        
        mActive = true;
        
        // Prefer keeping NovaLINK's sample rate/buffer stable while other apps (Chrome, etc.) are
        // playing through it. Changing NovaLINK mid-stream makes the client audio clock jump, which
        // shows up as YouTube playing in slow-motion then suddenly catching up after the next
        // device switch. Try to drive the real output at NovaLINK's rate first; only change NovaLINK
        // when the output can't follow.
        //
        // Exception: Bluetooth / BLE / AirPlay often accept SetNominalSampleRate (or update
        // Nominal) while Actual stays at another rate (commonly 44.1 kHz). PlayThrough has no
        // resampler, so that mismatch plays as slow / "monster" audio. Those transports must
        // remain the rate master — always match NovaLINK to them.
        const bool clientsPlaying = [&]() -> bool {
            try {
                return mInputDevice.IsNovaLINKDeviceInstance()
                        && IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice);
            } catch (...) {
                return false;
            }
        }();

        const bool outputMustBeRateMaster = [&]() -> bool {
            try {
                switch(mOutputDevice.GetTransportType())
                {
                    case kAudioDeviceTransportTypeBluetooth:
                    case kAudioDeviceTransportTypeBluetoothLE:
                    case kAudioDeviceTransportTypeAirPlay:
                        return true;
                    default:
                        return false;
                }
            } catch (...) {
                return false;
            }
        }();

        try
        {
            Float64 outputSampleRate = mOutputDevice.GetNominalSampleRate();
            // BT/AirPlay Nominal can disagree with the hardware clock. Prefer Actual when known.
            if(outputMustBeRateMaster)
            {
                try
                {
                    const Float64 outputActual = mOutputDevice.GetActualSampleRate();
                    if(outputActual > 1.0
                       && std::fabs(outputActual - outputSampleRate) > 0.5)
                    {
                        DebugMsg("NovaLINKPlayThrough::Activate: Using output Actual sample rate "
                                 "%.0f (Nominal was %.0f)",
                                 outputActual,
                                 outputSampleRate);
                        outputSampleRate = outputActual;
                    }
                }
                catch (...)
                {
                    // Actual may be unavailable before IO; keep Nominal.
                }
            }

            Float64 inputSampleRate = mInputDevice.GetNominalSampleRate();

            if(std::fabs(outputSampleRate - inputSampleRate) > 0.5)
            {
                if(clientsPlaying && !outputMustBeRateMaster)
                {
                    bool matchedOutputToNovaLINK = false;
                    try
                    {
                        DebugMsg("NovaLINKPlayThrough::Activate: Clients playing — matching output "
                                 "sample rate (%.0f) to NovaLINK (%.0f)",
                                 outputSampleRate,
                                 inputSampleRate);
                        mOutputDevice.SetNominalSampleRate(inputSampleRate);

                        // Some devices "succeed" without actually changing rate.
                        // Re-read Nominal; if it didn't stick, fall back to matching NovaLINK.
                        const Float64 outputAfterSet = mOutputDevice.GetNominalSampleRate();
                        if(std::fabs(outputAfterSet - inputSampleRate) <= 0.5)
                        {
                            matchedOutputToNovaLINK = true;
                        }
                        else
                        {
                            LogWarning("NovaLINKPlayThrough::Activate: Output Nominal stayed at %f "
                                       "after requesting %f; matching NovaLINK to output instead",
                                       outputAfterSet,
                                       inputSampleRate);
                            outputSampleRate = outputAfterSet;
                        }
                    }
                    catch (CAException e)
                    {
                        LogWarning("NovaLINKPlayThrough::Activate: Output rejected NovaLINK sample "
                                   "rate %f (err %d); matching NovaLINK to output %f instead",
                                   inputSampleRate,
                                   e.GetError(),
                                   outputSampleRate);
                    }

                    if(!matchedOutputToNovaLINK)
                    {
                        mInputDevice.SetNominalSampleRate(outputSampleRate);
                    }
                }
                else
                {
                    if(outputMustBeRateMaster)
                    {
                        DebugMsg("NovaLINKPlayThrough::Activate: Output transport requires rate "
                                 "master — matching NovaLINK (%.0f) to output (%.0f)",
                                 inputSampleRate,
                                 outputSampleRate);
                    }
                    mInputDevice.SetNominalSampleRate(outputSampleRate);
                }
            }
        }
        catch (CAException e)
        {
            LogWarning("NovaLINKPlayThrough::Activate: Failed to sync device sample rates. Error: %d",
                       e.GetError());
        }
        
        // Same policy for IO buffer size.
        try
        {
            UInt32 outputBufferSize = mOutputDevice.GetIOBufferSize();
            UInt32 inputBufferSize = mInputDevice.GetIOBufferSize();

            if(outputBufferSize != inputBufferSize)
            {
                if(clientsPlaying && !outputMustBeRateMaster)
                {
                    bool matchedOutputToNovaLINK = false;
                    try
                    {
                        mOutputDevice.SetIOBufferSize(inputBufferSize);
                        if(mOutputDevice.GetIOBufferSize() == inputBufferSize)
                        {
                            matchedOutputToNovaLINK = true;
                        }
                        else
                        {
                            outputBufferSize = mOutputDevice.GetIOBufferSize();
                            LogWarning("NovaLINKPlayThrough::Activate: Output buffer size stayed "
                                       "at %u after requesting %u; matching NovaLINK instead",
                                       outputBufferSize,
                                       inputBufferSize);
                        }
                    }
                    catch (CAException e)
                    {
                        LogWarning("NovaLINKPlayThrough::Activate: Output rejected NovaLINK buffer "
                                   "size %u (err %d); matching NovaLINK to output %u instead",
                                   inputBufferSize,
                                   e.GetError(),
                                   outputBufferSize);
                    }

                    if(!matchedOutputToNovaLINK)
                    {
                        mInputDevice.SetIOBufferSize(outputBufferSize);
                    }
                }
                else
                {
                    mInputDevice.SetIOBufferSize(outputBufferSize);
                }
            }
        }
        catch (CAException e)
        {
            LogWarning("NovaLINKPlayThrough::Activate: Failed to sync device buffer sizes. Error: %d",
                       e.GetError());
        }
        
        DebugMsg("NovaLINKPlayThrough::Activate: Registering for notifications from NovaLINKDevice.");
        
        mInputDevice.AddPropertyListener(CAPropertyAddress(kAudioDevicePropertyDeviceIsRunning),
                                         &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                         this);
        mInputDevice.AddPropertyListener(CAPropertyAddress(kAudioDeviceProcessorOverload),
                                         &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                         this);
        
        bool isNovaLINKDevice = true;
        CATry
        isNovaLINKDevice = mInputDevice.IsNovaLINKDeviceInstance();
        CACatch
        
        if(isNovaLINKDevice)
        {
            mInputDevice.AddPropertyListener(kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress,
                                             &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                             this);
        }
        else
        {
            LogWarning("NovaLINKPlayThrough::Activate: Playthrough activated with an output device other "
                       "than NovaLINKDevice. This hasn't been tested and is almost definitely a bug.");
            NovaLINKAssert(false, "NovaLINKPlayThrough::Activate: !mInputDevice.IsNovaLINKDeviceInstance()");
        }
    }
}

void    NovaLINKPlayThrough::Deactivate()
{
    CAMutex::Locker stateLocker(mStateMutex);
    
    if(mActive)
    {
        DebugMsg("NovaLINKPlayThrough::Deactivate: Deactivating playthrough");
        
        bool inputDeviceIsNovaLINKDevice = true;

        CATry
        inputDeviceIsNovaLINKDevice = mInputDevice.IsNovaLINKDeviceInstance();
        CACatch
        
        // Unregister notification listeners.
        if(inputDeviceIsNovaLINKDevice)
        {
            // There's not much we can do if these calls throw. The docs for AudioObjectRemovePropertyListener
            // just say that means it failed.
            NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Deactivate", [&] {
                mInputDevice.RemovePropertyListener(CAPropertyAddress(kAudioDevicePropertyDeviceIsRunning),
                                                    &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                                    this);
            });
            
            NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Deactivate", [&] {
                mInputDevice.RemovePropertyListener(CAPropertyAddress(kAudioDeviceProcessorOverload),
                                                    &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                                    this);
            });
            
            NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Deactivate", [&] {
                mInputDevice.RemovePropertyListener(kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress,
                                                    &NovaLINKPlayThrough::NovaLINKDeviceListenerProc,
                                                    this);
            });
        }

        NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Deactivate", [&] {
            Stop();
        });
        
        NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Deactivate", [&] {
            DestroyIOProcIDs();
        });
        
        mActive = false;
    }
}

void    NovaLINKPlayThrough::AllocateBuffer()
{
    // Allocate the ring buffer that will hold the data passing between the devices
    UInt32 numberStreams = 1;
    AudioStreamBasicDescription outputFormat[1];
    mOutputDevice.GetCurrentVirtualFormats(false, numberStreams, outputFormat);
    
    if(numberStreams < 1)
    {
        Throw(CAException(kAudioHardwareUnsupportedOperationError));
    }
    
    // Need to lock the buffer mutexes to make sure the IOProcs aren't accessing it. The order is
    // important here. We always lock them in the same order to prevent deadlocks.
    CAMutex::Locker lockerInput(mBufferInputMutex);
    CAMutex::Locker lockerOutput(mBufferOutputMutex);

    mBuffer = std::unique_ptr<CARingBuffer>(new CARingBuffer);

    // The calculation for the size of the buffer is from Apple's CAPlayThrough.cpp sample code
    //
    // TODO: Test playthrough with hardware with more than 2 channels per frame, a sample (virtual) format other than
    //       32-bit floats and/or an IO buffer size other than 512 frames
    // Match Apple's CAPlayThrough multiplier (20). A smaller buffer (e.g. 8) underflows easily with
    // Bluetooth/AirPlay clock drift and device start latency.
    static const UInt32 kRingBufferFrameMultiplier = 20;
    mBuffer->Allocate(outputFormat[0].mChannelsPerFrame,
                      outputFormat[0].mBytesPerFrame,
                      mOutputDevice.GetIOBufferSize() * kRingBufferFrameMultiplier);
}

void    NovaLINKPlayThrough::DeallocateBuffer()
{
    // Need to lock the buffer mutexes to make sure the IOProcs aren't accessing it. The order is
    // important here. We always lock them in the same order to prevent deadlocks.
    CAMutex::Locker lockerInput(mBufferInputMutex);
    CAMutex::Locker lockerOutput(mBufferOutputMutex);
    mBuffer = nullptr;  // Note that the buffer's destructor will deallocate it.
}

void    NovaLINKPlayThrough::CreateIOProcIDs()
{
    CAMutex::Locker stateLocker(mStateMutex);
    
    NovaLINKAssert(!mPlayingThrough,
              "NovaLINKPlayThrough::CreateIOProcIDs: Tried to create IOProcs when playthrough was already running");
    NovaLINKAssert(mInputDeviceIOProcID == nullptr,
              "NovaLINKPlayThrough::CreateIOProcIDs: mInputDeviceIOProcID must be destroyed first.");
    NovaLINKAssert(mOutputDeviceIOProcID == nullptr,
              "NovaLINKPlayThrough::CreateIOProcIDs: mOutputDeviceIOProcID must be destroyed first.");
    NovaLINKAssert(CheckIOProcsAreStopped(),
              "NovaLINKPlayThrough::CreateIOProcIDs: IOProcs not ready.");
    
    const bool inDeviceAlive = mInputDevice.IsAlive();
    const bool outDeviceAlive = mOutputDevice.IsAlive();
    
    if(inDeviceAlive && outDeviceAlive)
    {
        DebugMsg("NovaLINKPlayThrough::CreateIOProcIDs: Creating IOProcs");
    
        try
        {
            mInputDeviceIOProcID = mInputDevice.CreateIOProcID(&NovaLINKPlayThrough::InputDeviceIOProc, this);
        }
        catch(CAException e)
        {
            LogWarning("NovaLINKPlayThrough::CreateIOProcIDs: Failed to create input IOProc ID. mInputDevice = %d",
                       mInputDevice.GetObjectID());
            throw;
        }
        
        try
        {
            mOutputDeviceIOProcID = mOutputDevice.CreateIOProcID(&NovaLINKPlayThrough::OutputDeviceIOProc, this);
        }
        catch(CAException e)
        {
            LogWarning("NovaLINKPlayThrough::CreateIOProcIDs: Failed to create output IOProc ID. mOutputDevice = %d",
                       mOutputDevice.GetObjectID());
            DestroyIOProcIDs(); // Clean up.
            throw;
        }

        if(mInputDeviceIOProcID == nullptr || mOutputDeviceIOProcID == nullptr)
        {
            // Should never happen if CAHALAudioDevice::CreateIOProcID didn't throw.
            LogError("NovaLINKPlayThrough::CreateIOProcIDs: Null IOProc ID returned by CreateIOProcID");
            throw new CAException(kAudioHardwareIllegalOperationError);
        }
        
        // TODO: Try using SetIOCycleUsage to reduce latency? Our IOProcs don't really do anything except copy a small
        //       buffer. According to this, Jack OS X considered it:
        //       https://lists.apple.com/archives/coreaudio-api/2008/Mar/msg00043.html but from a quick look at their
        //       code, I don't think they ended up using it.
        // mInputDevice->SetIOCycleUsage(0.01f);
        // mOutputDevice->SetIOCycleUsage(0.01f);
    }
    else
    {
        LogWarning("NovaLINKPlayThrough::CreateIOProcIDs: Failed to create IOProcs.%s%s",
                   (inDeviceAlive ? "" : " Input device not alive."),
                   (outDeviceAlive ? "" : " Output device not alive."));
        throw new CAException(kAudioHardwareIllegalOperationError);
    }
}

void    NovaLINKPlayThrough::DestroyIOProcIDs()
{
    CAMutex::Locker stateLocker(mStateMutex);

    // In release builds, we still try to destroy the IDs if the IOProcs are running, hoping they just haven't been
    // stopped quite yet. The docs for AudioDeviceDestroyIOProcID don't say not to do that, but it could cause races
    // if one really is still running so it isn't ideal.
    NovaLINKAssert(CheckIOProcsAreStopped(), "NovaLINKPlayThrough::DestroyIOProcIDs: IOProcs not ready.");

    DebugMsg("NovaLINKPlayThrough::DestroyIOProcIDs: Destroying IOProcs");

    auto destroy = [](NovaLINKAudioDevice& device, const char* deviceName, AudioDeviceIOProcID& ioProcID) {
#if !DEBUG
    #pragma unused (deviceName)
#endif
        if(ioProcID != nullptr)
        {
            try
            {
                device.DestroyIOProcID(ioProcID);
            }
            catch(CAException e)
            {
                if((e.GetError() == kAudioHardwareBadDeviceError) || (e.GetError() == kAudioHardwareBadObjectError))
                {
                    // This means the IOProc IDs will have already been destroyed, so there's nothing to do.
                    DebugMsg("NovaLINKPlayThrough::DestroyIOProcIDs: Didn't destroy IOProc ID for %s device because "
                             "it's not connected anymore. deviceID = %d",
                             deviceName,
                             device.GetObjectID());
                }
                else
                {
                    ioProcID = nullptr;
                    throw;
                }
            }
            
            ioProcID = nullptr;
        }
    };
    
    destroy(mInputDevice, "input", mInputDeviceIOProcID);
    destroy(mOutputDevice, "output", mOutputDeviceIOProcID);
}

bool    NovaLINKPlayThrough::CheckIOProcsAreStopped() const noexcept
{
    bool statesOK = true;
    
    if(mInputDeviceIOProcState != IOState::Stopped)
    {
        LogWarning("NovaLINKPlayThrough::CheckIOProcsAreStopped: Input IOProc not stopped. mInputDeviceIOProcState = %d",
                   mInputDeviceIOProcState.load());
        statesOK = false;
    }
    
    if(mOutputDeviceIOProcState != IOState::Stopped)
    {
        LogWarning("NovaLINKPlayThrough::CheckIOProcsAreStopped: Output IOProc not stopped. mOutputDeviceIOProcState = %d",
                   mOutputDeviceIOProcState.load());
        statesOK = false;
    }
    
    return statesOK;
}

void    NovaLINKPlayThrough::SetDevices(const NovaLINKAudioDevice* __nullable inInputDevice,
                                   const NovaLINKAudioDevice* __nullable inOutputDevice)
{
    bool wasActive;
    bool wasPlayingThrough;

    {
        CAMutex::Locker stateLocker(mStateMutex);

        wasActive = mActive;
        wasPlayingThrough = mPlayingThrough;

        if(wasPlayingThrough)
        {
            NovaLINKAssert(wasActive, "NovaLINKPlayThrough::SetOutputDevice: wasPlayingThrough && !wasActive");  // Sanity check.
        }

        Deactivate();

        mInputDevice = inInputDevice ? *inInputDevice : mInputDevice;
        mOutputDevice = inOutputDevice ? *inOutputDevice : mOutputDevice;

        // Resize and reallocate the buffer if necessary.
        Init(mInputDevice, mOutputDevice);

        if(wasActive)
        {
            Activate();
        }
    }

    // Start outside the state lock — StartIOProc must not run while mStateMutex is held.
    if(wasPlayingThrough)
    {
        Start();
    }
}

#pragma mark Control Playthrough

void    NovaLINKPlayThrough::Start()
{
    AudioDeviceIOProcID inputProcID = nullptr;
    AudioDeviceIOProcID outputProcID = nullptr;
    bool restartAfterPartialStop = false;

    // Prepare under the state lock, but do not call StartIOProc while holding it. StartIOProc can
    // block for a long time (Bluetooth) and can re-enter the HAL while a client StartIO on
    // NovaLINKDevice is still unwinding — holding mStateMutex across that widens the deadlock window.
    {
        CAMutex::Locker stateLocker(mStateMutex);

        if(mPlayingThrough)
        {
            const IOState outputState = mOutputDeviceIOProcState;
            const IOState inputState = mInputDeviceIOProcState;

            // Healthy playthrough — nothing to do.
            if(outputState == IOState::Running && inputState == IOState::Running)
            {
                DebugMsg("NovaLINKPlayThrough::Start: Already running.");
                // If a non-App client is already playing, arm idle-stop so we can stop later
                // when they leave. (Start normally disarms idle-stop for device-switch races.)
                NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::Start", [&] {
                    if(IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice))
                    {
                        mIdleStopArmed = true;
                    }
                });
                ReleaseThreadsWaitingForOutputToStart();
                return;
            }

            // StartIOProc still in flight on another thread.
            if(outputState == IOState::Starting || inputState == IOState::Starting)
            {
                DebugMsg("NovaLINKPlayThrough::Start: Already starting.");
                return;
            }

            // Classic failure mode with Bluetooth: StopIfIdle / BT idle suspend leaves
            // mPlayingThrough true and the NovaLINK input IOProc alive, but the real output
            // IOProc is dead. A naive early-return then leaves Chrome/YouTube writing into a
            // black hole. Tear down and restart.
            LogWarning("NovaLINKPlayThrough::Start: Playthrough marked active but IOProcs are "
                       "not running (input=%d output=%d). Restarting.",
                       (int)inputState,
                       (int)outputState);
            restartAfterPartialStop = true;
        }

        if(!restartAfterPartialStop)
        {
            if(!mInputDevice.IsAlive() || !mOutputDevice.IsAlive())
            {
                LogError("NovaLINKPlayThrough::Start: %s %s",
                         mInputDevice.IsAlive() ? "" : "!mInputDevice",
                         mOutputDevice.IsAlive() ? "" : "!mOutputDevice");

                ReleaseThreadsWaitingForOutputToStart();

                throw CAException(kAudioHardwareBadDeviceError);
            }

            // Set up IOProcs and listeners if they haven't been already.
            Activate();

            NovaLINKAssert((mInputDeviceIOProcID != nullptr) && (mOutputDeviceIOProcID != nullptr),
                      "NovaLINKPlayThrough::Start: Null IOProc ID");

            if((mInputDeviceIOProcState != IOState::Stopped) || (mOutputDeviceIOProcState != IOState::Stopped))
            {
                LogWarning("NovaLINKPlayThrough::Start: IOProc(s) not ready. Trying to start anyway. %s%d %s%d",
                           "mInputDeviceIOProcState = ", mInputDeviceIOProcState.load(),
                           "mOutputDeviceIOProcState = ", mOutputDeviceIOProcState.load());
            }

            DebugMsg("NovaLINKPlayThrough::Start: Starting playthrough");

            mOutputDeviceIOProcState = IOState::Starting;
            mInputDeviceIOProcState = IOState::Starting;
            // Claim playthrough before releasing the lock so a concurrent Start() returns early.
            mPlayingThrough = true;
            // Cancel any StopIfIdle scheduled before this Start (device-switch / XPC races).
            mLastNotifiedIOStoppedOnNovaLINKDevice = mach_absolute_time();
            // Disarm idle-stop until a non-App client is seen playing again (or deadline).
            mIdleStopArmed = false;
            {
                mach_timebase_info_data_t info{};
                mach_timebase_info(&info);
                const UInt64 graceNsec = 60ULL * NSEC_PER_SEC;
                const UInt64 graceTicks =
                        (info.denom == 0) ? graceNsec
                                          : (graceNsec * info.denom) / info.numer;
                mIdleStopArmDeadlineHostTime = mach_absolute_time() + graceTicks;
            }

            inputProcID = mInputDeviceIOProcID;
            outputProcID = mOutputDeviceIOProcID;
        }
    }

    if(restartAfterPartialStop)
    {
        Stop();
        Start();
        return;
    }

    // Never StartIOProc on the (virtual) input until Microphone TCC is granted.
    // Otherwise each StartIOProc can queue another identical system dialog.
    if(!NovaLINKMicrophoneAccessIsAuthorized())
    {
        LogWarning("NovaLINKPlayThrough::Start: Microphone not authorized — skipping StartIOProc "
                   "(avoids stacked TCC dialogs)");
        CAMutex::Locker stateLocker(mStateMutex);
        mPlayingThrough = false;
        mInputDeviceIOProcState = IOState::Stopped;
        mOutputDeviceIOProcState = IOState::Stopped;
        ReleaseThreadsWaitingForOutputToStart();
        return;
    }

    // Start the real output device first, then NovaLINK input.
    //
    // Starting NovaLINK (input) first nests another StartIO on the virtual device while a client
    // app's StartIO may still be finishing in the HAL. On modern macOS that deadlocks; Bluetooth
    // outputs make it much more likely because their StartIOProc is slow and the race window is wide.
    const char* failedDevice = "output";
    try
    {
        mOutputDevice.StartIOProc(outputProcID);

        failedDevice = "input";
        mInputDevice.StartIOProc(inputProcID);
    }
    catch(CAException e)
    {
        CAMutex::Locker stateLocker(mStateMutex);

        ReleaseThreadsWaitingForOutputToStart();

        OSStatus err = e.GetError();
        char err4CC[5] = CA4CCToCString(err);
        LogError("NovaLINKPlayThrough::Start: Failed to start %s device. Error: %d (%s)",
                 failedDevice,
                 err,
                 err4CC);

        // Try to stop the IOProcs in case StartIOProc failed because one of our IOProc was already
        // running. I don't know if it actually does fail in that case, but the documentation
        // doesn't say so it's safer to assume it could.
        CATry
        mInputDevice.StopIOProc(mInputDeviceIOProcID);
        CACatch
        CATry
        mOutputDevice.StopIOProc(mOutputDeviceIOProcID);
        CACatch

        mInputDeviceIOProcState = IOState::Stopped;
        mOutputDeviceIOProcState = IOState::Stopped;
        mPlayingThrough = false;

        throw;
    }
}

bool    NovaLINKPlayThrough::ClientsArePlaying() const
{
    return IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice);
}

OSStatus    NovaLINKPlayThrough::WaitForOutputDeviceToStart() noexcept
{
    // Check for errors.
    //
    // Technically we should take the state mutex here, but that could cause deadlocks because
    // NovaLINK_Device::StartIO (in NovaLINKDriver) blocks on this function (via XPC). Other NovaLINKPlayThrough
    // functions make requests to NovaLINKDriver while holding the state mutex, usually to get/set
    // properties, but the HAL will block those requests until NovaLINK_Device::StartIO returns.
    try
    {
        if(!mActive)
        {
            LogError("NovaLINKPlayThrough::WaitForOutputDeviceToStart: !mActive");
            return kAudioHardwareNotRunningError;
        }
        
        if(!mOutputDevice.IsAlive())
        {
            LogError("NovaLINKPlayThrough::WaitForOutputDeviceToStart: Device not alive");
            return kAudioHardwareBadDeviceError;
        }
    }
    catch(const CAException& e)
    {
        NovaLINKLogException(e);
        return e.GetError();
    }
    
    const IOState initialState = mOutputDeviceIOProcState;
    const UInt64 startedAt = mach_absolute_time();

    if(initialState == IOState::Running)
    {
        // Return early because the output device is already running.
        return kAudioHardwareNoError;
    }
    else if(initialState != IOState::Starting)
    {
        // Warn if we haven't been told to start the output device yet. Usually means we
        // haven't received a kAudioDevicePropertyDeviceIsRunning notification yet, which can
        // happen. It's most common when the user changes the output device while IO is
        // running.
        LogWarning("NovaLINKPlayThrough::WaitForOutputDeviceToStart: Device not starting");
        
        return kDeviceNotStarting;
    }

    // Wait for our output IOProc to start. mOutputDeviceIOProcSemaphore is reset to 0
    // (semaphore_signal_all) when our IOProc is running on the output device.
    //
    // This does mean that we won't have any data the first time our IOProc is called, but I
    // don't know any way to wait until just before that point. (The device's IsRunning property
    // changes immediately after we call StartIOProc.)
    //
    // We check mOutputDeviceIOProcState every 200ms as a fault tolerance mechanism. (Though,
    // I'm not completely sure it's impossible to miss the signal from the IOProc because of a
    // spurious wake up, so it might actually be necessary.)
    DebugMsg("NovaLINKPlayThrough::WaitForOutputDeviceToStart: Waiting.");
    
    kern_return_t theError;
    IOState state;
    UInt64 waitedNsec = 0;
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    
    do
    {
        NovaLINKAssert(mOutputDeviceIOProcSemaphore != SEMAPHORE_NULL,
                  "NovaLINKPlayThrough::WaitForOutputDeviceToStart: !mOutputDeviceIOProcSemaphore");
        
        theError = semaphore_timedwait(mOutputDeviceIOProcSemaphore,
                                       (mach_timespec_t){ 0, 200 * NSEC_PER_MSEC });
        
        // Update the total time we've been waiting and the output device's state.
        waitedNsec = (mach_absolute_time() - startedAt) * info.numer / info.denom;
        state = mOutputDeviceIOProcState;
    }
    while((theError != KERN_SUCCESS) &&         // Signalled from the IOProc.
          (state == IOState::Starting) &&       // IO state changed.
          (waitedNsec < kStartIOTimeoutNsec));  // Timed out.

    if(NovaLINKDebugLoggingIsEnabled())
    {
        UInt64 startedBy = mach_absolute_time();

        struct mach_timebase_info baseInfo = { 0, 0 };
        mach_timebase_info(&baseInfo);
        Float64 base = static_cast<Float64>(baseInfo.numer) / static_cast<Float64>(baseInfo.denom);

        DebugMsg("NovaLINKPlayThrough::WaitForOutputDeviceToStart: Started %f ms after notification, %f "
                 "ms after entering WaitForOutputDeviceToStart.",
                 static_cast<Float64>(startedBy - mToldOutputDeviceToStartAt) * base
                         / static_cast<Float64>(NSEC_PER_MSEC),
                 static_cast<Float64>(startedBy - startedAt) * base
                         / static_cast<Float64>(NSEC_PER_MSEC));
    }

    // Figure out which error code to return.
    switch (theError)
    {
        case KERN_SUCCESS:              // Signalled from the IOProc.
            return kAudioHardwareNoError;
            
                                        // IO state changed or we timed out after
        case KERN_OPERATION_TIMED_OUT:  //  - semaphore_timedwait timed out, or
        case KERN_ABORTED:              //  - a spurious wake-up.
            return (state == IOState::Running) ? kAudioHardwareNoError : kAudioHardwareNotRunningError;
            
        default:
            NovaLINK_Utils::LogIfMachError("NovaLINKPlayThrough::WaitForOutputDeviceToStart",
                                      "semaphore_timedwait",
                                      theError);
            return kAudioHardwareUnspecifiedError;
    }
}

// Release any threads waiting for the output device to start. This function doesn't take mStateMutex
// because it gets called on the IO thread, which is realtime priority.
void    NovaLINKPlayThrough::ReleaseThreadsWaitingForOutputToStart()
{
    if(mActive)
    {
        semaphore_t semaphore = mOutputDeviceIOProcSemaphore;
        
        if(semaphore != SEMAPHORE_NULL)
        {
            mRTLogger.LogReleasingWaitingThreads();

            kern_return_t theError = semaphore_signal_all(semaphore);
            mRTLogger.LogIfMachError_ReleaseWaitingThreadsSignal(theError);
        }
    }
}

OSStatus    NovaLINKPlayThrough::Stop()
{
    CAMutex::Locker stateLocker(mStateMutex);
    
    // TODO: Tell the waiting threads what happened so they can return an error?
    ReleaseThreadsWaitingForOutputToStart();
    
    if(mActive && mPlayingThrough)
    {
        DebugMsg("NovaLINKPlayThrough::Stop: Stopping playthrough");
        
        bool inputDeviceAlive = false;
        bool outputDeviceAlive = false;
        
        CATry
        inputDeviceAlive = CAHALAudioObject::ObjectExists(mInputDevice) && mInputDevice.IsAlive();
        CACatch
        
        CATry
        outputDeviceAlive =
            CAHALAudioObject::ObjectExists(mOutputDevice) && mOutputDevice.IsAlive();
        CACatch

        mInputDeviceIOProcState = inputDeviceAlive ? IOState::Stopping : IOState::Stopped;
        mOutputDeviceIOProcState = outputDeviceAlive ? IOState::Stopping : IOState::Stopped;
        
        // Wait for the IOProcs to stop themselves. This is so the IOProcs don't get called after the NovaLINKPlayThrough instance
        // (pointed to by the client data they get from the HAL) is deallocated.
        //
        // From Jeff Moore on the Core Audio mailing list:
        //     Note that there is no guarantee about how many times your IOProc might get called after AudioDeviceStop() returns
        //     when you make the call from outside of your IOProc. However, if you call AudioDeviceStop() from inside your IOProc,
        //     you do get the guarantee that your IOProc will not get called again after the IOProc has returned.
        UInt64 totalWaitNs = 0;
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&]() {
            Float64 expectedInputCycleNs = 0;

            if(inputDeviceAlive)
            {
                expectedInputCycleNs =
                    mInputDevice.GetIOBufferSize() * (1 / mInputDevice.GetNominalSampleRate()) *
                            NSEC_PER_SEC;
            }

            Float64 expectedOutputCycleNs = 0;

            if(outputDeviceAlive)
            {
                expectedOutputCycleNs =
                    mOutputDevice.GetIOBufferSize() * (1 / mOutputDevice.GetNominalSampleRate()) *
                            NSEC_PER_SEC;
            }

            UInt64 expectedMaxCycleNs =
                static_cast<UInt64>(std::max(expectedInputCycleNs, expectedOutputCycleNs));

            while((mInputDeviceIOProcState == IOState::Stopping || mOutputDeviceIOProcState == IOState::Stopping)
                  && (totalWaitNs < kStopIOProcTimeoutInIOCycles * expectedMaxCycleNs))
            {
                // TODO: If playthrough is started again while we're waiting in this loop we could drop frames. Wait on a
                //       semaphore instead of sleeping? That way Start() could also signal it, before waiting on the state mutex,
                //       as a way of cancelling the stop operation.
                struct timespec rmtp;
                int err = nanosleep((const struct timespec[]){{0, NSEC_PER_MSEC}}, &rmtp);
                totalWaitNs += NSEC_PER_MSEC - (err == -1 ? rmtp.tv_nsec : 0);
            }
        });
        
        // Clean up if the IOProcs didn't stop themselves
        if(mInputDeviceIOProcState == IOState::Stopping && mInputDeviceIOProcID != nullptr)
        {
            LogError("NovaLINKPlayThrough::Stop: The input IOProc didn't stop itself in time. Stopping "
                     "it from outside of the IO thread.");
            
            NovaLINKLogUnexpectedExceptions("NovaLINKPlayThrough::Stop", [&]() {
                mInputDevice.StopIOProc(mInputDeviceIOProcID);
            });

            mInputDeviceIOProcState = IOState::Stopped;
        }
        
        if(mOutputDeviceIOProcState == IOState::Stopping && mOutputDeviceIOProcID != nullptr)
        {
            LogError("NovaLINKPlayThrough::Stop: The output IOProc didn't stop itself in time. Stopping "
                     "it from outside of the IO thread.");
            
            NovaLINKLogUnexpectedExceptions("NovaLINKPlayThrough::Stop", [&]() {
                mOutputDevice.StopIOProc(mOutputDeviceIOProcID);
            });

            mOutputDeviceIOProcState = IOState::Stopped;
        }
        
        mPlayingThrough = false;
    }
    
    mFirstInputSampleTime = -1;
    mLastInputSampleTime = -1;
    mLastOutputSampleTime = -1;
    
    return noErr; // TODO: Why does this return anything and why always noErr?
}

void    NovaLINKPlayThrough::StopIfIdle()
{
    // To save CPU time, we stop playthrough when no clients are doing IO. This should reduce the coreaudiod and NovaLINKApp
    // processes' idle CPU use to virtually none. If this isn't working for you, a client might be running IO without
    // being audible. VLC does that when you have a file paused, for example.
    
    CAMutex::Locker stateLocker(mStateMutex);
    
    NovaLINKAssert(mInputDevice.IsNovaLINKDeviceInstance(),
              "NovaLINKDevice not set as input device. StopIfIdle can't tell if other devices are idle.");

    // After Start()/device switch, non-App clients often drop IO briefly. Do not stop until we've
    // seen them playing again (or the safety deadline passes with still nobody playing).
    if(!mIdleStopArmed)
    {
        bool clientsPlaying = false;
        NovaLINKLogAndSwallowExceptions("NovaLINKPlayThrough::StopIfIdle", [&] {
            clientsPlaying = IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice);
        });

        if(clientsPlaying)
        {
            DebugMsg("NovaLINKPlayThrough::StopIfIdle: Arming idle-stop (non-App client playing).");
            mIdleStopArmed = true;
            // Fall through — if they're playing we won't schedule a stop below anyway.
        }
        else if(mach_absolute_time() < mIdleStopArmDeadlineHostTime)
        {
            DebugMsg("NovaLINKPlayThrough::StopIfIdle: Suppressed (waiting for non-App client after Start).");
            return;
        }
        else
        {
            // Deadline expired and still idle — allow the normal idle-stop path.
            DebugMsg("NovaLINKPlayThrough::StopIfIdle: Arm deadline expired with no non-App client.");
            mIdleStopArmed = true;
        }
    }
    
    if(!IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice))
    {
        mLastNotifiedIOStoppedOnNovaLINKDevice = mach_absolute_time();
        
        // Wait a bit before stopping playthrough.
        //
        // This keeps us from starting and stopping IO too rapidly, which wastes CPU, and gives NovaLINKDriver time to update
        // kAudioDeviceCustomPropertyDeviceAudibleState, which it can only do while IO is running. (The wait duration is
        // more or less arbitrary, except that it has to be longer than kDeviceAudibleStateMinChangedFramesForUpdate.)

        // 1 / sample rate = seconds per frame
        Float64 nsecPerFrame = (1.0 / mInputDevice.GetNominalSampleRate()) * NSEC_PER_SEC;
        UInt64 waitNsec = static_cast<UInt64>(20 * kDeviceAudibleStateMinChangedFramesForUpdate * nsecPerFrame);
        UInt64 queuedAt = mLastNotifiedIOStoppedOnNovaLINKDevice;
        
        DebugMsg("NovaLINKPlayThrough::StopIfIdle: Will dispatch stop-if-idle block in %llu ns. %s%llu",
                 waitNsec,
                 "queuedAt=", queuedAt);
        
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, waitNsec),
                       dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
                       ^{
                           // Check the NovaLINKPlayThrough instance hasn't been destructed since it queued this block
                           if(mActive)
                           {
                               // The "2" is just to avoid shadowing the other locker
                               CAMutex::Locker stateLocker2(mStateMutex);
                               
                               // Don't stop playthrough if IO has started running again or if
                               // kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp has changed since
                               // this block was queued
                               if(mPlayingThrough
                                  && mIdleStopArmed
                                  && !IsRunningSomewhereOtherThanNovaLINKApp(mInputDevice)
                                  && queuedAt == mLastNotifiedIOStoppedOnNovaLINKDevice)
                               {
                                   DebugMsg("NovaLINKPlayThrough::StopIfIdle: NovaLINKDevice is only running IO for NovaLINKApp. "
                                            "Stopping playthrough.");
                                   Stop();
                               }
                           }
                       });
    }
}

#pragma mark NovaLINKDevice Listener

// TODO: Listen for changes to the sample rate and IO buffer size of the output device and update
//       the input device to match. Especially important for Bluetooth, which can renegotiate rate
//       after StartIO / codec switches — without a listener, playthrough stays pitch-shifted.

// static
OSStatus    NovaLINKPlayThrough::NovaLINKDeviceListenerProc(AudioObjectID inObjectID,
                                                  UInt32 inNumberAddresses,
                                                  const AudioObjectPropertyAddress* __nonnull inAddresses,
                                                  void* __nullable inClientData)
{
    // refCon (reference context) is the instance that registered the listener proc
    NovaLINKPlayThrough* refCon = static_cast<NovaLINKPlayThrough*>(inClientData);
    
    // If the input device isn't NovaLINKDevice, this listener proc shouldn't be registered
    ThrowIf(inObjectID != refCon->mInputDevice.GetObjectID(),
            CAException(kAudioHardwareBadObjectError),
            "NovaLINKPlayThrough::NovaLINKDeviceListenerProc: notified about audio object other than NovaLINKDevice");
    
    for(int i = 0; i < inNumberAddresses; i++)
    {
        switch(inAddresses[i].mSelector)
        {
            case kAudioDeviceProcessorOverload:
                // These warnings are common when you use the UI if you're running a debug build or have "Debug executable"
                // checked. You shouldn't be seeing them otherwise.
                DebugMsg("NovaLINKPlayThrough::NovaLINKDeviceListenerProc: WARNING! Got kAudioDeviceProcessorOverload notification");
                LogWarning("NovaLINK: CPU overload reported\n");
                break;
                
            // Start playthrough when a client starts IO on NovaLINKDevice and stop when NovaLINKApp (i.e. playthrough itself) is
            // the only client left doing IO.
            //
            // These cases are dispatched to avoid causing deadlocks by triggering one of the following notifications in
            // the process of handling one. Deadlocks could happen if these were handled synchronously when:
            //     - the first NovaLINKDeviceListenerProc call takes the state mutex, then requests some data from the HAL and
            //       waits for it to return,
            //     - the request triggers the HAL to send notifications, which it sends on a different thread,
            //     - the HAL waits for the second NovaLINKDeviceListenerProc call to return before it returns the data
            //       requested by the first NovaLINKDeviceListenerProc call, and
            //     - the second NovaLINKDeviceListenerProc call waits for the first to unlock the state mutex.
                
            case kAudioDevicePropertyDeviceIsRunning:  // Received on the IO thread before our IOProc is called
                HandleNovaLINKDeviceIsRunning(refCon);
                break;
                
            case kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp:
                HandleNovaLINKDeviceIsRunningSomewhereOtherThanNovaLINKApp(refCon);
                break;
                
            default:
                // We might get properties we didn't ask for, so we just ignore them.
                break;
        }
    }
    
    // From AudioHardware.h: "The return value is currently unused and should always be 0."
    return 0;
}

// static
void    NovaLINKPlayThrough::HandleNovaLINKDeviceIsRunning(NovaLINKPlayThrough* refCon)
{
    DebugMsg("NovaLINKPlayThrough::HandleNovaLINKDeviceIsRunning: Got notification");
    
    // This is dispatched because it can block and
    //   - we might be on a real-time thread, or
    //   - NovaLINKXPCListener::startPlayThroughSyncWithReply might get called on the same thread just
    //     before this and time out waiting for this to run.
    //
    // TODO: We should find a way to do this without dispatching because dispatching isn't actually
    //       real-time safe.
    dispatch_async(NovaLINKGetDispatchQueue_PriorityUserInteractive(), ^{
        if(!refCon->mActive)
        {
            return;
        }

        // Set to true initially because if we fail to get this property from NovaLINKDevice we want to
        // try to start playthrough anyway.
        bool isRunningSomewhereOtherThanNovaLINKApp = true;

        {
            CAMutex::Locker stateLocker(refCon->mStateMutex);

            NovaLINKLogAndSwallowExceptions("HandleNovaLINKDeviceIsRunning", [&]() {
                // IsRunning doesn't always return true when IO is starting. Using
                // RunningSomewhereOtherThanNovaLINKApp instead seems to be working so far.
                isRunningSomewhereOtherThanNovaLINKApp =
                    IsRunningSomewhereOtherThanNovaLINKApp(refCon->mInputDevice);
            });

            DebugMsg("NovaLINKPlayThrough::HandleNovaLINKDeviceIsRunning: "
                     "NovaLINKDevice is %srunning somewhere other than NovaLINKApp",
                     isRunningSomewhereOtherThanNovaLINKApp ? "" : " not");

            if(isRunningSomewhereOtherThanNovaLINKApp)
            {
                refCon->mToldOutputDeviceToStartAt = mach_absolute_time();
            }
        }

        // Start outside the state lock — StartIOProc must not run while mStateMutex is held.
        if(isRunningSomewhereOtherThanNovaLINKApp)
        {
            // TODO: Handle expected exceptions (mostly CAExceptions from PublicUtility classes) in Start.
            //       For any that can't be handled sensibly in Start, catch them here and retry a few
            //       times (with a very short delay) before handling them by showing an unobtrusive error
            //       message or something. Then try a different device or just set the system device back
            //       to the real device.
            NovaLINKLogAndSwallowExceptions("HandleNovaLINKDeviceIsRunning", [&refCon]() {
                refCon->Start();
            });
            // Ensure idle-stop is armed now that we know a non-App client is running.
            NovaLINKLogAndSwallowExceptions("HandleNovaLINKDeviceIsRunning", [&refCon]() {
                CAMutex::Locker stateLocker(refCon->mStateMutex);
                refCon->mIdleStopArmed = true;
            });
        }
    });
}

// static
void    NovaLINKPlayThrough::HandleNovaLINKDeviceIsRunningSomewhereOtherThanNovaLINKApp(NovaLINKPlayThrough* refCon)
{
    DebugMsg("NovaLINKPlayThrough::HandleNovaLINKDeviceIsRunningSomewhereOtherThanNovaLINKApp: Got notification");
    
    // These notifications don't need to be handled quickly, so we can always dispatch.
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // TODO: Handle expected exceptions (mostly CAExceptions from PublicUtility classes) in StopIfIdle.
        NovaLINKLogUnexpectedExceptions("HandleNovaLINKDeviceIsRunningSomewhereOtherThanNovaLINKApp", [&refCon]() {
            if(refCon->mActive)
            {
                refCon->StopIfIdle();
            }
        });
    });
}

// static
bool    NovaLINKPlayThrough::IsRunningSomewhereOtherThanNovaLINKApp(const NovaLINKAudioDevice& inNovaLINKDevice)
{
    return CFBooleanGetValue(
        static_cast<CFBooleanRef>(
            inNovaLINKDevice.GetPropertyData_CFType(kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress)));
}

#pragma mark IOProcs

// Note that the IOProcs will very likely not run on the same thread and that they intentionally
// only lock mutexes around their use of mBuffer.

// static
OSStatus    NovaLINKPlayThrough::InputDeviceIOProc(AudioObjectID           inDevice,
                                              const AudioTimeStamp*   inNow,
                                              const AudioBufferList*  inInputData,
                                              const AudioTimeStamp*   inInputTime,
                                              AudioBufferList*        outOutputData,
                                              const AudioTimeStamp*   inOutputTime,
                                              void* __nullable        inClientData)
{
    #pragma unused (inDevice, inNow, outOutputData, inOutputTime)
    
    // refCon (reference context) is the instance that created the IOProc
    NovaLINKPlayThrough* const refCon = static_cast<NovaLINKPlayThrough*>(inClientData);
    
    IOState state;
    UpdateIOProcState("InputDeviceIOProc",
                      refCon->mRTLogger,
                      refCon->mInputDeviceIOProcState,
                      refCon->mInputDeviceIOProcID,
                      refCon->mInputDevice,
                      state);
    
    if(state == IOState::Stopped || state == IOState::Stopping)
    {
        // Return early, since we just asked to stop. (Or something really weird is going on.)
        return noErr;
    }
    
    NovaLINKAssert(state == IOState::Running, "NovaLINKPlayThrough::InputDeviceIOProc: Unexpected state");
    
    if(refCon->mFirstInputSampleTime == -1)
    {
        refCon->mFirstInputSampleTime = inInputTime->mSampleTime;
    }
    
    UInt32 framesToStore = inInputData->mBuffers[0].mDataByteSize / (SizeOf32(Float32) * 2);

    // See the comments in OutputDeviceIOProc where it locks mBufferOutputMutex.
    CAMutex::Tryer tryer(refCon->mBufferInputMutex);

    // Disable a warning about accessing mBuffer without holding both mBufferInputMutex and
    // mBufferOutputMutex. Explained further in OutputDeviceIOProc.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety"
    if(tryer.HasLock() && refCon->mBuffer)
    {
        CARingBufferError err =
                refCon->mBuffer->Store(inInputData,
                                       framesToStore,
                                       static_cast<CARingBuffer::SampleTime>(
                                               inInputTime->mSampleTime));
#pragma clang diagnostic pop
        refCon->mRTLogger.LogIfRingBufferError_Store(err);

        refCon->mLastInputSampleTime = inInputTime->mSampleTime;
    }
    else
    {
        refCon->mRTLogger.LogRingBufferUnavailable("InputDeviceIOProc", tryer.HasLock());
    }

    return noErr;
}

// static
OSStatus    NovaLINKPlayThrough::OutputDeviceIOProc(AudioObjectID           inDevice,
                                               const AudioTimeStamp*   inNow,
                                               const AudioBufferList*  inInputData,
                                               const AudioTimeStamp*   inInputTime,
                                               AudioBufferList*        outOutputData,
                                               const AudioTimeStamp*   inOutputTime,
                                               void* __nullable        inClientData)
{
    #pragma unused (inDevice, inNow, inInputData, inInputTime)
    
    // refCon (reference context) is the instance that created the IOProc
    NovaLINKPlayThrough* const refCon = static_cast<NovaLINKPlayThrough*>(inClientData);
    
    IOState state;
    const bool didChangeState = UpdateIOProcState("OutputDeviceIOProc",
                                                  refCon->mRTLogger,
                                                  refCon->mOutputDeviceIOProcState,
                                                  refCon->mOutputDeviceIOProcID,
                                                  refCon->mOutputDevice,
                                                  state);
    
    if(state == IOState::Stopped || state == IOState::Stopping)
    {
        // Return early, since we just asked to stop. (Or something really weird is going on.)
        FillWithSilence(outOutputData);
        return noErr;
    }
    
    NovaLINKAssert(state == IOState::Running, "NovaLINKPlayThrough::OutputDeviceIOProc: Unexpected state");
    
    if(didChangeState)
    {
        // We just changed state from Starting to Running, which means this is the first time this IOProc
        // has been called since the output device finished starting up, so now we can wake any threads
        // waiting in WaitForOutputDeviceToStart.
        NovaLINKAssert(refCon->mLastOutputSampleTime == -1,
                  "NovaLINKPlayThrough::OutputDeviceIOProc: mLastOutputSampleTime not reset");
        
        refCon->ReleaseThreadsWaitingForOutputToStart();
    }
    
    if(refCon->mLastInputSampleTime == -1)
    {
        // Return early, since we don't have any data to output yet.
        FillWithSilence(outOutputData);
        return noErr;
    }
    
    // If this is the first time this IOProc has been called since starting playthrough...
    if(refCon->mLastOutputSampleTime == -1)
    {
        // Calculate the number of frames between the read and write heads
        refCon->mInToOutSampleOffset = inOutputTime->mSampleTime - refCon->mLastInputSampleTime;
        
        // Log if we dropped frames
        refCon->mRTLogger.LogIfDroppedFrames(refCon->mFirstInputSampleTime,
                                             refCon->mLastInputSampleTime);
    }
    
    CARingBuffer::SampleTime readHeadSampleTime =
        static_cast<CARingBuffer::SampleTime>(inOutputTime->mSampleTime - refCon->mInToOutSampleOffset);
    CARingBuffer::SampleTime lastInputSampleTime =
        static_cast<CARingBuffer::SampleTime>(refCon->mLastInputSampleTime);
    
    UInt32 framesToOutput = outOutputData->mBuffers[0].mDataByteSize / (SizeOf32(Float32) * 2);

    // When the input and output devices are set, during start up or because the user changed the
    // output device, this class (re)allocates the ring buffer (mBuffer). We try to take this
    // lock before accessing the buffer to make sure it's allocated.
    //
    // If we don't get the lock, another thread must be allocating or deallocating it, so we just
    // give up. We can't avoid audio glitches while changing devices anyway. This class tries to
    // make sure the IOProcs aren't running when it allocates the buffer, but it can't guarantee
    // that.
    //
    // Note that this is only realtime safe because we only try to lock the mutex. If another
    // thread has the mutex, it will be a non-realtime thread, so we can't wait for it.
    CAMutex::Tryer tryer(refCon->mBufferOutputMutex);

    // Disable a warning about accessing mBuffer without holding both mBufferInputMutex and
    // mBufferOutputMutex. The input IOProc always writes ahead of where the output IOProc will read
    // in a given IO cycle, so it's safe for them to read and write at the same time.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wthread-safety"
    if(tryer.HasLock() && refCon->mBuffer)
    {
        // Very occasionally (at least for me) our read head gets ahead of input, i.e. we haven't
        // received any new input since this IOProc was last called, and we have to recalculate its
        // position. I figure this might be caused by clock drift but I'm really not sure. It also
        // happens if the input or output sample times are restarted from zero.
        //
        // We also recalculate the offset if the read head is outside of the ring buffer. This
        // happens for example when you plug in or unplug headphones, which causes the output sample
        // times to be restarted from zero.
        //
        // The vast majority of the time, just using lastInputSampleTime as the read head time
        // instead of the one we calculate would work fine (and would also account for the above).
        SInt64 bufferStartTime, bufferEndTime;
        CARingBufferError err = refCon->mBuffer->GetTimeBounds(bufferStartTime, bufferEndTime);
        bool outOfBounds = false;

        if(err == kCARingBufferError_OK)
        {
            outOfBounds = (readHeadSampleTime < bufferStartTime)
                    || (readHeadSampleTime - framesToOutput > bufferEndTime);
        }

        if(lastInputSampleTime < readHeadSampleTime || outOfBounds)
        {
            refCon->mRTLogger.LogNoSamplesReady(lastInputSampleTime,
                                                readHeadSampleTime,
                                                refCon->mInToOutSampleOffset);

            // Recalculate the in-to-out offset and read head.
            refCon->mInToOutSampleOffset =
                    inOutputTime->mSampleTime - static_cast<Float64>(lastInputSampleTime);
            readHeadSampleTime = static_cast<CARingBuffer::SampleTime>(
                    inOutputTime->mSampleTime - refCon->mInToOutSampleOffset);
        }

        // Copy the frames from the ring buffer.
        err = refCon->mBuffer->Fetch(outOutputData, framesToOutput, readHeadSampleTime);
        refCon->mRTLogger.LogIfRingBufferError_Fetch(err);

        if(err != kCARingBufferError_OK)
        {
            FillWithSilence(outOutputData);
        }
    }
    else
    {
        refCon->mRTLogger.LogRingBufferUnavailable("OutputDeviceIOProc", tryer.HasLock());
        FillWithSilence(outOutputData);
    }
#pragma clang diagnostic pop

    refCon->mLastOutputSampleTime = inOutputTime->mSampleTime;
    
    return noErr;
}

// static
inline void NovaLINKPlayThrough::FillWithSilence(AudioBufferList* ioBuffer)
{
    for(UInt32 i = 0; i < ioBuffer->mNumberBuffers; i++)
    {
        memset(ioBuffer->mBuffers[i].mData, 0, ioBuffer->mBuffers[i].mDataByteSize);
    }
}

// static
bool    NovaLINKPlayThrough::UpdateIOProcState(const char* inCallerName,
                                          NovaLINKPlayThroughRTLogger& inRTLogger,
                                          std::atomic<IOState>& inState,
                                          AudioDeviceIOProcID __nullable inIOProcID,
                                          NovaLINKAudioDevice& inDevice,
                                          IOState& outNewState)
{
    NovaLINKAssert(inIOProcID != nullptr, "NovaLINKPlayThrough::UpdateIOProcState: !inIOProcID");

    // Change this IOProc's state to Running if this is the first time it's been called since we
    // started playthrough.
    //
    // compare_exchange_strong will return true iff it changed inState from Starting to Running.
    // Otherwise it will set prevState to the current value of inState.
    //
    // TODO: We probably don't actually need memory_order_seq_cst (the default). Would it be worth
    //       changing? Might be worth checking for the other atomics/barriers in this class, too.
    IOState prevState = IOState::Starting;
    bool didChangeState = inState.compare_exchange_strong(prevState, IOState::Running);

    if(didChangeState)
    {
        NovaLINKAssert(prevState == IOState::Starting, "NovaLINKPlayThrough::UpdateIOProcState: ?!");
        outNewState = IOState::Running;
    }
    else
    {
        // Return the current value of inState to the caller.
        outNewState = prevState;
        
        if(outNewState != IOState::Running)
        {
            // The IOProc isn't Starting or Running, so it must be Stopping. That is, it's been
            // told to stop itself.
            NovaLINKAssert(outNewState == IOState::Stopping,
                      "NovaLINKPlayThrough::UpdateIOProcState: Unexpected state: %d",
                      outNewState);
            
            bool stoppedSuccessfully = false;

            try
            {
                inDevice.StopIOProc(inIOProcID);

                // StopIOProc didn't throw, so the IOProc won't be called again until the next
                // time playthrough is started.
                stoppedSuccessfully = true;
            }
            catch(CAException e)
            {
                inRTLogger.LogExceptionStoppingIOProc(inCallerName, e.GetError());
            }
            catch(...)
            {
                inRTLogger.LogExceptionStoppingIOProc(inCallerName);
            }

            if(stoppedSuccessfully)
            {
                // Change inState to Stopped.
                //
                // If inState has been changed since we last read it, we don't know if we called
                // StopIOProc before or after the thread that changed it called StartIOProc (if it
                // did). However, inState is only changed here (in the IOProc), in Start and in
                // Stop.
                //
                // Stop won't return until the IOProc has changed inState to Stopped, unless it
                // times out, so Stop should still be waiting. And since Start and Stop are
                // mutually exclusive, this should be safe.
                //
                // But if Stop has timed out and inState has changed, we leave it in its new
                // state (unless there's some ABA problem thing happening), which I suspect is
                // the safest option.
                didChangeState = inState.compare_exchange_strong(outNewState, IOState::Stopped);
                
                if(didChangeState)
                {
                    outNewState = IOState::Stopped;
                }
                else
                {
                    inRTLogger.LogUnexpectedIOStateAfterStopping(inCallerName,
                                                                 static_cast<int>(outNewState));
                }
            }
        }
    }

    return didChangeState;
}

