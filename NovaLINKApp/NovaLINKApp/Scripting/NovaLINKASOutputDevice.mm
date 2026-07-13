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
//  NovaLINKASOutputDevice.mm
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//

// Self Include
#import "NovaLINKASOutputDevice.h"

// Local Includes
#import "NovaLINKAudioDevice.h"

// PublicUtility Includes
#import "CADebugMacros.h"


#pragma clang assume_nonnull begin

@implementation NovaLINKASOutputDevice {
    NSScriptObjectSpecifier* parentSpecifier;
    NovaLINKAudioDevice device;
    NovaLINKAudioDeviceManager* audioDevices;
}

- (instancetype) initWithAudioObjectID:(AudioObjectID)objID
                          audioDevices:(NovaLINKAudioDeviceManager*)devices
                       parentSpecifier:(NSScriptObjectSpecifier* __nullable)parent {
    if ((self = [super init])) {
        parentSpecifier = parent;
        device = objID;
        audioDevices = devices;
    }

    return self;
}

- (NSString*) name {
    return (NSString*)CFBridgingRelease(device.CopyName());
}

- (BOOL) selected {
    return [audioDevices isOutputDevice:device];
}

- (void) setSelected:(BOOL)selected {
    if (selected && ![self selected]) {
        DebugMsg("NovaLINKASOutputDevice::setSelected: A script is setting output device to %s",
                 [[self name] UTF8String]);

        NSError* err = [audioDevices setOutputDeviceWithID:device revertOnFailure:YES];
        (void)err;  // TODO: Return an error to the script somehow if this isn't nil. Also, should
                    //       we return an error if the script tries to set this property to false?     
    }
}

- (NSScriptObjectSpecifier* __nullable) objectSpecifier {
    NSScriptClassDescription* parentClassDescription = [parentSpecifier keyClassDescription];
    return [[NSNameSpecifier alloc] initWithContainerClassDescription:parentClassDescription
                                                   containerSpecifier:parentSpecifier
                                                                  key:@"output devices"
                                                                 name:self.name];
}

@end

#pragma clang assume_nonnull end

