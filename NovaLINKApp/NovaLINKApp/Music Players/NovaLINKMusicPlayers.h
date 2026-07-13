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
//  NovaLINKMusicPlayers.h
//  NovaLINKApp
//
//  Copyright © 2016, 2019 Kyle Neideck
//
//  Holds the music players (i.e. NovaLINKMusicPlayer objects) available in NovaLINKApp. Also keeps track of
//  which music player is currently selected by the user.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKMusicPlayer.h"
#import "NovaLINKUserDefaults.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKMusicPlayers : NSObject

// Calls initWithAudioDevices:musicPlayers: with sensible defaults.
- (instancetype) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices
                         userDefaults:(NovaLINKUserDefaults*)defaults;

// defaultMusicPlayerID is the musicPlayerID (see NovaLINKMusicPlayer.h) of the music player that should be
// selected by default.
//
// The createInstancesWithDefaults method of each class in musicPlayerClasses will be called and
// the results will be stored in the musicPlayers property.
- (instancetype) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices
                 defaultMusicPlayerID:(NSUUID*)defaultMusicPlayerID
                   musicPlayerClasses:(NSArray<Class<NovaLINKMusicPlayer>>*)musicPlayerClasses
                         userDefaults:(NovaLINKUserDefaults*)defaults;

@property (readonly) NSArray<id<NovaLINKMusicPlayer>>* musicPlayers;

// The music player currently selected in the preferences menu. NovaLINKDevice is informed when this property
// is changed.
@property id<NovaLINKMusicPlayer> selectedMusicPlayer;

@end

#pragma clang assume_nonnull end

