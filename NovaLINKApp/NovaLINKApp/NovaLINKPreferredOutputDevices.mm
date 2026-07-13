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
//  NovaLINKPreferredOutputDevices.m
//  NovaLINKApp
//
//  Copyright © 2018 Kyle Neideck
//

// Self Include
#import "NovaLINKPreferredOutputDevices.h"

// Local Includes
#import "NovaLINK_Types.h"
#import "NovaLINK_Utils.h"
#import "NovaLINKAudioDevice.h"

// PublicUtility Includes
#import "CAAutoDisposer.h"
#import "CAHALAudioSystemObject.h"
#import "CAPropertyAddress.h"


#pragma clang assume_nonnull begin

// The Plist file CoreAudio stores the preferred devices list in. It isn't part of the public API,
// but if this class fails to read/parse it, NovaLINKApp will continue without it.
NSString* const kAudioSystemSettingsPlist =
    @"/Library/Preferences/Audio/com.apple.audio.SystemSettings.plist";

@implementation NovaLINKPreferredOutputDevices {
    NSRecursiveLock* _stateLock;

    // Used to change NovaLINKApp's output device.
    NovaLINKAudioDeviceManager* _devices;

    // User settings and data.
    NovaLINKUserDefaults* _userDefaults;

    // The UIDs of the preferred devices, in order of preference. The most-preferred device is at
    // index 0. This list is derived from several sources.
    NSArray<NSString*>* _preferredDeviceUIDs;

    // Called when a device is connected or disconnected.
    AudioObjectPropertyListenerBlock _deviceListListener;
}

- (instancetype) initWithDevices:(NovaLINKAudioDeviceManager*)devices
                    userDefaults:(NovaLINKUserDefaults*)userDefaults {
    if ((self = [super init])) {
        _stateLock = [NSRecursiveLock new];
        _devices = devices;
        _userDefaults = userDefaults;
        _preferredDeviceUIDs = [self readPreferredDevices];

        DebugMsg("NovaLINKPreferredOutputDevices::initWithDevices: Preferred devices: %s",
                 _preferredDeviceUIDs.debugDescription.UTF8String);

        [self listenForDevicesAddedOrRemoved];
    }

    return self;
}

- (void) dealloc {
    @try {
        [_stateLock lock];

        // Tell CoreAudio not to call the listener block anymore.
        CAHALAudioSystemObject().RemovePropertyListenerBlock(
            CAPropertyAddress(kAudioHardwarePropertyDevices),
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
            _deviceListListener);
    } @finally {
        [_stateLock unlock];
    }
}

