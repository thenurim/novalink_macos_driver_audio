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
#include "CACFArray.h"
#include "CACFDictionary.h"

// STL Includes
#include <map>


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

#pragma mark App Volumes

CFArrayRef NovaLINKDevice::GetAppVolumes() const
{
    CFTypeRef appVolumes = GetPropertyData_CFType(kNovaLINKAppVolumesAddress);

    ThrowIfNULL(appVolumes,
                CAException(kAudioHardwareIllegalOperationError),
                "NovaLINKDevice::GetAppVolumes: !appVolumes");
    ThrowIf(CFGetTypeID(appVolumes) != CFArrayGetTypeID(),
            CAException(kAudioHardwareIllegalOperationError),
            "NovaLINKDevice::GetAppVolumes: Expected CFArray value");

    return static_cast<CFArrayRef>(appVolumes);
}

void NovaLINKDevice::SetAppVolume(SInt32 inVolume,
                                            pid_t inAppProcessID,
                                            CFStringRef __nullable inAppBundleID)
{
    NovaLINKAssert((kAppRelativeVolumeMinRawValue <= inVolume) &&
                      (inVolume <= kAppRelativeVolumeMaxRawValue),
              "NovaLINKDevice::SetAppVolume: Volume out of bounds");

    // Clamp the volume to [kAppRelativeVolumeMinRawValue, kAppPanRightRawValue].
    inVolume = std::max(kAppRelativeVolumeMinRawValue, inVolume);
    inVolume = std::min(kAppRelativeVolumeMaxRawValue, inVolume);

    SendAppVolumeOrPanToNovaLINKDevice(inVolume,
                                  CFSTR(kNovaLINKAppVolumesKey_RelativeVolume),
                                  inAppProcessID,
                                  inAppBundleID);
}

void NovaLINKDevice::SetAppPanPosition(SInt32 inPanPosition,
                                                 pid_t inAppProcessID,
                                                 CFStringRef __nullable inAppBundleID)
{
    NovaLINKAssert((kAppPanLeftRawValue <= inPanPosition) && (inPanPosition <= kAppPanRightRawValue),
              "NovaLINKDevice::SetAppPanPosition: Pan position out of bounds");

    // Clamp the pan position to [kAppPanLeftRawValue, kAppPanRightRawValue].
    inPanPosition = std::max(kAppPanLeftRawValue, inPanPosition);
    inPanPosition = std::min(kAppPanRightRawValue, inPanPosition);

    SendAppVolumeOrPanToNovaLINKDevice(inPanPosition,
                                  CFSTR(kNovaLINKAppVolumesKey_PanPosition),
                                  inAppProcessID,
                                  inAppBundleID);
}

void NovaLINKDevice::SendAppVolumeOrPanToNovaLINKDevice(SInt32 inNewValue,
                                                             CFStringRef inVolumeTypeKey,
                                                             pid_t inAppProcessID,
                                                             CFStringRef __nullable inAppBundleID)
{
    CACFArray appVolumeChanges(true);

    auto addVolumeChange = [&] (pid_t pid, CFStringRef bundleID)
    {
        CACFDictionary appVolumeChange(true);

        appVolumeChange.AddSInt32(CFSTR(kNovaLINKAppVolumesKey_ProcessID), pid);
        appVolumeChange.AddString(CFSTR(kNovaLINKAppVolumesKey_BundleID), bundleID);
        appVolumeChange.AddSInt32(inVolumeTypeKey, inNewValue);

        appVolumeChanges.AppendDictionary(appVolumeChange.GetDict());
    };

    addVolumeChange(inAppProcessID, inAppBundleID);

    // Add the same change for each process the app is responsible for.
    for(CACFString responsibleBundleID : ResponsibleBundleIDsOf(CACFString(inAppBundleID)))
    {
        // Send -1 as the PID so this volume will only ever be matched by bundle ID.
        addVolumeChange(-1, responsibleBundleID.GetCFString());
    }

    CFPropertyListRef changesPList = appVolumeChanges.AsPropertyList();

    // Send the change to NovaLINKDevice.
    SetPropertyData_CFType(kNovaLINKAppVolumesAddress, changesPList);

    // Also send it to the instance of NovaLINKDevice that handles UI sounds.
    mUISoundsNovaLINKDevice.SetPropertyData_CFType(kNovaLINKAppVolumesAddress, changesPList);
}

// This is a temporary solution that lets us control the volumes of some multiprocess apps, i.e.
// apps that play their audio from a process with a different bundle ID.
//
// We can't just check the child processes of the apps' main processes because they're usually
// created with launchd rather than being actual child processes. There's a private API to get the
// processes that an app is "responsible for", so we'll try to use it in the proper fix and only use
// this list if the API doesn't work.
//
// static
std::vector<CACFString>
NovaLINKDevice::ResponsibleBundleIDsOf(CACFString inParentBundleID)
{
    if(!inParentBundleID.IsValid())
    {
        return {};
    }

    std::map<CACFString, std::vector<CACFString>> bundleIDMap = {
        // Finder
        { "com.apple.finder",
            { "com.apple.quicklook.ui.helper",
              "com.apple.quicklook.QuickLookUIService" } },
        // Safari
        { "com.apple.Safari", { "com.apple.WebKit.WebContent" } },
        // Firefox
        { "org.mozilla.firefox", { "org.mozilla.plugincontainer" } },
        // Firefox Nightly
        { "org.mozilla.nightly", { "org.mozilla.plugincontainer" } },
        // VMWare Fusion
        { "com.vmware.fusion", { "com.vmware.vmware-vmx" } },
        // Parallels
        { "com.parallels.desktop.console", { "com.parallels.vm" } },
        // MPlayer OSX Extended
        { "hu.mplayerhq.mplayerosx.extended",
                { "ch.sttz.mplayerosx.extended.binaries.officialsvn" } },
        // Discord
        { "com.hnc.Discord", { "com.hnc.Discord.helper" } },
        // Skype
        { "com.skype.skype", { "com.skype.skype.Helper" } },
        // Google Chrome
        { "com.google.Chrome", { "com.google.Chrome.helper" } },
        // Microsoft Edge
        { "com.microsoft.edgemac", { "com.microsoft.edgemac.helper" } },
        // Arc
        { "company.thebrowser.Browser", { "company.thebrowser.browser.helper" } }
    };

    // Parallels' VM "dock helper" apps have bundle IDs like
    // com.parallels.winapp.87f6bfc236d64d70a81c47f6243add4c.f5a25fdede514f7aa0a475a1873d3287.fs
    if(inParentBundleID.StartsWith(CFSTR("com.parallels.winapp.")))
    {
        return { "com.parallels.vm" };
    }

    return bundleIDMap[inParentBundleID];
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

