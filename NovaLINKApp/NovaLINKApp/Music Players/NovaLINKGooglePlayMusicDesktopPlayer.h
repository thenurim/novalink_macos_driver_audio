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
//  NovaLINKGooglePlayMusicDesktopPlayer.h
//  NovaLINKApp
//
//  Copyright © 2019 Kyle Neideck
//
//  We have a lot more code for GPMDP than most music players largely because GPMDP has a WebSockets
//  API and because the user has to enter a code from GPMDP to allow NovaLINKApp to control it.
//  Currently, the other music players all have AppleScript APIs, so for them the OS asks the user
//  for permission on our behalf automatically and handles the whole process for us.
//
//  This class implements the usual NovaLINKMusicPlayer methods and handles the UI for authenticating
//  with GPMDP. NovaLINKGooglePlayMusicDesktopPlayerConnection manages the connection to GPMDP and hides
//  the details of its API.
//

// Superclass/Protocol Import
#import "NovaLINKMusicPlayer.h"


#pragma clang assume_nonnull begin

API_AVAILABLE(macos(10.10))
@interface NovaLINKGooglePlayMusicDesktopPlayer : NovaLINKMusicPlayerBase<NovaLINKMusicPlayer>

+ (NSArray<id<NovaLINKMusicPlayer>>*) createInstancesWithDefaults:(NovaLINKUserDefaults*)userDefaults;

@end

#pragma clang assume_nonnull end

