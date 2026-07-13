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
//  NovaLINKiTunes.m
//  NovaLINKApp
//
//  Copyright © 2016-2018 Kyle Neideck
//

// Self Include
#import "NovaLINKiTunes.h"

// Auto-generated Scripting Bridge header
#import "iTunes.h"

// Local Includes
#import "NovaLINKScriptingBridge.h"

// PublicUtility Includes
#import "CADebugMacros.h"


#pragma clang assume_nonnull begin

@implementation NovaLINKiTunes {
    NovaLINKScriptingBridge* scriptingBridge;
}

+ (NSUUID*) sharedMusicPlayerID {
    NSUUID* __nullable musicPlayerID = [[NSUUID alloc] initWithUUIDString:@"7B62B5BF-CF90-4938-84E3-F16DEDC3F608"];
    NSAssert(musicPlayerID, @"NovaLINKiTunes::sharedMusicPlayerID: !musicPlayerID");
    return (NSUUID*)musicPlayerID;
}

- (instancetype) init {
    if ((self = [super initWithMusicPlayerID:[NovaLINKiTunes sharedMusicPlayerID]
                                        name:@"iTunes"
                                    bundleID:@"com.apple.iTunes"])) {
        scriptingBridge = [[NovaLINKScriptingBridge alloc] initWithMusicPlayer:self];
    }
    
    return self;
}

- (iTunesApplication* __nullable) iTunes {
    return (iTunesApplication*)scriptingBridge.application;
}

- (void) wasSelected {
    [super wasSelected];
    [scriptingBridge ensurePermission];
}

- (BOOL) isRunning {
    return self.iTunes.running;
}

// isPlaying and isPaused check self.running first just in case iTunes is closed but self.iTunes hasn't become
// nil yet. In that case, reading self.iTunes.playerState could make Scripting Bridge open iTunes.

- (BOOL) isPlaying {
    return self.running && (self.iTunes.playerState == iTunesEPlSPlaying);
}

- (BOOL) isPaused {
    return self.running && (self.iTunes.playerState == iTunesEPlSPaused);
}

- (BOOL) pause {
    // isPlaying checks isRunning, so we don't need to check it here and waste an Apple event
    BOOL wasPlaying = self.playing;
    
    if (wasPlaying) {
        DebugMsg("NovaLINKiTunes::pause: Pausing iTunes");
        [self.iTunes pause];
    }
    
    return wasPlaying;
}

- (BOOL) unpause {
    // isPaused checks isRunning, so we don't need to check it here and waste an Apple event
    BOOL wasPaused = self.paused;
    
    if (wasPaused) {
        DebugMsg("NovaLINKiTunes::unpause: Unpausing iTunes");
        [self.iTunes playpause];
    }
    
    return wasPaused;
}

@end

#pragma clang assume_nonnull end

