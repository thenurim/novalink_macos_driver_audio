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
//  NovaLINKMusicPlayers.mm
//  NovaLINKApp
//
//  Copyright © 2016-2019 Kyle Neideck
//

// Self include
#import "NovaLINKMusicPlayers.h"

// Local includes
#import "NovaLINK_Types.h"

// Music player includes
#import "NovaLINKiTunes.h"
#import "NovaLINKSpotify.h"
#import "NovaLINKVLC.h"
#import "NovaLINKVOX.h"
#import "NovaLINKDecibel.h"
#import "NovaLINKHermes.h"
#import "NovaLINKSwinsian.h"
#import "NovaLINKMusic.h"
#import "NovaLINKGooglePlayMusicDesktopPlayer.h"


#pragma clang assume_nonnull begin

@implementation NovaLINKMusicPlayers {
    NovaLINKAudioDeviceManager* audioDevices;
    NovaLINKUserDefaults* userDefaults;
}

@synthesize selectedMusicPlayer = _selectedMusicPlayer;

- (instancetype) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices
                         userDefaults:(NovaLINKUserDefaults*)defaults {
    // The classes handling each music player we support. If you write a new music player class, add
    // it to this array.
    NSArray<Class<NovaLINKMusicPlayer>>* mpClasses = @[ [NovaLINKVOX class],
                                                   [NovaLINKVLC class],
                                                   [NovaLINKSpotify class],
                                                   [NovaLINKiTunes class],
                                                   [NovaLINKDecibel class],
                                                   [NovaLINKHermes class],
                                                   [NovaLINKSwinsian class],
                                                   [NovaLINKMusic class] ];

    // We only support Google Play Music Desktop Player on macOS 10.10 and higher.
    if (@available(macOS 10.10, *)) {
        mpClasses = [mpClasses arrayByAddingObject:[NovaLINKGooglePlayMusicDesktopPlayer class]];
    }

    return [self initWithAudioDevices:devices
                 defaultMusicPlayerID:[NovaLINKiTunes sharedMusicPlayerID]
                   musicPlayerClasses:mpClasses
                         userDefaults:defaults];
}

- (instancetype) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices
                 defaultMusicPlayerID:(NSUUID*)defaultMusicPlayerID
                   musicPlayerClasses:(NSArray<Class<NovaLINKMusicPlayer>>*)musicPlayerClasses
                         userDefaults:(NovaLINKUserDefaults*)defaults {
    if ((self = [super init])) {
        audioDevices = devices;
        userDefaults = defaults;
        
        // Init _musicPlayers, an array containing one object for each music player in NovaLINKApp.
        //
        // Each music player class has a factory method, createInstancesWithDefaults, that returns
        // all the instances of that class NovaLINKApp will use. (Though so far it's always just one
        // instance.)
        NSMutableArray* musicPlayers = [NSMutableArray new];
        for (Class<NovaLINKMusicPlayer> musicPlayerClass in musicPlayerClasses) {
            NSArray<id<NovaLINKMusicPlayer>>* instances =
                    [musicPlayerClass createInstancesWithDefaults:userDefaults];
            [musicPlayers addObjectsFromArray:instances];
        }
        
        _musicPlayers = [NSArray arrayWithArray:musicPlayers];
        
        // Set _selectedMusicPlayer to its setting from last time NovaLINKApp ran. (Unless this is the first run or
        // that music player isn't available this time.)
        [self initSelectedMusicPlayerFromUserDefaults];
        
        if (!_selectedMusicPlayer) {
            // Couldn't set _selectedMusicPlayer from user defaults, so try NovaLINKDevice's music player property.
            [self initSelectedMusicPlayerFromNovaLINKDevice];
        }
        
        if (!_selectedMusicPlayer) {
            // The user hasn't changed the music player yet, so we set the default music player as selected.
            [self setSelectedMusicPlayerByID:defaultMusicPlayerID];
        }
        
        NSAssert(_selectedMusicPlayer, @"NovaLINKMusicPlayers::initWithAudioDevices: !_selectedMusicPlayer");
    }
    
    return self;
}

- (void) initSelectedMusicPlayerFromUserDefaults {
    // Load the selected music player setting from user defaults.
    
    NSString* __nullable selectedMusicPlayerIDStr = userDefaults.selectedMusicPlayerID;
    NSUUID* __nullable selectedMusicPlayerID = nil;
    
    if (selectedMusicPlayerIDStr) {
        NSString* idStrNN = selectedMusicPlayerIDStr;
        selectedMusicPlayerID = [[NSUUID alloc] initWithUUIDString:idStrNN];
        
        NSAssert(selectedMusicPlayerID,
                 @"NovaLINKMusicPlayers::initSelectedMusicPlayerFromUserDefaults: !selectedMusicPlayerID");
    }
    
    if (selectedMusicPlayerID) {
        NSUUID* idNN = selectedMusicPlayerID;
        BOOL didChangeMusicPlayer = [self setSelectedMusicPlayerByID:idNN];
        
#if DEBUG
        DebugMsg("NovaLINKMusicPlayers::initSelectedMusicPlayerFromUserDefaults: %s selectedMusicPlayerIDStr=%s",
                 (didChangeMusicPlayer ?
                     "Selected music player restored from user defaults." :
                     "The selected music player setting found in user defaults didn't match an available music player."),
                 selectedMusicPlayerIDStr.UTF8String);
#else
        #pragma unused (didChangeMusicPlayer)
#endif
    }
}

