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
//  NovaLINKDeviceControlSync.h
//  NovaLINKApp
//
//  Copyright © 2016, 2017 Kyle Neideck
//
//  Synchronises NovaLINKDevice's controls (just volume and mute currently) with the output device's
//  controls. This allows the user to control the output device normally while NovaLINKDevice is set as
//  the default device.
//
//  NovaLINKDeviceControlSync disables any NovaLINKDevice controls that the output device doesn't also have.
//  When the value of one of NovaLINKDevice's controls is changed, NovaLINKDeviceControlSync copies the new
//  value to the output device.
//
//  Thread safe.
//

#ifndef NovaLINKApp__NovaLINKDeviceControlSync
#define NovaLINKApp__NovaLINKDeviceControlSync

// Local Includes
#include "NovaLINKAudioDevice.h"
#include "NovaLINKDeviceControlsList.h"

// PublicUtility Includes
#include "CAHALAudioSystemObject.h"
#include "CAMutex.h"

// System Includes
#include <AudioToolbox/AudioServices.h>


#pragma clang assume_nonnull begin

class NovaLINKDeviceControlSync
{

#pragma mark Construction/Destruction

public:
                        NovaLINKDeviceControlSync(AudioObjectID inNovaLINKDevice,
                                             AudioObjectID inOutputDevice,
                                             CAHALAudioSystemObject inAudioSystem
                                                     = CAHALAudioSystemObject());
                        ~NovaLINKDeviceControlSync();
                        // Disallow copying
                        NovaLINKDeviceControlSync(const NovaLINKDeviceControlSync&) = delete;
                        NovaLINKDeviceControlSync& operator=(const NovaLINKDeviceControlSync&) = delete;

#ifdef __OBJC__
                        // Only intended as a convenience for Objective-C instance vars
                        NovaLINKDeviceControlSync()
                        : NovaLINKDeviceControlSync(kAudioObjectUnknown, kAudioObjectUnknown) { };
#endif

    /*!
     Begin synchronising NovaLINKDevice's controls with the output device's.

     @throws NovaLINK_DeviceNotSetException if NovaLINKDevice isn't set.
     @throws CAException if the HAL or one of the devices returns an error when this function
                         registers for device property notifications or when it copies the current
                         values of the output device's controls to NovaLINKDevice. This
                         NovaLINKDeviceControlSync will remain inactive if this function throws.
     */
    void                Activate();
    /*! Stop synchronising NovaLINKDevice's controls with the output device's. */
    void                Deactivate();

#pragma mark Accessors

    /*!
     Set the IDs of NovaLINKDevice and the output device to synchronise with.

     @throws NovaLINK_DeviceNotSetException if NovaLINKDevice isn't set.
     @throws CAException if the HAL or one of the new devices returns an error while restarting
                         synchronisation. This NovaLINKDeviceControlSync will be deactivated if this
                         function throws, but its devices will still be set.
     */
    void                SetDevices(AudioObjectID inNovaLINKDevice, AudioObjectID inOutputDevice);

#pragma mark Listener Procs
    
private:
    /*! Receives HAL notifications about the NovaLINKDevice properties this class listens to. */
    static OSStatus     NovaLINKDeviceListenerProc(AudioObjectID inObjectID,
                                              UInt32 inNumberAddresses,
                                              const AudioObjectPropertyAddress* inAddresses,
                                              void* __nullable inClientData);
    
private:
    CAMutex             mMutex         { "Device Control Sync" };
    bool                mActive        = false;

    CAHALAudioSystemObject mAudioSystem;
    
    NovaLINKAudioDevice      mNovaLINKDevice     { (AudioObjectID)kAudioObjectUnknown };
    NovaLINKAudioDevice      mOutputDevice  { (AudioObjectID)kAudioObjectUnknown };

    NovaLINKDeviceControlsList mNovaLINKDeviceControlsList;
    
};

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKDeviceControlSync */

