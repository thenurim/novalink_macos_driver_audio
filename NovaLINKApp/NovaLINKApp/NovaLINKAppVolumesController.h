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
//  NovaLINKAppVolumesController.h
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"

// System Includes
#import <Cocoa/Cocoa.h>


#pragma clang assume_nonnull begin

typedef struct NovaLINKAppVolumeAndPan {
    int volume;
    int pan;
} NovaLINKAppVolumeAndPan;

@interface NovaLINKAppVolumesController : NSObject

- (id) initWithMenu:(NSMenu*)menu
      appVolumeView:(NSView*)view
       audioDevices:(NovaLINKAudioDeviceManager*)audioDevices;

// See NovaLINKDevice::SetAppVolume.
- (void)  setVolume:(SInt32)volume
forAppWithProcessID:(pid_t)processID
           bundleID:(NSString* __nullable)bundleID;

// See NovaLINKDevice::SetPanVolume.
- (void) setPanPosition:(SInt32)pan
    forAppWithProcessID:(pid_t)processID
               bundleID:(NSString* __nullable)bundleID;

- (NovaLINKAppVolumeAndPan) getVolumeAndPanForApp:(NSRunningApplication *)app;
- (void) setVolumeAndPan:(NovaLINKAppVolumeAndPan)volumeAndPan forApp:(NSRunningApplication*)app;

@end

#pragma clang assume_nonnull end

