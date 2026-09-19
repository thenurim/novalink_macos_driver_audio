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
//  NovaLINKDeviceControlsList.cpp
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//

// Self Include
#include "NovaLINKDeviceControlsList.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Utils.h"

// PublicUtility Includes
#include "CAPropertyAddress.h"
#include "CACFArray.h"


#pragma clang assume_nonnull begin

static const SInt64 kToggleDeviceInitialDelay = 50 * NSEC_PER_MSEC;
static const SInt64 kToggleDeviceBackDelay    = 500 * NSEC_PER_MSEC;
static const SInt64 kDisableNullDeviceDelay   = 500 * NSEC_PER_MSEC;
static const SInt64 kDisableNullDeviceTimeout = 5000 * NSEC_PER_MSEC;

#pragma mark Construction/Destruction

NovaLINKDeviceControlsList::NovaLINKDeviceControlsList(AudioObjectID inNovaLINKDevice,
                                             CAHALAudioSystemObject inAudioSystem)
:
    mNovaLINKDevice(inNovaLINKDevice),
    mAudioSystem(inAudioSystem)
{
    NovaLINKAssert((mNovaLINKDevice.IsNovaLINKDevice() || mNovaLINKDevice.GetObjectID() == kAudioObjectUnknown),
              "NovaLINKDeviceControlsList::NovaLINKDeviceControlsList: Given device is not NovaLINKDevice");

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    mCanToggleDeviceOnSystem = (&dispatch_block_wait &&
                                &dispatch_block_cancel &&
                                &dispatch_block_testcancel &&
                                &dispatch_queue_attr_make_with_qos_class);
#pragma clang diagnostic pop
}

NovaLINKDeviceControlsList::~NovaLINKDeviceControlsList()
{
    CAMutex::Locker locker(mMutex);

    if(!mDeviceTogglingInitialised)
    {
        return;
    }

    if(mListenerQueue && mListenerBlock)
    {
        NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlsList::~NovaLINKDeviceControlsList", ([&] {
            mAudioSystem.RemovePropertyListenerBlock(
                    CAPropertyAddress(kAudioHardwarePropertyDevices),
                    mListenerQueue,
                    mListenerBlock);
        }));
    }

    // If we're in the middle of toggling the default device, block until we've finished.
    if(mDisableNullDeviceBlock && mDeviceToggleState != ToggleState::NotToggling)
    {
        DebugMsg("NovaLINKDeviceControlsList::~NovaLINKDeviceControlsList: Waiting for device toggle");

        // Copy the reference so we can unlock the mutex and allow any remaining blocks to run.
        dispatch_block_t disableNullDeviceBlock = mDisableNullDeviceBlock;

        CAMutex::Unlocker unlocker(mMutex);

        // Note that if mDisableNullDeviceBlock is currently running this will return after it
        // finishes and if it's already run this will return immediately. So we don't have to
        // worry about ending up waiting for mDisableNullDeviceBlock when it isn't queued.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        long timedOut = dispatch_block_wait(disableNullDeviceBlock, kDisableNullDeviceTimeout);
#pragma clang diagnostic pop

        if(timedOut)
        {
            LogWarning("NovaLINKDeviceControlsList::~NovaLINKDeviceControlsList: Device toggle timed out");
        }
    }

    mDeviceToggleState = ToggleState::NotToggling;

    DestroyBlock(mDeviceToggleBlock);
    DestroyBlock(mDeviceToggleBackBlock);
    DestroyBlock(mDisableNullDeviceBlock);

    if(mListenerBlock)
    {
        Block_release(mListenerBlock);
    }

    if(mListenerQueue)
    {
        dispatch_release(NovaLINK_Utils::NN(mListenerQueue));
    }
}

#pragma mark Accessors

void    NovaLINKDeviceControlsList::SetNovaLINKDevice(AudioObjectID inNovaLINKDeviceID)
{
    CAMutex::Locker locker(mMutex);

    mNovaLINKDevice = inNovaLINKDeviceID;

    NovaLINKAssert(mNovaLINKDevice.IsNovaLINKDevice(),
              "NovaLINKDeviceControlsList::SetNovaLINKDevice: Given device is not NovaLINKDevice");
}

#pragma mark Update Controls List

