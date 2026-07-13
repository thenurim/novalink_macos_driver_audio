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
//  NovaLINKAppDelegate+AppleScript.h
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//

#import "NovaLINKAppDelegate.h"

// Local Includes
#import "NovaLINKASOutputDevice.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKAppDelegate (AppleScript)

- (BOOL) application:(NSApplication*)sender delegateHandlesKey:(NSString*)key;

@property NovaLINKASOutputDevice* selectedOutputDevice;
@property (readonly) NSArray<NovaLINKASOutputDevice*>* outputDevices;
@property double mainVolume;

@end

#pragma clang assume_nonnull end
