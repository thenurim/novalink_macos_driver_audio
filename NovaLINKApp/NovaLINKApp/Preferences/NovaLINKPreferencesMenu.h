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
//  NovaLINKPreferencesMenu.h
//  NovaLINKApp
//
//  Copyright © 2016, 2018, 2019 Kyle Neideck
//
//  Handles the preferences menu UI. The user's preference changes are often passed directly to the driver rather
//  than to other NovaLINKApp classes.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKMusicPlayers.h"
#import "NovaLINKStatusBarItem.h"

// System Includes
#import <Cocoa/Cocoa.h>


NS_ASSUME_NONNULL_BEGIN

@interface NovaLINKPreferencesMenu : NSObject

- (id) initWithNovaLINKMenu:(NSMenu*)inNovaLINKMenu
          audioDevices:(NovaLINKAudioDeviceManager*)inAudioDevices
          musicPlayers:(NovaLINKMusicPlayers*)inMusicPlayers
         statusBarItem:(NovaLINKStatusBarItem*)inStatusBarItem
            aboutPanel:(NSPanel*)inAboutPanel
 aboutPanelLicenseView:(NSTextView*)inAboutPanelLicenseView;

@end

NS_ASSUME_NONNULL_END

