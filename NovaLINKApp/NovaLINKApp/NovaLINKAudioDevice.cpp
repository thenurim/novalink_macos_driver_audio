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
//  NovaLINKAudioDevice.cpp
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//

// Self Include
#include "NovaLINKAudioDevice.h"

// Local Includes
#include "NovaLINK_Types.h"

// System Includes
#include <AudioToolbox/AudioServices.h>


#pragma mark Construction/Destruction

NovaLINKAudioDevice::NovaLINKAudioDevice(AudioObjectID inAudioDevice)
:
    CAHALAudioDevice(inAudioDevice)
{
}

NovaLINKAudioDevice::NovaLINKAudioDevice(CFStringRef inUID)
:
    CAHALAudioDevice(inUID)
{
}

NovaLINKAudioDevice::NovaLINKAudioDevice(const CAHALAudioDevice& inDevice)
:
    NovaLINKAudioDevice(inDevice.GetObjectID())
{
}

NovaLINKAudioDevice::~NovaLINKAudioDevice()
{
}

bool    NovaLINKAudioDevice::CanBeOutputDeviceInNovaLINKApp() const
{
    CFStringRef uid = CopyDeviceUID();
    bool isNullDevice = CFEqual(uid, CFSTR(kNovaLINKNullDeviceUID));
    CFRelease(uid);

    bool hasOutputChannels = GetTotalNumberChannels(/* inIsInput = */ false) > 0;
    bool canBeDefault = CanBeDefaultDevice(/* inIsInput = */ false, /* inIsSystem = */ false);

    return !IsNovaLINKDeviceInstance() &&
            !isNullDevice &&
            !IsHidden() &&
            hasOutputChannels &&
            canBeDefault;
}

#pragma mark Available Controls

bool    NovaLINKAudioDevice::HasSettableMasterVolume(AudioObjectPropertyScope inScope) const
{
    return HasVolumeControl(inScope, kMasterChannel) &&
        VolumeControlIsSettable(inScope, kMasterChannel);
}

bool    NovaLINKAudioDevice::HasSettableVirtualMasterVolume(AudioObjectPropertyScope inScope) const
{
    AudioObjectPropertyAddress virtualMasterVolumeAddress = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        inScope,
        kAudioObjectPropertyElementMaster
    };

    // TODO: Replace these calls deprecated AudioToolbox functions. There are more below.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    Boolean virtualMasterVolumeIsSettable;
    OSStatus err = AudioHardwareServiceIsPropertySettable(GetObjectID(),
                                                          &virtualMasterVolumeAddress,
                                                          &virtualMasterVolumeIsSettable);
    virtualMasterVolumeIsSettable &= (err == kAudioServicesNoError);

    bool hasVirtualMasterVolume =
        AudioHardwareServiceHasProperty(GetObjectID(), &virtualMasterVolumeAddress);
#pragma clang diagnostic pop

    return hasVirtualMasterVolume && virtualMasterVolumeIsSettable;
}

bool    NovaLINKAudioDevice::HasSettableMasterMute(AudioObjectPropertyScope inScope) const
{
    return HasMuteControl(inScope, kMasterChannel) &&
        MuteControlIsSettable(inScope, kMasterChannel);
}

#pragma mark Control Values Accessors

void    NovaLINKAudioDevice::CopyMuteFrom(const NovaLINKAudioDevice inDevice,
                                     AudioObjectPropertyScope inScope)
{
    // TODO: Support for devices that have per-channel mute controls but no master mute control
    if(HasSettableMasterMute(inScope) && inDevice.HasMuteControl(inScope, kMasterChannel))
    {
        SetMuteControlValue(inScope,
                            kMasterChannel,
                            inDevice.GetMuteControlValue(inScope, kMasterChannel));
    }
}