// Reads the preferred devices list from CoreAudio's Plist file. Uses NovaLINKApp's stored list to fill
// in the blanks, if necessary.
- (NSArray<NSString*>*) readPreferredDevices {
    // Read the Plist file into a dictionary.
    //
    // TODO: If this file doesn't exist, try paths used by older versions of macOS. (If there are
    //       any, that is. I haven't checked.)
    NSURL* url = [NSURL fileURLWithPath:kAudioSystemSettingsPlist];
    NSError* error = nil;
    NSData* data = [NSData dataWithContentsOfURL:url options:0 error:&error];
    NSDictionary* settings = nil;

    if (data && !error) {
        settings = [NSPropertyListSerialization propertyListWithData:data
                                                             options:NSPropertyListImmutable
                                                              format:nil
                                                               error:&error];
    }

    // Default to a list with just the systemwide default device (or an empty list if that fails) if
    // we can't read the preferred devices from the Plist because preferredDeviceUIDsFrom will use
    // NovaLINKApp's stored preferred devices to fill in the rest optimistically. This doesn't help us
    // tell when to switch to a newly connected device, but it should improve our chances of
    // switching to the best device if the current output device is disconnected.
    NSArray<NSDictionary*>* _Nonnull preferredOutputDeviceInfos = @[];

    // If we can't read the Plist, we only know that the current systemwide default device is the
    // most-preferred device that's currently connected.
    //
    // TODO: If we are able to read the Plist, check that the systemwide default device is the
    //       most-preferred device in the list from the Plist that's also connected. If it isn't,
    //       the format of the Plist has probably changed, so we should ignore its data and log a
    //       warning.
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice defaultDevice = CAHALAudioSystemObject().GetDefaultAudioDevice(false, false);
        NSString* __nullable defaultDeviceUID =
            (__bridge_transfer NSString* __nullable)defaultDevice.CopyDeviceUID();

        if (defaultDeviceUID) {
            preferredOutputDeviceInfos = @[ @{ @"uid": NovaLINKNN(defaultDeviceUID) } ];
        }
    });

    if (error || !data || !settings) {
        // The Plist file either doesn't exist or we weren't able to parse it.
        LogWarning("NovaLINKPreferredOutputDevices::readPreferredDevices: Couldn't read %s. "
                   "(data = %s, settings = %s) Error: %s",
                   kAudioSystemSettingsPlist.UTF8String,
                   data.debugDescription.UTF8String,
                   settings.debugDescription.UTF8String,
                   error.debugDescription.UTF8String);
    } else if (!settings[@"preferred devices"]) {
        // The Plist doesn't include the lists of preferred devices (for input, output and system
        // output).
        LogWarning("NovaLINKPreferredOutputDevices::readPreferredDevices: No preferred devices in %s",
                   settings.debugDescription.UTF8String);
    } else if (!settings[@"preferred devices"][@"output"]) {
        // The Plist doesn't include the list of preferred output devices.
        LogWarning("NovaLINKPreferredOutputDevices::readPreferredDevices: "
                   "No preferred output devices in %s",
                   settings.debugDescription.UTF8String);
    } else {
        // Copy the preferred devices out of the Plist.
        preferredOutputDeviceInfos = NovaLINKNN(settings[@"preferred devices"][@"output"]);
    }

    return [self preferredDeviceUIDsFrom:preferredOutputDeviceInfos
               storedPreferredDeviceUIDs:_userDefaults.preferredDeviceUIDs];
}

- (NSArray<NSString*>*) preferredDeviceUIDsFrom:(NSArray<NSDictionary*>*)deviceInfos
                      storedPreferredDeviceUIDs:(NSArray<NSString*>*)storedUIDs {
    NSArray<NSString*>* deviceUIDs = @[];
    int storedPreferredDeviceIdx = 0;

    for (NSDictionary* deviceInfo in deviceInfos) {
        // Check the Plist actually has a UID for this device.
        if (![deviceInfo[@"uid"] isKindOfClass:NSString.class]) {
            LogWarning("NovaLINKPreferredOutputDevices::preferredDeviceUIDsFrom: No UID in %s",
                       deviceInfo.debugDescription.UTF8String);
            continue;
        }

        NSString* uid = deviceInfo[@"uid"];

        if ([uid isEqualToString:@kNovaLINKDeviceUID] ||
            [uid isEqualToString:@kNovaLINKDeviceUID_UISounds] ||
            [uid isEqualToString:@kNovaLINKNullDeviceUID]) {
            // This is one of the NovaLINK devices, so look for a preferred device saved
            // from a previous run of NovaLINKApp and add it instead.
            //
            // NovaLINKApp has to set NovaLINKDevice, and often also the Null Device for a short time, as the
            // systemwide default audio device, which makes CoreAudio put them in its Plist. And
            // since the Plist is limited to three devices, it only gives us one or two usable ones.
            // Ideally, CoreAudio just wouldn't add our devices to its list, but I don't think we
            // can prevent that. And we can't be sure that editing its Plist file ourselves would be
            // safe.
            //
            // TODO: This doesn't work if the user has made NovaLINKDevice the systemwide default device
            //       themselves since the last time they closed NovaLINKApp. We might be able to fix that
            //       by having a background process watch the Plist for changes while NovaLINKApp is
            //       closed or something like that, but I doubt there's a nice or simple solution.
            deviceUIDs = [self addNextStoredPreferredDevice:&storedPreferredDeviceIdx
                                        preferredDeviceUIDs:deviceUIDs
                                  storedPreferredDeviceUIDs:storedUIDs];
        } else if (![deviceUIDs containsObject:uid]) {
            // Add this preferred device's UID to the list.
            deviceUIDs = [deviceUIDs arrayByAddingObject:uid];
        }
    }

    // Fill in any remaining places in the list with stored devices. Limit the list to three devices
    // just to match CoreAudio's behaviour.
    while ((storedPreferredDeviceIdx < storedUIDs.count) && (deviceUIDs.count < 3)) {
        deviceUIDs = [self addNextStoredPreferredDevice:&storedPreferredDeviceIdx
                                    preferredDeviceUIDs:deviceUIDs
                              storedPreferredDeviceUIDs:storedUIDs];
    }

    return deviceUIDs;
}