- (void) initSelectedMusicPlayerFromNovaLINKDevice {
    // When the selected music player setting hasn't been stored in user defaults yet, we get the music player
    // bundle ID from the driver and look for the music player with that bundle ID. This is mainly done for
    // backwards compatibility.
    
    NSString* __nullable bundleID =
        (__bridge_transfer NSString* __nullable)[audioDevices novaLINKDevice].GetMusicPlayerBundleID();

    DebugMsg("NovaLINKMusicPlayers::initSelectedMusicPlayerFromNovaLINKDevice: "
             "Trying to set selected music player by bundle ID (from NovaLINKDriver). bundleID=%s",
             (bundleID ? bundleID.UTF8String : "(null)"));
    
    if (bundleID && ![bundleID isEqualToString:@""]) {
        // Find any music players with a bundle ID matching the one from NovaLINKDriver.
        NSArray<id<NovaLINKMusicPlayer>>* matchingMusicPlayers = @[ ];
        
        for (id<NovaLINKMusicPlayer> musicPlayer in _musicPlayers) {
            NSString* bundleIDNN = bundleID;
            if ([musicPlayer.bundleID isEqualToString:bundleIDNN]) {
                DebugMsg("NovaLINKMusicPlayers::initSelectedMusicPlayerFromNovaLINKDevice: Bundle ID on NovaLINKDevice matches %s",
                         musicPlayer.name.UTF8String);
                
                matchingMusicPlayers = [matchingMusicPlayers arrayByAddingObject:musicPlayer];
            }
        }
        
        // Currently, the music players all have different bundle IDs, but that might change at some point. We
        // might want to consider some websites as music players, for example. So we don't change the setting
        // unless the bundle ID only matches one music player.
        if (matchingMusicPlayers.count == 1) {
            // (Use setSelectedMusicPlayerImpl to avoid setSelectedMusicPlayer being called in init.)
            [self setSelectedMusicPlayerImpl:matchingMusicPlayers[0]];
        }
    }
}

- (id<NovaLINKMusicPlayer>) selectedMusicPlayer {
    return _selectedMusicPlayer;
}

- (void) setSelectedMusicPlayer:(id<NovaLINKMusicPlayer>)newSelectedMusicPlayer {
    // Apparently you shouldn't call properties' setter methods in init (KVO notifications might trigger, etc.)
    // so the actual work is done in setSelectedMusicPlayerImpl.
    [self setSelectedMusicPlayerImpl:newSelectedMusicPlayer];
    
    NSAssert(self.selectedMusicPlayer == newSelectedMusicPlayer,
             @"NovaLINKMusicPlayers::setSelectedMusicPlayer: selectedMusicPlayer wasn't set to the object expected");
}

- (BOOL) setSelectedMusicPlayerByID:(NSUUID*)newSelectedMusicPlayerID {
    id<NovaLINKMusicPlayer> __nullable newSelectedMusicPlayer = nil;
    
    // Find the music player with the given ID, if there is one.
    for (id<NovaLINKMusicPlayer> musicPlayer in _musicPlayers) {
        if ([musicPlayer.musicPlayerID isEqual:newSelectedMusicPlayerID]) {
            NSAssert(!newSelectedMusicPlayer, @"NovaLINKMusicPlayers::setSelectedMusicPlayerByID: Non-unique musicPlayerID");
            
            newSelectedMusicPlayer = musicPlayer;
        }
    }
    
    if (newSelectedMusicPlayer) {
        // (Use setSelectedMusicPlayerImpl to avoid setSelectedMusicPlayer being called in init.)
        id<NovaLINKMusicPlayer> newPlayerNN = newSelectedMusicPlayer;
        [self setSelectedMusicPlayerImpl:newPlayerNN];
        
        return YES;
    } else {
        return NO;
    }
}

- (void) setSelectedMusicPlayerImpl:(id<NovaLINKMusicPlayer>)newSelectedMusicPlayer {
    NSAssert([_musicPlayers containsObject:newSelectedMusicPlayer],
             @"NovaLINKMusicPlayers::setSelectedMusicPlayerImpl: Only the music players in the musicPlayers array can be selected. "
              "newSelectedMusicPlayer=%@",
             newSelectedMusicPlayer.name);

    if (_selectedMusicPlayer == newSelectedMusicPlayer) {
        DebugMsg("NovaLINKMusicPlayers::setSelectedMusicPlayerImpl: %s is already the selected music "
                 "player.",
                 _selectedMusicPlayer.name.UTF8String);
        return;
    }

    // Tell the current music player (object) a different player has been selected.
    [_selectedMusicPlayer wasDeselected];
    
    _selectedMusicPlayer = newSelectedMusicPlayer;
    
    DebugMsg("NovaLINKMusicPlayers::setSelectedMusicPlayerImpl: Set selected music player to %s",
             _selectedMusicPlayer.name.UTF8String);
    
    // Update the selected music player on the driver.
    [self updateNovaLINKDeviceMusicPlayerProperties];
    
    // Save the new setting in user defaults.
    userDefaults.selectedMusicPlayerID = _selectedMusicPlayer.musicPlayerID.UUIDString;

    // Tell the music player (object) it's been selected.
    [_selectedMusicPlayer wasSelected];
}

- (void) updateNovaLINKDeviceMusicPlayerProperties {
    // Send the music player's PID and/or bundle ID to the driver.
    
    NSAssert(self.selectedMusicPlayer.pid || self.selectedMusicPlayer.bundleID,
             @"NovaLINKMusicPlayers::updateNovaLINKDeviceMusicPlayerProperties: Music player has neither bundle ID nor PID");

    if (self.selectedMusicPlayer.pid) {
        [audioDevices novaLINKDevice].SetMusicPlayerProcessID((__bridge CFNumberRef)self.selectedMusicPlayer.pid);
    }
    
    if (self.selectedMusicPlayer.bundleID) {
        [audioDevices novaLINKDevice].SetMusicPlayerBundleID((__bridge CFStringRef)self.selectedMusicPlayer.bundleID);
    }
}

@end

#pragma clang assume_nonnull end

