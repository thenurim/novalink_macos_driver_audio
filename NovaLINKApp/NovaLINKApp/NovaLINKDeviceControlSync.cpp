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
//  NovaLINKDeviceControlSync.cpp
//  NovaLINKApp
//
//  Copyright © 2016, 2017 Kyle Neideck
//

// Self Include
#include "NovaLINKDeviceControlSync.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Utils.h"

// PublicUtility Includes
#include "CAPropertyAddress.h"


#pragma clang assume_nonnull begin

static const AudioObjectPropertyAddress kMutePropertyAddress =
    { kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMaster };

static const AudioObjectPropertyAddress kVolumePropertyAddress =
    { kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMaster };

#pragma mark Construction/Destruction

NovaLINKDeviceControlSync::NovaLINKDeviceControlSync(AudioObjectID inNovaLINKDevice,
                                           AudioObjectID inOutputDevice,
                                           CAHALAudioSystemObject inAudioSystem)
:
    mNovaLINKDevice(inNovaLINKDevice),
    mOutputDevice(inOutputDevice),
    mAudioSystem(inAudioSystem),
    mNovaLINKDeviceControlsList(inNovaLINKDevice)
{
}

NovaLINKDeviceControlSync::~NovaLINKDeviceControlSync()
{
    NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlSync::~NovaLINKDeviceControlSync", [&] {
        CAMutex::Locker locker(mMutex);

        Deactivate();
    });
}

void    NovaLINKDeviceControlSync::Activate()
{
    CAMutex::Locker locker(mMutex);

    ThrowIf((mNovaLINKDevice.GetObjectID() == kAudioObjectUnknown || mOutputDevice.GetObjectID() == kAudioObjectUnknown),
            NovaLINK_DeviceNotSetException(),
            "NovaLINKDeviceControlSync::Activate: Both the output device and NovaLINKDevice must be set to start synchronizing their controls");

    if(!mActive)
    {
        DebugMsg("NovaLINKDeviceControlSync::Activate: Activating control sync");

        // Disable NovaLINKDevice controls that the output device doesn't have and reenable any that were
        // disabled for the previous output device.
        //
        // Continue anyway if this fails because it's better to have extra/missing controls than to
        // be unable to use the device.
        NovaLINKLogAndSwallowExceptionsMsg("NovaLINKDeviceControlSync::Activate", "Controls list", [&] {
            bool wasUpdated = mNovaLINKDeviceControlsList.MatchControlsListOf(mOutputDevice);
            if(wasUpdated)
            {
                mNovaLINKDeviceControlsList.PropagateControlListChange();
            }
        });

        // Init NovaLINKDevice controls to match output device
        mNovaLINKDevice.CopyVolumeFrom(mOutputDevice, kAudioObjectPropertyScopeOutput);
        mNovaLINKDevice.CopyMuteFrom(mOutputDevice, kAudioObjectPropertyScopeOutput);

        // Register listeners for volume and mute values
        mNovaLINKDevice.AddPropertyListener(kVolumePropertyAddress, &NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc, this);
        
        try
        {
            mNovaLINKDevice.AddPropertyListener(kMutePropertyAddress, &NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc, this);
        }
        catch(CAException)
        {
            CATry
            mNovaLINKDevice.RemovePropertyListener(kVolumePropertyAddress, &NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc, this);
            CACatch
            
            throw;
        }
        
        mActive = true;
    }
    else
    {
        DebugMsg("NovaLINKDeviceControlSync::Activate: Already active");
    }
}

void    NovaLINKDeviceControlSync::Deactivate()
{
    CAMutex::Locker locker(mMutex);

    if(mActive)
    {
        DebugMsg("NovaLINKDeviceControlSync::Deactivate: Deactivating control sync");

        // Deregister listeners
        if(mNovaLINKDevice.GetObjectID() != kAudioDeviceUnknown)
        {
            NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlSync::Deactivate", [&] {
                mNovaLINKDevice.RemovePropertyListener(kVolumePropertyAddress,
                                                  &NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc,
                                                  this);
            });

            NovaLINKLogAndSwallowExceptions("NovaLINKDeviceControlSync::Deactivate", [&] {
                mNovaLINKDevice.RemovePropertyListener(kMutePropertyAddress,
                                                  &NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc,
                                                  this);
            });
        }

        mActive = false;
    }
    else
    {
        DebugMsg("NovaLINKDeviceControlSync::Deactivate: Not active");
    }
}

#pragma mark Accessors

void    NovaLINKDeviceControlSync::SetDevices(AudioObjectID inNovaLINKDevice, AudioObjectID inOutputDevice)
{
    CAMutex::Locker locker(mMutex);

    bool wasActive = mActive;

    Deactivate();

    mNovaLINKDevice = inNovaLINKDevice;
    mNovaLINKDeviceControlsList.SetNovaLINKDevice(inNovaLINKDevice);
    mOutputDevice = inOutputDevice;
    
    if(wasActive)
    {
        Activate();
    }
}

#pragma mark Listener Procs

// static
OSStatus    NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc(AudioObjectID inObjectID, UInt32 inNumberAddresses, const AudioObjectPropertyAddress* inAddresses, void* __nullable inClientData)
{
    // refCon (reference context) is the instance that registered this listener proc.
    NovaLINKDeviceControlSync* refCon = static_cast<NovaLINKDeviceControlSync*>(inClientData);

    auto checkState = [&] {
        if(!refCon)
        {
            LogError("NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc: !refCon");
            return false;
        }

        if(!refCon->mActive ||
           (refCon->mNovaLINKDevice.GetObjectID() == kAudioObjectUnknown) ||
           (refCon->mOutputDevice.GetObjectID() == kAudioObjectUnknown))
        {
            return false;
        }

        if(inObjectID != refCon->mNovaLINKDevice.GetObjectID())
        {
            LogError("NovaLINKDeviceControlSync::NovaLINKDeviceListenerProc: notified about audio object other than NovaLINKDevice");
            return false;
        }
        
        return true;
    };

    for(int i = 0; i < inNumberAddresses; i++)
    {
        AudioObjectPropertyScope scope = inAddresses[i].mScope;
        
        switch(inAddresses[i].mSelector)
        {
            case kAudioDevicePropertyVolumeScalar:
                {
                    CAMutex::Locker locker(refCon->mMutex);

                    // Update the output device's volume.
                    if(checkState())
                    {
                        refCon->mOutputDevice.CopyVolumeFrom(refCon->mNovaLINKDevice, scope);
                    }
                }
                break;
                
            case kAudioDevicePropertyMute:
                {
                    CAMutex::Locker locker(refCon->mMutex);

                    // Update the output device's mute control. Note that this also runs when you
                    // change the volume (on NovaLINKDevice).
                    if(checkState())
                    {
                        refCon->mOutputDevice.CopyMuteFrom(refCon->mNovaLINKDevice, scope);
                    }
                }
                break;
        }
    }

    // "The return value [of an AudioObjectPropertyListenerProc] is currently unused and should always be 0."
    return 0;
}

#pragma clang assume_nonnull end

