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
//  NovaLINKAppDelegate.h
//  NovaLINKApp
//
//  Copyright © 2016, 2017, 2020 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//
//  Sets up and tears down the app.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"

// System Includes
#import <Cocoa/Cocoa.h>


@interface NovaLINKAppDelegate : NSObject <NSApplicationDelegate, NSMenuDelegate>

@property (weak) IBOutlet NSMenu* novaLINKMenu;

@property (weak) IBOutlet NSPanel* aboutPanel;
@property (unsafe_unretained) IBOutlet NSTextView* aboutPanelLicenseView;

@property (weak) IBOutlet NSMenuItem* autoPauseMenuItemUnwrapped;
@property (weak) IBOutlet NSMenuItem* debugLoggingMenuItemUnwrapped;

@property (readonly) NovaLINKAudioDeviceManager* audioDevices;

@end
