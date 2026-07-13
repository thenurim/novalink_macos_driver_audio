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

//  NovaLINKAudioDevice.h
//  NovaLINKApp
//
//  Copyright © 2017, 2020 Kyle Neideck
//
//  A HAL audio device. Note that this class's only state is the AudioObjectID of the device.
//

#ifndef NovaLINKApp__NovaLINKAudioDevice
#define NovaLINKApp__NovaLINKAudioDevice

// PublicUtility Includes
#include "CAHALAudioDevice.h"


class NovaLINKAudioDevice
:
    public CAHALAudioDevice
{

#pragma mark Construction/Destruction

public:
                       NovaLINKAudioDevice(AudioObjectID inAudioDevice);
    /*!
     Creates a NovaLINKAudioDevice with the Audio Object ID of the device whose UID is inUID or, if no
     such device is found, kAudioObjectUnknown.

     @throws CAException If the HAL returns an error when queried for the device's ID.
     @see kAudioPlugInPropertyTranslateUIDToDevice in AudioHardwareBase.h.
     */
                       NovaLINKAudioDevice(CFStringRef inUID);
                       NovaLINKAudioDevice(const CAHALAudioDevice& inDevice);
    virtual            ~NovaLINKAudioDevice();

#if defined(__OBJC__)

    // Hack/workaround for Objective-C classes so we don't have to use pointers for instance
    // variables.
                       NovaLINKAudioDevice() : NovaLINKAudioDevice(kAudioObjectUnknown) { }

#endif /* defined(__OBJC__) */

                       operator AudioObjectID() const { return GetObjectID(); }

    /*!
     @return True if this device is NovaLINKDevice. (Specifically, the main instance of NovaLINKDevice, not
             the instance used for UI sounds.)
     @throws CAException If the HAL returns an error when queried.
     */
    bool               IsNovaLINKDevice() const { return IsNovaLINKDevice(false); };
    /*!
     @return True if this device is either the main instance of NovaLINKDevice (the device named
             "NovaLINK") or the instance used for UI sounds (the device named "Background
             Music (UI Sounds)").
     @throws CAException If the HAL returns an error when queried.
     */
    bool               IsNovaLINKDeviceInstance() const { return IsNovaLINKDevice(true); };

    /*!
     @return True if this device can be set as the output device in NovaLINKApp.
     @throws CAException If the HAL returns an error when queried.
     */
    bool               CanBeOutputDeviceInNovaLINKApp() const;

#pragma mark Available Controls

    bool               HasSettableMasterVolume(AudioObjectPropertyScope inScope) const;
    bool               HasSettableVirtualMasterVolume(AudioObjectPropertyScope inScope) const;
    bool               HasSettableMasterMute(AudioObjectPropertyScope inScope) const;

#pragma mark Control Values Accessors

    void               CopyMuteFrom(const NovaLINKAudioDevice inDevice,
                                    AudioObjectPropertyScope inScope);
    void               CopyVolumeFrom(const NovaLINKAudioDevice inDevice,
                                      AudioObjectPropertyScope inScope);

    bool               SetMasterVolumeScalar(AudioObjectPropertyScope inScope, Float32 inVolume);
    
    bool               GetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                    Float32& outVirtualMasterVolume) const;
    bool               SetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                    Float32 inVolume);

    bool               GetVirtualMasterBalance(AudioObjectPropertyScope inScope,
                                               Float32& outVirtualMasterBalance) const;

#pragma mark Implementation

private:
    bool               IsNovaLINKDevice(bool inIncludingUISoundsInstance) const;

    static OSStatus    AHSGetPropertyData(AudioObjectID inObjectID,
                                          const AudioObjectPropertyAddress* inAddress,
                                          UInt32* ioDataSize,
                                          void* outData);
    static OSStatus    AHSSetPropertyData(AudioObjectID inObjectID,
                                          const AudioObjectPropertyAddress* inAddress,
                                          UInt32 inDataSize,
                                          const void* inData);

};

#endif /* NovaLINKApp__NovaLINKAudioDevice */