bool    NovaLINKDeviceControlsList::MatchControlsListOf(AudioObjectID inDeviceID)
{
    CAMutex::Locker locker(mMutex);

    if(!mNovaLINKDevice.IsNovaLINKDevice())
    {
        LogWarning("NovaLINKDeviceControlsList::MatchControlsListOf: NovaLINKDevice ID not set");
        return false;
    }

    // If the output device doesn't have a control that NovaLINKDevice does, disable it on NovaLINKDevice so
    // the system's audio UI isn't confusing.

    // No need to change input controls.
    AudioObjectPropertyScope inScope = kAudioObjectPropertyScopeOutput;

    // Check which of NovaLINKDevice's controls are currently enabled. We need to know whether we're
    // actually enabling/disabling any controls so we know whether we need to call
    // PropagateControlListChange afterward.
    CFTypeRef __nullable enabledControlsRef =
        mNovaLINKDevice.GetPropertyData_CFType(kNovaLINKEnabledOutputControlsAddress);

    ThrowIf(!enabledControlsRef || (CFGetTypeID(enabledControlsRef) != CFArrayGetTypeID()),
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDeviceControlsList::MatchControlsListOf: Expected a CFArray for "
            "kAudioDeviceCustomPropertyEnabledOutputControls");

    CACFArray enabledControls(static_cast<CFArrayRef>(enabledControlsRef), true);

    NovaLINKAssert(enabledControls.GetNumberItems() == 2,
              "NovaLINKDeviceControlsList::MatchControlsListOf: Expected 2 array elements for "
              "kAudioDeviceCustomPropertyEnabledOutputControls");

    bool volumeEnabled;
    bool didGetBool = enabledControls.GetBool(kNovaLINKEnabledOutputControlsIndex_Volume, volumeEnabled);
    ThrowIf(!didGetBool,
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDeviceControlsList::MatchControlsListOf: Expected volume element of "
            "kAudioDeviceCustomPropertyEnabledOutputControls to be a CFBoolean");

    bool muteEnabled;
    didGetBool = enabledControls.GetBool(kNovaLINKEnabledOutputControlsIndex_Mute, muteEnabled);
    ThrowIf(!didGetBool,
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDeviceControlsList::MatchControlsListOf: Expected mute element of "
            "kAudioDeviceCustomPropertyEnabledOutputControls to be a CFBoolean");

    DebugMsg("NovaLINKDeviceControlsList::MatchControlsListOf: NovaLINKDevice has volume %s, mute %s",
             (volumeEnabled ? "enabled" : "disabled"),
             (muteEnabled ? "enabled" : "disabled"));

    // Check which controls the other device has.
    NovaLINKAudioDevice device(inDeviceID);
    bool hasMute = device.HasSettableMasterMute(inScope);

    bool hasVolume =
        device.HasSettableMasterVolume(inScope) || device.HasSettableVirtualMasterVolume(inScope);

    if(!hasVolume)
    {
        // Check for per-channel volume controls.
        UInt32 numChannels =
            device.GetTotalNumberChannels(inScope == kAudioObjectPropertyScopeInput);

        for(UInt32 channel = 1; channel <= numChannels; channel++)
        {
            NovaLINKLogAndSwallowExceptionsMsg("NovaLINKDeviceControlsList::MatchControlsListOf",
                                          "Checking for channel volume controls",
                                          ([&] {
                hasVolume =
                    (device.HasVolumeControl(inScope, channel)
                            && device.VolumeControlIsSettable(inScope, channel));
            }));

            if(hasVolume)
            {
                break;
            }
        }
    }

    // Tell NovaLINKDevice to enable/disable its controls to match the output device.
    bool deviceUpdated = false;

    CACFArray newEnabledControls;
    newEnabledControls.SetCFMutableArrayFromCopy(enabledControls.GetCFArray());

    // Update volume.
    if(volumeEnabled != hasVolume)
    {
        DebugMsg("NovaLINKDeviceControlsList::MatchControlsListOf: %s NovaLINKDevice volume control.",
                 hasVolume ? "Enabling" : "Disabling");

        newEnabledControls.SetBool(kNovaLINKEnabledOutputControlsIndex_Volume, hasVolume);
        deviceUpdated = true;
    }

    // Update mute.
    if(muteEnabled != hasMute)
    {
        DebugMsg("NovaLINKDeviceControlsList::MatchControlsListOf: %s NovaLINKDevice mute control.",
                 hasMute ? "Enabling" : "Disabling");

        newEnabledControls.SetBool(kNovaLINKEnabledOutputControlsIndex_Mute, hasMute);
        deviceUpdated = true;
    }

    if(deviceUpdated)
    {
        mNovaLINKDevice.SetPropertyData_CFType(kNovaLINKEnabledOutputControlsAddress,
                                          newEnabledControls.GetCFMutableArray());
    }

    return deviceUpdated;
}

void    NovaLINKDeviceControlsList::PropagateControlListChange()
{
    // Enabling the Null Device and cycling the OS default output is a known coreaudiod
    // deadlock: System Settings, screenshot shutter, IME, and unrelated apps all freeze.
    // MatchControlsListOf already applied the enabled-controls property on NovaLINKDevice.
    DebugMsg("NovaLINKDeviceControlsList::PropagateControlListChange: skipped null-device default toggle");
}

#pragma mark Implementation

void    NovaLINKDeviceControlsList::InitDeviceToggling()
{
    CAMutex::Locker locker(mMutex);

    if(mDeviceTogglingInitialised || !mCanToggleDeviceOnSystem)
    {
        return;
    }

    NovaLINKAssert(mNovaLINKDevice.IsNovaLINKDevice(),
              "NovaLINKDeviceControlsList::InitDeviceToggling: mNovaLINKDevice device is not set to "
              "NovaLINKDevice's ID");

    // Register a listener to find out when the Null Device becomes available/unavailable. See
    // ToggleDefaultDevice.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    dispatch_queue_attr_t attr =
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_DEFAULT, 0);
#pragma clang diagnostic pop
    mListenerQueue = dispatch_queue_create("com.bearisdriving.NovaLINK.NovaLINKDeviceControlsList", attr);

    auto listenerBlock = ^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress* inAddresses) {
        // Ignore the notification if we're not toggling the default device, which would just mean
        // the default device has been changed for an unrelated reason.
        if(mDeviceToggleState == ToggleState::NotToggling)
        {
            return;
        }

        for(int i = 0; i < inNumberAddresses; i++)
        {
            switch(inAddresses[i].mSelector)
            {
                case kAudioHardwarePropertyDevices:
                    {
                        CAMutex::Locker innerLocker(mMutex);

                        DebugMsg("NovaLINKDeviceControlsList::InitDeviceToggling: Got "
                                 "kAudioHardwarePropertyDevices");

                        // Cancel the previous block in case it hasn't run yet.
                        DestroyBlock(mDeviceToggleBlock);

                        mDeviceToggleBlock = CreateDeviceToggleBlock();

                        // Changing the default device too quickly after enabling the Null Device
                        // seems to cause problems with some programs. Not sure why.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
                        if(mDeviceToggleBlock)
                        {
                            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                                         kToggleDeviceInitialDelay),
                                           dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                                           NovaLINK_Utils::NN(mDeviceToggleBlock));
                        }
#pragma clang diagnostic pop
                    }
                    break;

                default:
                    break;
            }
        }
    };

    mListenerBlock = Block_copy(listenerBlock);

    NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlsList::InitDeviceToggling", [&] {
        mAudioSystem.AddPropertyListenerBlock(CAPropertyAddress(kAudioHardwarePropertyDevices),
                                              mListenerQueue,
                                              mListenerBlock);
    });

    mDeviceTogglingInitialised = true;
}

void    NovaLINKDeviceControlsList::ToggleDefaultDevice()
{
    // Set the Null Device as the OS X default device.
    AudioObjectID nullDeviceID = mAudioSystem.GetAudioDeviceForUID(CFSTR(kNovaLINKNullDeviceUID));

    if(nullDeviceID == kAudioObjectUnknown)
    {
        // It's unlikely, but we might have been notified about an unrelated device so just log a
        // warning.
        LogWarning("NovaLINKDeviceControlsList::ToggleDefaultDevice: Null Device not found");
        return;
    }

    DebugMsg("NovaLINKDeviceControlsList::ToggleDefaultDevice: Setting Null Device as default. "
             "nullDeviceID = %u", nullDeviceID);
    mAudioSystem.SetDefaultAudioDevice(false, false, nullDeviceID);

    mDeviceToggleState = ToggleState::SettingNovaLINKDeviceAsDefault;

    // A small number of apps (e.g. Firefox) seem to have trouble with the default device being
    // changed back immediately, so for now we insert a short delay here and before disabling the
    // Null Device.

    // Cancel the previous block in case it hasn't run yet.
    DestroyBlock(mDeviceToggleBackBlock);

    mDeviceToggleBackBlock = CreateDeviceToggleBackBlock();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    if(mDeviceToggleBackBlock)
    {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kToggleDeviceBackDelay),
                       dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                       NovaLINK_Utils::NN(mDeviceToggleBackBlock));
    }
#pragma clang diagnostic pop
}

void    NovaLINKDeviceControlsList::SetNullDeviceEnabled(bool inEnabled)
{
    DebugMsg("NovaLINKDeviceControlsList::SetNullDeviceEnabled: %s the null device",
             inEnabled ? "Enabling" : "Disabling");

    // Get the audio object for NovaLINKDriver, which is the object the Null Device belongs to.
    AudioObjectID novaLINKDriverID = mAudioSystem.GetAudioPlugInForBundleID(CFSTR(kNovaLINKDriverBundleID));

    if(novaLINKDriverID == kAudioObjectUnknown)
    {
        LogError("NovaLINKDeviceControlsList::SetNullDeviceEnabled: NovaLINKDriver plug-in audio object not "
                 "found");
        throw CAException(kAudioHardwareUnspecifiedError);
    }

    CAHALAudioObject novaLINKDriver(novaLINKDriverID);
    novaLINKDriver.SetPropertyData_CFType(CAPropertyAddress(kAudioPlugInCustomPropertyNullDeviceActive),
                                     (inEnabled ? kCFBooleanTrue : kCFBooleanFalse));
}

