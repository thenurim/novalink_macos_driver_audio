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
//  NovaLINKStatusBarItem.h
//  NovaLINKApp
//
//  Copyright © 2019, 2020 Kyle Neideck
//
//  The button in the system status bar (the bar with volume, battery, clock, etc.) to show the main
//  menu for the app. These are called "menu bar extras" in the Human Interface Guidelines.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKDebugLoggingMenuItem.h"

// System Includes
#import <Cocoa/Cocoa.h>

// Forward Declarations
@class NovaLINKUserDefaults;


#pragma clang assume_nonnull begin

typedef NS_ENUM(NSInteger, NovaLINKStatusBarIcon) {
    NovaLINKFermataStatusBarIcon = 0,
    NovaLINKVolumeStatusBarIcon
};

static NovaLINKStatusBarIcon const kNovaLINKStatusBarIconMinValue     = NovaLINKFermataStatusBarIcon;
static NovaLINKStatusBarIcon const kNovaLINKStatusBarIconMaxValue     = NovaLINKVolumeStatusBarIcon;
static NovaLINKStatusBarIcon const kNovaLINKStatusBarIconDefaultValue = NovaLINKFermataStatusBarIcon;

@interface NovaLINKStatusBarItem : NSObject

- (instancetype) initWithMenu:(NSMenu*)novaLINKMenu
                 audioDevices:(NovaLINKAudioDeviceManager*)devices
                 userDefaults:(NovaLINKUserDefaults*)defaults;

// Set this to NovaLINKFermataStatusBarIcon to change the icon to the NovaLINK logo.
//
// Set this to NovaLINKFermataStatusBarIcon to change the icon to a volume icon. This icon has the
// advantage of indicating the volume level, but we can't make it the default because it looks the
// same as the icon for the macOS volume status bar item.
@property NovaLINKStatusBarIcon icon;

// If the user holds down the option key when they click the status bar icon, this menu item will be
// shown in the main menu.
- (void) setDebugLoggingMenuItem:(NovaLINKDebugLoggingMenuItem*)menuItem;

@end

#pragma clang assume_nonnull end

