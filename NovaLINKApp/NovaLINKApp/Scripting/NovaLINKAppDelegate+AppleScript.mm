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
//  NovaLINKAppDelegate+AppleScript.mm
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//

// Self Include
#import "NovaLINKAppDelegate+AppleScript.h"

// Local Includes
#import "NovaLINKAudioDevice.h"

// PublicUtility Includes
#import "CAHALAudioSystemObject.h"
#import "CAAutoDisposer.h"

const AudioObjectPropertyScope kScope                   = kAudioDevicePropertyScopeOutput;

#pragma clang assume_nonnull begin

@implementation NovaLINKAppDelegate (AppleScript)

- (BOOL) application:(NSApplication*)sender delegateHandlesKey:(NSString*)key {
    #pragma unused (sender)

    if (![key isEqual:@"_keyWindow"]) {
        DebugMsg("NovaLINKAppDelegate:application:delegateHandlesKey: Key queried: '%s'",
                 [key UTF8String]);
    }

    return [@[@"selectedOutputDevice", @"outputDevices", @"mainVolume", @"applications"] containsObject:key];
}

- (NovaLINKASOutputDevice*) selectedOutputDevice {
    AudioObjectID outputDeviceID = [self.audioDevices outputDevice].GetObjectID();

    return [[NovaLINKASOutputDevice alloc] initWithAudioObjectID:outputDeviceID
                                               audioDevices:self.audioDevices
                                            parentSpecifier:[self objectSpecifier]];
}

- (void) setSelectedOutputDevice:(NovaLINKASOutputDevice*)device {
    [device setSelected:YES];
}

- (NSArray<NovaLINKASOutputDevice*>*) outputDevices {
    UInt32 numDevices = CAHALAudioSystemObject().GetNumberAudioDevices();
    
    CAAutoArrayDelete<AudioObjectID> devices(numDevices);
    CAHALAudioSystemObject().GetAudioDevices(numDevices, devices);

    NSMutableArray<NovaLINKASOutputDevice*>* outputDevices =
        [NSMutableArray arrayWithCapacity:numDevices];

    for (UInt32 i = 0; i < numDevices; i++) {
        NovaLINKAudioDevice device(devices[i]);

        if (device.CanBeOutputDeviceInNovaLINKApp()) {
            NovaLINKASOutputDevice* outputDevice =
                [[NovaLINKASOutputDevice alloc] initWithAudioObjectID:device.GetObjectID()
                                                    audioDevices:self.audioDevices
                                                 parentSpecifier:[self objectSpecifier]];

            [outputDevices addObject:outputDevice];
        }
    }

    return outputDevices;
}

- (double) mainVolume {
    NovaLINKAudioDevice novaLINKDevice = [self.audioDevices novaLINKDevice];
    return novaLINKDevice.GetVolumeControlScalarValue(kScope, kMasterChannel);
}

- (void) setMainVolume:(double)mainVolume {
    NovaLINKAudioDevice novaLINKDevice = [self.audioDevices novaLINKDevice];
    novaLINKDevice.SetMasterVolumeScalar(kScope, (Float32)mainVolume);
    [self.outputVolumeSlider setFloatValue:(float)mainVolume];
}

- (NSArray<NovaLINKASApplication*>*) applications {
    NSArray<NSRunningApplication*>* apps = [[NSWorkspace sharedWorkspace] runningApplications];
    NSMutableArray<NovaLINKASApplication*>* applications = [NSMutableArray arrayWithCapacity:[apps count]];

    for (UInt32 i = 0; i < [apps count]; i++) {
        NovaLINKASApplication *app = [[NovaLINKASApplication alloc] initWithApplication:apps[i] volumeController:self.appVolumes parentSpecifier:[self objectSpecifier] index:i];

        [applications addObject:app];
    }

    return applications;
}

@end

#pragma clang assume_nonnull end