dispatch_block_t __nullable NovaLINKDeviceControlsList::CreateDeviceToggleBlock()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    dispatch_block_t __nullable toggleBlock = dispatch_block_create((dispatch_block_flags_t)0, ^{
#pragma clang diagnostic pop
        CAMutex::Locker locker(mMutex);

        if(mDeviceToggleState == ToggleState::SettingNullDeviceAsDefault)
        {
            NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlsList::CreateDeviceToggleBlock",
                                       ([&] {
                ToggleDefaultDevice();
            }));
        }
    });

    if(!toggleBlock)
    {
        // Pretty sure this should never happen, but the docs aren't completely clear.
        LogError("NovaLINKDeviceControlsList::CreateDeviceToggleBlock: !toggleBlock");
    }

    return toggleBlock;
}

dispatch_block_t __nullable NovaLINKDeviceControlsList::CreateDeviceToggleBackBlock()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    dispatch_block_t __nullable toggleBackBlock =
            dispatch_block_create((dispatch_block_flags_t)0, ^{
#pragma clang diagnostic pop
        CAMutex::Locker locker(mMutex);

        if(mDeviceToggleState != ToggleState::SettingNovaLINKDeviceAsDefault)
        {
            return;
        }

        // Set NovaLINKDevice back as the default device.
        DebugMsg("NovaLINKDeviceControlsList::ToggleDefaultDevice: Setting NovaLINKDevice as default");
        NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlsList::CreateDeviceToggleBackBlock", ([&] {
            mAudioSystem.SetDefaultAudioDevice(false, false, mNovaLINKDevice.GetObjectID());
        }));

        mDeviceToggleState = ToggleState::DisablingNullDevice;

        // Cancel the previous block in case it hasn't run yet.
        DestroyBlock(mDisableNullDeviceBlock);

        mDisableNullDeviceBlock = CreateDisableNullDeviceBlock();

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        if(mDisableNullDeviceBlock)
        {
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kDisableNullDeviceDelay),
                           dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0),
                           NovaLINK_Utils::NN(mDisableNullDeviceBlock));
        }
#pragma clang diagnostic pop
    });

    if(!toggleBackBlock)
    {
        // Pretty sure this should never happen, but the docs aren't completely clear.
        LogError("NovaLINKDeviceControlsList::CreateDeviceToggleBackBlock: !toggleBackBlock");
    }

    return toggleBackBlock;
}

dispatch_block_t __nullable NovaLINKDeviceControlsList::CreateDisableNullDeviceBlock()
{
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    dispatch_block_t __nullable disableNullDeviceBlock =
            dispatch_block_create((dispatch_block_flags_t)0, ^{
#pragma clang diagnostic pop
        CAMutex::Locker locker(mMutex);

        if(mDeviceToggleState != ToggleState::DisablingNullDevice)
        {
            return;
        }

        mDeviceToggleState = ToggleState::NotToggling;

        NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlsList::CreateDisableNullDeviceBlock",
                                   ([&] {
            CAMutex::Unlocker unlocker(mMutex);
            // Hide the null device from the user again.
            SetNullDeviceEnabled(false);
        }));

        NovaLINKAssert(mNovaLINKDevice.IsNovaLINKDevice(), "NovaLINKDevice's AudioObjectID changed");
    });

    if(!disableNullDeviceBlock)
    {
        // Pretty sure this should never happen, but the docs aren't completely clear.
        LogError("NovaLINKDeviceControlsList::CreateDisableNullDeviceBlock: !disableNullDeviceBlock");
    }

    return disableNullDeviceBlock;
}

void    NovaLINKDeviceControlsList::DestroyBlock(dispatch_block_t __nullable & block)
{
    if(!block)
    {
        return;
    }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
    dispatch_block_t& blockNN = (dispatch_block_t&)block;
    if(!dispatch_block_testcancel(blockNN))
    {
        // Stop the block from running if it's currently queued.
        dispatch_block_cancel(blockNN);

        // Make sure the block isn't currently running. That should almost never be the case.
        while(!dispatch_block_testcancel(blockNN))
        {
            CAMutex::Unlocker unlocker(mMutex);
            usleep(10);
        }
        
        Block_release(block);
        block = nullptr;
    }
#pragma clang diagnostic pop
}

#pragma clang assume_nonnull end

