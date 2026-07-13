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
//  NovaLINKAutoPauseMenuItem.h
//  NovaLINKApp
//
//  Copyright © 2016 Kyle Neideck
//

// Local Includes
#import "NovaLINKAutoPauseMusic.h"
#import "NovaLINKMusicPlayers.h"
#import "NovaLINKUserDefaults.h"

// System Includes
#import <Cocoa/Cocoa.h>


#pragma clang assume_nonnull begin

@interface NovaLINKAutoPauseMenuItem : NSObject

- (instancetype) initWithMenuItem:(NSMenuItem*)item
                   autoPauseMusic:(NovaLINKAutoPauseMusic*)autoPause
                     musicPlayers:(NovaLINKMusicPlayers*)players
                     userDefaults:(NovaLINKUserDefaults*)defaults;

// Handle events passed along by the delegate (NSMenuDelegate) of the menu containing this menu item.
- (void) parentMenuNeedsUpdate;
- (void) parentMenuItemWillHighlight:(NSMenuItem* __nullable)item;

@end

#pragma clang assume_nonnull end