void    NovaLINKAudioDevice::CopyVolumeFrom(const NovaLINKAudioDevice inDevice,
                                       AudioObjectPropertyScope inScope)
{
    // Get the volume of the other device.
    bool didGetVolume = false;
    Float32 volume = FLT_MIN;

    if(inDevice.HasVolumeControl(inScope, kMasterChannel))
    {
        volume = inDevice.GetVolumeControlScalarValue(inScope, kMasterChannel);
        didGetVolume = true;
    }

    // Use the average channel volume of the other device if it has no master volume.
    if(!didGetVolume)
    {
        UInt32 numChannels =
            inDevice.GetTotalNumberChannels(inScope == kAudioObjectPropertyScopeInput);
        volume = 0;

        for(UInt32 channel = 1; channel <= numChannels; channel++)
        {
            if(inDevice.HasVolumeControl(inScope, channel))
            {
                volume += inDevice.GetVolumeControlScalarValue(inScope, channel);
                didGetVolume = true;
            }
        }

        if(numChannels > 0)  // Avoid divide by zero.
        {
            volume /= static_cast<Float32>(numChannels);
        }
    }

    // Set the volume of this device.
    if(didGetVolume && volume != FLT_MIN)
    {
        bool didSetVolume = false;

        try
        {
            didSetVolume = SetMasterVolumeScalar(inScope, volume);
        }
        catch(CAException e)
        {
            OSStatus err = e.GetError();
            char err4CC[5] = CA4CCToCString(err);
            CFStringRef uid = CopyDeviceUID();
            LogWarning("NovaLINKAudioDevice::CopyVolumeFrom: CAException '%s' trying to set master "
                       "volume of %s",
                       err4CC,
                       CFStringGetCStringPtr(uid, kCFStringEncodingUTF8));
            CFRelease(uid);
        }

        if(!didSetVolume)
        {
            // Couldn't find a master volume control to set, so try to find a virtual one
            Float32 virtualMasterVolume;
            bool success = inDevice.GetVirtualMasterVolumeScalar(inScope, virtualMasterVolume);
            if(success)
            {
                didSetVolume = SetVirtualMasterVolumeScalar(inScope, virtualMasterVolume);
            }
        }

        if(!didSetVolume)
        {
            // Couldn't set a master or virtual master volume, so as a fallback try to set each
            // channel individually.
            UInt32 numChannels = GetTotalNumberChannels(inScope == kAudioObjectPropertyScopeInput);
            for(UInt32 channel = 1; channel <= numChannels; channel++)
            {
                if(HasVolumeControl(inScope, channel) && VolumeControlIsSettable(inScope, channel))
                {
                    SetVolumeControlScalarValue(inScope, channel, volume);
                }
            }
        }
    }
}

bool    NovaLINKAudioDevice::SetMasterVolumeScalar(AudioObjectPropertyScope inScope, Float32 inVolume)
{
    if(HasSettableMasterVolume(inScope))
    {
        SetVolumeControlScalarValue(inScope, kMasterChannel, inVolume);
        return true;
    }

    return false;
}

bool    NovaLINKAudioDevice::GetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                     Float32& outVirtualMasterVolume) const
{
    AudioObjectPropertyAddress virtualMasterVolumeAddress = {
        kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
        inScope,
        kAudioObjectPropertyElementMaster
    };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if(!AudioHardwareServiceHasProperty(GetObjectID(), &virtualMasterVolumeAddress))
    {
        return false;
    }
#pragma clang diagnostic pop

    UInt32 virtualMasterVolumePropertySize = sizeof(Float32);
    return kAudioServicesNoError == AHSGetPropertyData(GetObjectID(),
                                                       &virtualMasterVolumeAddress,
                                                       &virtualMasterVolumePropertySize,
                                                       &outVirtualMasterVolume);
}

