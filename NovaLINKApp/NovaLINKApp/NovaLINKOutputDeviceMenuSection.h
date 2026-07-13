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
//  NovaLINKOutputDeviceMenuSection.h
//  NovaLINKApp
//
//  Copyright © 2016, 2018 Kyle Neideck
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKPreferredOutputDevices.h"

// System Includes
#import <AppKit/AppKit.h>


#pragma clang assume_nonnull begin

@interface NovaLINKOutputDeviceMenuSection : NSObject

- (instancetype) initWithNovaLINKMenu:(NSMenu*)inNovaLINKMenu
                    audioDevices:(NovaLINKAudioDeviceManager*)inAudioDevices
                preferredDevices:(NovaLINKPreferredOutputDevices*)inPreferredDevices;

// To be called when NovaLINKApp has been set to use a different output device. For example, when a new
// device is connected and NovaLINKPreferredOutputDevices decides NovaLINKApp should switch to it.
- (void) outputDeviceDidChange;

@end

#pragma clang assume_nonnull end