- (NSArray<NSString*>*) addNextStoredPreferredDevice:(int*)storedPreferredDeviceIdx
                                 preferredDeviceUIDs:(NSArray<NSString*>*)deviceUIDs
                           storedPreferredDeviceUIDs:(NSArray<NSString*>*)storedUIDs {
    NSString* __nullable storedPreferredDevice;

    // Try to find a stored UID that isn't already in the list.
    do {
        storedPreferredDevice = (*storedPreferredDeviceIdx < storedUIDs.count) ?
                                    storedUIDs[*storedPreferredDeviceIdx] : nil;
        (*storedPreferredDeviceIdx)++;
    } while (storedPreferredDevice && [deviceUIDs containsObject:NovaLINKNN(storedPreferredDevice)]);

    // If we found a stored UID, add it to the list.
    if (storedPreferredDevice) {
        DebugMsg("NovaLINKPreferredOutputDevices::addNextStoredPreferredDevice: "
                 "Adding stored preferred device: %s",
                 storedPreferredDevice.UTF8String);
        deviceUIDs = [deviceUIDs arrayByAddingObject:NovaLINKNN(storedPreferredDevice)];
    }

    return deviceUIDs;
}

- (void) listenForDevicesAddedOrRemoved {
    // Create the block that will run when a device is added or removed.
    NovaLINKPreferredOutputDevices* __weak weakSelf = self;

    _deviceListListener = ^(UInt32 inNumberAddresses,
                            const AudioObjectPropertyAddress* inAddresses) {
        #pragma unused (inNumberAddresses, inAddresses)

        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            [weakSelf connectedDeviceListChanged];
        });
    };

    // Register the listener block with CoreAudio.
    CAHALAudioSystemObject().AddPropertyListenerBlock(
        CAPropertyAddress(kAudioHardwarePropertyDevices),
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0),
        _deviceListListener);
}

- (void) connectedDeviceListChanged {
    @try {
        [_stateLock lock];

        // Decide which device should be the output device now. If a device has been connected and
        // it's preferred over the current output device, we'll change to that device. If the
        // current output device has been removed, we'll change to the next most-preferred device.
        AudioObjectID preferredDevice = [self findPreferredDevice];

        if (preferredDevice == kAudioObjectUnknown) {
            LogWarning("NovaLINKPreferredOutputDevices::connectedDeviceListChanged: "
                       "No preferred device found.");
        } else if (_devices.outputDevice.GetObjectID() == preferredDevice) {
            DebugMsg("NovaLINKPreferredOutputDevices::connectedDeviceListChanged: "
                     "The preferred device is already set as the output device.");
        } else {
            // Change to the preferred device.
            DebugMsg("NovaLINKPreferredOutputDevices::connectedDeviceListChanged: "
                     "Changing output device to %d.",
                     preferredDevice);
            NSError* __nullable error = [_devices setOutputDeviceWithID:preferredDevice
                                                        revertOnFailure:YES];
            if (error) {
                // There's not much we can do if this happens.
                LogError("NovaLINKPreferredOutputDevices::connectedDeviceListChanged: "
                         "Failed to change to preferred device. Error: %s",
                         error.debugDescription.UTF8String);
            }
        }
    } @finally {
        [_stateLock unlock];
    }
}