bool    NovaLINKAudioDevice::SetVirtualMasterVolumeScalar(AudioObjectPropertyScope inScope,
                                                     Float32 inVolume)
{
    // TODO: For me, setting the virtual master volume sets all the device's channels to the same volume, meaning you can't
    //       keep any channels quieter than the others. The expected behaviour is to scale the channel volumes
    //       proportionally. So to do this properly I think we'd have to store NovaLINKDevice's previous volume and calculate
    //       each channel's new volume from its current volume and the distance between NovaLINKDevice's old and new volumes.
    //
    //       The docs kAudioHardwareServiceDeviceProperty_VirtualMasterVolume for say
    //           "If the device has individual channel volume controls, this property will apply to those identified by the
    //           device's preferred multi-channel layout (or preferred stereo pair if the device is stereo only). Note that
    //           this control maintains the relative balance between all the channels it affects.
    //       so I'm not sure why that's not working here. As a workaround we take the to device's (virtual master) balance
    //       before changing the volume and set it back after, but of course that'll only work for stereo devices.

    bool didSetVolume = false;

    if(HasSettableVirtualMasterVolume(inScope))
    {
        // Not sure why, but setting the virtual master volume sets all channels to the same volume. As a workaround, we store
        // the current balance here so we can reset it after setting the volume.
        Float32 virtualMasterBalance;
        bool didGetVirtualMasterBalance = GetVirtualMasterBalance(inScope, virtualMasterBalance);

        AudioObjectPropertyAddress virtualMasterVolumeAddress = {
            kAudioHardwareServiceDeviceProperty_VirtualMainVolume,
            inScope,
            kAudioObjectPropertyElementMaster
        };

        didSetVolume = (kAudioServicesNoError == AHSSetPropertyData(GetObjectID(),
                                                                    &virtualMasterVolumeAddress,
                                                                    sizeof(Float32),
                                                                    &inVolume));

        // Reset the balance
        AudioObjectPropertyAddress virtualMasterBalanceAddress = {
            kAudioHardwareServiceDeviceProperty_VirtualMainBalance,
            inScope,
            kAudioObjectPropertyElementMaster
        };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        if(didSetVolume &&
           didGetVirtualMasterBalance &&
           AudioHardwareServiceHasProperty(GetObjectID(), &virtualMasterBalanceAddress))
        {
            Boolean balanceIsSettable;
            OSStatus err = AudioHardwareServiceIsPropertySettable(GetObjectID(),
                                                                  &virtualMasterBalanceAddress,
                                                                  &balanceIsSettable);
            if(err == kAudioServicesNoError && balanceIsSettable)
            {
                AHSSetPropertyData(GetObjectID(),
                                   &virtualMasterBalanceAddress,
                                   sizeof(Float32),
                                   &virtualMasterBalance);
            }
        }
#pragma clang diagnostic pop
    }

    return didSetVolume;
}

bool    NovaLINKAudioDevice::GetVirtualMasterBalance(AudioObjectPropertyScope inScope,
                                                Float32& outVirtualMasterBalance) const
{
    AudioObjectPropertyAddress virtualMasterBalanceAddress = {
        kAudioHardwareServiceDeviceProperty_VirtualMainBalance,
        inScope,
        kAudioObjectPropertyElementMaster
    };

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if(!AudioHardwareServiceHasProperty(GetObjectID(), &virtualMasterBalanceAddress))
    {
        return false;
    }
#pragma clang diagnostic pop

    UInt32 virtualMasterVolumePropertySize = sizeof(Float32);
    return kAudioServicesNoError == AHSGetPropertyData(GetObjectID(),
                                                       &virtualMasterBalanceAddress,
                                                       &virtualMasterVolumePropertySize,
                                                       &outVirtualMasterBalance);
}

#pragma mark Implementation

bool    NovaLINKAudioDevice::IsNovaLINKDevice(bool inIncludeUISoundsInstance) const
{
    bool isNovaLINKDevice = false;

    if(GetObjectID() != kAudioObjectUnknown)
    {
        // Check the device's UID to see whether it's NovaLINKDevice.
        CFStringRef uid = CopyDeviceUID();

        isNovaLINKDevice =
            CFEqual(uid, CFSTR(kNovaLINKDeviceUID)) ||
                    (inIncludeUISoundsInstance && CFEqual(uid, CFSTR(kNovaLINKDeviceUID_UISounds)));

        CFRelease(uid);
    }

    return isNovaLINKDevice;
}

// static
OSStatus    NovaLINKAudioDevice::AHSGetPropertyData(AudioObjectID inObjectID,
                                               const AudioObjectPropertyAddress* inAddress,
                                               UInt32* ioDataSize,
                                               void* outData)
{
    // The docs for AudioHardwareServiceGetPropertyData specifically allow passing NULL for
    // inQualifierData as we do here, but it's declared in an assume_nonnull section so we have to
    // disable the warning here. I'm not sure why inQualifierData isn't __nullable. I'm assuming
    // it's either a backwards compatibility thing or just a bug.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    // The non-depreciated version of this (and the setter below) doesn't seem to support devices
    // other than the default
    return AudioHardwareServiceGetPropertyData(inObjectID, inAddress, 0, NULL, ioDataSize, outData);
#pragma clang diagnostic pop
}

// static
OSStatus    NovaLINKAudioDevice::AHSSetPropertyData(AudioObjectID inObjectID,
                                               const AudioObjectPropertyAddress* inAddress,
                                               UInt32 inDataSize,
                                               const void* inData)
{
    // See the explanation about these pragmas in AHSGetPropertyData
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnonnull"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    return AudioHardwareServiceSetPropertyData(inObjectID, inAddress, 0, NULL, inDataSize, inData);
#pragma clang diagnostic pop
}

