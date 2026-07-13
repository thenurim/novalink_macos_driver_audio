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
//  NovaLINKDevice.cpp
//  NovaLINKApp
//
//  Copyright © 2016-2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//

// Self Include
#include "NovaLINKDevice.h"

// Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Utils.h"

// PublicUtility Includes
#include "CADebugMacros.h"
#include "CAHALAudioSystemObject.h"


#pragma clang assume_nonnull begin

#pragma mark Construction/Destruction

NovaLINKDevice::NovaLINKDevice()
:
    NovaLINKAudioDevice(CFSTR(kNovaLINKDeviceUID)),
    mUISoundsNovaLINKDevice(CFSTR(kNovaLINKDeviceUID_UISounds))
{
    if((GetObjectID() == kAudioObjectUnknown) || (mUISoundsNovaLINKDevice == kAudioObjectUnknown))
    {
        LogError("NovaLINKDevice::NovaLINKDevice: Error getting NovaLINKDevice ID");
        Throw(CAException(kAudioHardwareIllegalOperationError));
    }
};

NovaLINKDevice::~NovaLINKDevice()
{
}

#pragma mark Systemwide Default Device

void NovaLINKDevice::SetAsOSDefault()
{
    DebugMsg("NovaLINKDevice::SetAsOSDefault: Setting the system's default audio device "
             "to NovaLINKDevice");

    CAHALAudioSystemObject audioSystem;

    AudioDeviceID defaultDevice = audioSystem.GetDefaultAudioDevice(false, false);
    AudioDeviceID systemDefaultDevice = audioSystem.GetDefaultAudioDevice(false, true);

    if(systemDefaultDevice == defaultDevice)
    {
        // The default system device is the same as the default device, so change both of them.
        //
        // Use the UI sounds instance of NovaLINKDevice because the default system output device is the
        // device "to use for system related sound". The allows NovaLINKDriver to tell when the audio it
        // receives is UI-related.
        audioSystem.SetDefaultAudioDevice(false, true, mUISoundsNovaLINKDevice);
    }

    audioSystem.SetDefaultAudioDevice(false, false, GetObjectID());
}

void NovaLINKDevice::UnsetAsOSDefault(AudioDeviceID inOutputDeviceID)
{
    CAHALAudioSystemObject audioSystem;

    // Set NovaLINKApp's output device as OS X's default output device.
    bool novaLINKDeviceIsDefault =
            (audioSystem.GetDefaultAudioDevice(false, false) == GetObjectID());

    if(novaLINKDeviceIsDefault)
    {
        DebugMsg("NovaLINKDevice::UnsetAsOSDefault: Setting the system's default output "
                 "device back to device %d", inOutputDeviceID);

        audioSystem.SetDefaultAudioDevice(false, false, inOutputDeviceID);
    }

    // Set NovaLINKApp's output device as OS X's default system output device.
    bool novaLINKDeviceIsSystemDefault =
            (audioSystem.GetDefaultAudioDevice(false, true) == mUISoundsNovaLINKDevice);

    // If we changed the default system output device to NovaLINKDevice, which we only do if it's set to
    // the same device as the default output device, change it back to the previous device.
    if(novaLINKDeviceIsSystemDefault)
    {
        DebugMsg("NovaLINKDevice::UnsetAsOSDefault: Setting the system's default system "
                 "output device back to device %d", inOutputDeviceID);

        audioSystem.SetDefaultAudioDevice(false, true, inOutputDeviceID);
    }
}

#pragma mark Audible State

NovaLINKDeviceAudibleState NovaLINKDevice::GetAudibleState() const
{
    CFTypeRef propertyDataRef = GetPropertyData_CFType(kNovaLINKAudibleStateAddress);

    ThrowIfNULL(propertyDataRef,
                CAException(kAudioHardwareIllegalOperationError),
                "NovaLINKDevice::GetAudibleState: !propertyDataRef");

    ThrowIf(CFGetTypeID(propertyDataRef) != CFNumberGetTypeID(),
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDevice::GetAudibleState: Property was not a CFNumber");

    CFNumberRef audibleStateRef = static_cast<CFNumberRef>(propertyDataRef);

    NovaLINKDeviceAudibleState audibleState;
    Boolean success = CFNumberGetValue(audibleStateRef, kCFNumberSInt32Type, &audibleState);
    CFRelease(audibleStateRef);

    ThrowIf(!success,
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDevice::GetMusicPlayerProcessID: CFNumberGetValue failed");

    return audibleState;
}

#pragma mark Music Player

pid_t NovaLINKDevice::GetMusicPlayerProcessID() const
{
    CFTypeRef propertyDataRef = GetPropertyData_CFType(kNovaLINKMusicPlayerProcessIDAddress);

    ThrowIfNULL(propertyDataRef,
                CAException(kAudioHardwareIllegalOperationError),
                "NovaLINKDevice::GetMusicPlayerProcessID: !propertyDataRef");

    ThrowIf(CFGetTypeID(propertyDataRef) != CFNumberGetTypeID(),
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDevice::GetMusicPlayerProcessID: Property was not a CFNumber");

    CFNumberRef pidRef = static_cast<CFNumberRef>(propertyDataRef);

    pid_t pid;
    Boolean success = CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
    CFRelease(pidRef);

    ThrowIf(!success,
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDevice::GetMusicPlayerProcessID: CFNumberGetValue failed");

    return pid;
}

CFStringRef NovaLINKDevice::GetMusicPlayerBundleID() const
{
    CFStringRef bundleID = GetPropertyData_CFString(kNovaLINKMusicPlayerBundleIDAddress);

    ThrowIfNULL(bundleID,
                CAException(kAudioHardwareIllegalOperationError),
                "NovaLINKDevice::GetMusicPlayerBundleID: !bundleID");

    return bundleID;
}

#pragma clang assume_nonnull end