- (AudioObjectID) findPreferredDevice {
    AudioObjectID preferredDevice = kAudioObjectUnknown;
    CAHALAudioSystemObject audioSystem;

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice defaultDevice = audioSystem.GetDefaultAudioDevice(false, false);

        if (!defaultDevice.IsNovaLINKDeviceInstance()) {
            // NovaLINKDevice isn't the systemwide default device, so we know the default device is the
            // most-preferred device that's currently connected.
            preferredDevice = defaultDevice;
        }
    });

    if (preferredDevice == kAudioObjectUnknown) {
        // NovaLINKDevice is the systemwide default device, so this method is probably being called after
        // launch, since we set NovaLINKDevice as the default device then. It could also be that the user
        // set it manually or that NovaLINKApp failed to change it back the last time it closed. Either
        // way, we'll try to find a device to use in the Plist or stored list instead.
        DebugMsg("NovaLINKPreferredOutputDevices::findPreferredDevice: Checking Plist and stored list.");

        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            preferredDevice = [self findPreferredDeviceInDerivedList];
        });
    }

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        if (preferredDevice == kAudioObjectUnknown && [self isCurrentOutputDeviceConnected]) {
            // The output device (for NovaLINKApp) has been set and is still connected. We haven't found
            // a better device to use, so prefer leaving the output device as it is.
            DebugMsg("NovaLINKPreferredOutputDevices::findPreferredDevice: "
                     "Choosing the current output device as the preferred device.");
            preferredDevice = _devices.outputDevice.GetObjectID();
        }
    });

    if (preferredDevice == kAudioObjectUnknown) {
        // The current output device has been disconnected or hasn't been set yet and there are no
        // preferred devices connected, so pick one arbitrarily.
        DebugMsg("NovaLINKPreferredOutputDevices::findPreferredDevice: Choosing an arbitrary device.");
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            preferredDevice = [self findPreferredDeviceByLatency];
        });
    }

    return preferredDevice;
}

// Looks for a suitable device in _preferredDeviceUIDs, the list of preferred devices derived from
// CoreAudio's Plist and NovaLINKApp's stored list.
- (AudioObjectID) findPreferredDeviceInDerivedList {
    CAHALAudioSystemObject audioSystem;
    UInt32 numDevices = audioSystem.GetNumberAudioDevices();

    // Get the list of currently connected audio devices.
    CAAutoArrayDelete<AudioObjectID> devices(numDevices);
    audioSystem.GetAudioDevices(numDevices, devices);

    // Look through the preferred devices list to see if one's connected. Return the first one we
    // find because they're stored in order of preference.
    for (int position = 0; position < _preferredDeviceUIDs.count; position++) {
        // Compare the current preferred device to each connected device by UID.
        for (UInt32 i = 0; i < numDevices; i++) {
            NSString* __nullable connectedDeviceUID = nil;

            NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
                // Skip devices we can't use, e.g. NovaLINKDevice.
                if (NovaLINKAudioDevice(devices[i]).CanBeOutputDeviceInNovaLINKApp()) {
                    // Get the connected device's UID.
                    connectedDeviceUID =
                        (__bridge NSString* __nullable)CAHALAudioDevice(devices[i]).CopyDeviceUID();
                }
            });

            // If the UIDs match, the current preferred device is connected.
            //
            // If you plug a USB device in to different USB port, macOS might assign it a different
            // UID and make it fail to match its old UID here. CoreAudio/macOS doesn't seem to
            // handle that case either, though, so it's probably not worth worrying about.
            if ([connectedDeviceUID isEqualToString:_preferredDeviceUIDs[position]]) {
                // We're iterating through the preferred devices from most to least-preferred, so
                // we've found the device to use.
                DebugMsg("NovaLINKPreferredOutputDevices::findPreferredDeviceInDerivedList: "
                         "Found preferred device '%s' at position %d",
                         _preferredDeviceUIDs[position].UTF8String,
                         position);
                return devices[i];
            }
        }

        DebugMsg("NovaLINKPreferredOutputDevices::findPreferredDeviceInDerivedList: "
                 "Preferred device not connected: %s",
                 _preferredDeviceUIDs[position].UTF8String);
    }

    return kAudioObjectUnknown;
}

