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
//  NovaLINKAutoPauseMusic.h
//  NovaLINKApp
//
//  Copyright © 2016 Kyle Neideck
//
//  When enabled, NovaLINKAutoPauseMusic listens for notifications from NovaLINKDevice to tell when music is playing and
//  pauses the music player if other audio starts.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKMusicPlayers.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKAutoPauseMusic : NSObject

- (id) initWithAudioDevices:(NovaLINKAudioDeviceManager*)inAudioDevices musicPlayers:(NovaLINKMusicPlayers*)inMusicPlayers;

- (void) enable;
- (void) disable;

@end

#pragma clang assume_nonnull end