- (bool) isCurrentOutputDeviceConnected {
    if (_devices.outputDevice.GetObjectID() == kAudioObjectUnknown) {
        DebugMsg("NovaLINKPreferredOutputDevices::isCurrentOutputDeviceConnected: "
                 "The output device hasn't been set yet.");
        return false;
    }

    CAHALAudioSystemObject audioSystem;
    UInt32 numDevices = audioSystem.GetNumberAudioDevices();

    // Get the list of currently connected audio devices.
    CAAutoArrayDelete<AudioObjectID> devices(numDevices);
    audioSystem.GetAudioDevices(numDevices, devices);

    // Look for the current output device in the list of connected devices.
    for (UInt32 i = 0; i < numDevices; i++) {
        // TODO: Are AudioObjectIDs reused? If they are, could that cause a collision here?
        if (_devices.outputDevice.GetObjectID() == devices[i]) {
            DebugMsg("NovaLINKPreferredOutputDevices::isCurrentOutputDeviceConnected: "
                     "The output device is connected.");
            return true;
        }
    }

    DebugMsg("NovaLINKPreferredOutputDevices::isCurrentOutputDeviceConnected: "
             "The output device is not connected.");
    return false;
}

// Returns the audio output device with the lowest latency. Used when we have no better way to
// choose the output device for NovaLINKApp to use.
- (AudioObjectID) findPreferredDeviceByLatency {
    CAHALAudioSystemObject audioSystem;
    UInt32 numDevices = audioSystem.GetNumberAudioDevices();
    AudioObjectID minLatencyDevice = kAudioObjectUnknown;
    UInt32 minLatency = UINT32_MAX;

    CAAutoArrayDelete<AudioObjectID> devices(numDevices);
    audioSystem.GetAudioDevices(numDevices, devices);

    for (UInt32 i = 0; i < numDevices; i++) {
        NovaLINKAudioDevice device(devices[i]);

        if (!device.IsNovaLINKDeviceInstance()) {
            BOOL hasOutputChannels = NO;

            NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, "GetTotalNumberChannels", [&] {
                hasOutputChannels = device.GetTotalNumberChannels(/* inIsInput = */ false) > 0;
            });

            if (hasOutputChannels) {
                NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, "GetLatency", [&] {
                    UInt32 latency = device.GetLatency(false);

                    if (latency < minLatency) {
                        minLatencyDevice = devices[i];
                        minLatency = latency;
                    }
                });
            }
        }
    }

    return minLatencyDevice;
}

- (void) userChangedOutputDeviceTo:(AudioObjectID)device {
    @try {
        [_stateLock lock];

        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            // Add the new output device to the list.
            NSString* __nullable outputDeviceUID =
                (__bridge_transfer NSString* __nullable)CAHALAudioDevice(device).CopyDeviceUID();

            if (outputDeviceUID) {
                // Limit the list to three devices because that's what macOS does.
                if (_preferredDeviceUIDs.count >= 2) {
                    _preferredDeviceUIDs = @[NovaLINKNN(outputDeviceUID),
                                          _preferredDeviceUIDs[0],
                                          _preferredDeviceUIDs[1]];
                } else if (_preferredDeviceUIDs.count >= 1) {
                    _preferredDeviceUIDs = @[NovaLINKNN(outputDeviceUID), _preferredDeviceUIDs[0]];
                } else {
                    _preferredDeviceUIDs = @[NovaLINKNN(outputDeviceUID)];
                }

                DebugMsg("NovaLINKPreferredOutputDevices::userChangedOutputDeviceTo: "
                         "Preferred devices: %s",
                         _preferredDeviceUIDs.debugDescription.UTF8String);

                // Save the list.
                _userDefaults.preferredDeviceUIDs = _preferredDeviceUIDs;
            } else {
                LogWarning("NovaLINKPreferredOutputDevices::userChangedOutputDeviceTo: "
                           "Output device has no UID");
            }
        });
    } @finally {
        [_stateLock unlock];
    }
}

@end

#pragma clang assume_nonnull end

