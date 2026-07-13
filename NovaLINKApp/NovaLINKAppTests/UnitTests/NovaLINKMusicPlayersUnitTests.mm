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
//  NovaLINKMusicPlayersUnitTests.mm
//  NovaLINKAppUnitTests
//
//  Copyright © 2016-2020 Kyle Neideck
//

// Unit include
#import "NovaLINKMusicPlayers.h"

// NovaLINK includes
#import "NovaLINK_Types.h"
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKiTunes.h"
#import "NovaLINKDecibel.h"
#import "NovaLINKSpotify.h"
#import "NovaLINKVLC.h"

// Local includes
#import "NovaLINK_TestUtils.h"
#import "MockAudioObject.h"
#import "MockAudioObjects.h"

// System includes
#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>


// Note that the PublicUtility classes that we use to communicate with the HAL, CAHALAudioObject and
// CAHALAudioSystemObject, are also mocked. The unit tests are compiled with mock implementations:
// Mock_CAHALAudioObject.cpp and Mock_CAHALAudioSystemObject.cpp.

@interface NovaLINKMockUserDefaults : NovaLINKUserDefaults

@property NSUUID* selectedPlayerID;

@end

@implementation NovaLINKMockUserDefaults

- (void) registerDefaults {
}

- (NSString* __nullable) selectedMusicPlayerID {
    return [self.selectedPlayerID UUIDString];
}

- (void) setSelectedMusicPlayerID:(NSString* __nullable)selectedMusicPlayerID {
    #pragma unused (selectedMusicPlayerID)
}

- (BOOL) autoPauseMusicEnabled {
    return YES;
}

- (void) setAutoPauseMusicEnabled:(BOOL)autoPauseMusicEnabled {
    #pragma unused (autoPauseMusicEnabled)
}

@end

// -------------------------------------------------------------------------------------------------

@interface NovaLINKMockAudioDeviceManager : NovaLINKAudioDeviceManager
@end

@implementation NovaLINKMockAudioDeviceManager {
    NovaLINKDevice novaLINKDevice;
}

- (NovaLINKDevice) novaLINKDevice {
    return novaLINKDevice;
}

@end

// -------------------------------------------------------------------------------------------------

@interface NovaLINKMusicPlayersUnitTests : XCTestCase
@end

@implementation NovaLINKMusicPlayersUnitTests {
    NovaLINKAudioDeviceManager* devices;
    NovaLINKMockUserDefaults* defaults;
    
    NSUUID* spotifyID;
    NSUUID* vlcID;
}

- (void) setUp {
    [super setUp];

    // Mock NovaLINKDevice.
    MockAudioObjects::CreateMockDevice(kNovaLINKDeviceUID);
    MockAudioObjects::CreateMockDevice(kNovaLINKDeviceUID_UISounds);

    devices = [NovaLINKMockAudioDeviceManager new];
    defaults = [NovaLINKMockUserDefaults new];
    
    // These are the IDs hardcoded in NovaLINKSpotify and NovaLINKVLC.
    spotifyID = [[NSUUID alloc] initWithUUIDString:@"EC2A907F-8515-4687-9570-1BF63176E6D8"];
    vlcID = [[NSUUID alloc] initWithUUIDString:@"5226F4B9-C740-4045-A273-4B8EABC0E8FC"];
}

- (void) tearDown {
    [super tearDown];
    MockAudioObjects::DestroyMocks();
}

- (void) testNoSelectedMusicPlayerStored_iTunesDefault {
    // Test the case where the user has never changed the music player preference.
    
    // Test with iTunes as the default.
    NovaLINKMusicPlayers* players = [[NovaLINKMusicPlayers alloc] initWithAudioDevices:devices
                                                        defaultMusicPlayerID:[NovaLINKiTunes sharedMusicPlayerID]
                                                          musicPlayerClasses:@[ NovaLINKiTunes.class, NovaLINKVLC.class ]
                                                                userDefaults:defaults];
    
    XCTAssertEqual(players.musicPlayers.count, 2);
    
    for (id<NovaLINKMusicPlayer> player in players.musicPlayers) {
        XCTAssertTrue([player isKindOfClass:NovaLINKiTunes.class] || [player isKindOfClass:NovaLINKVLC.class]);
    }
    
    XCTAssertEqualObjects(players.selectedMusicPlayer.musicPlayerID, [NovaLINKiTunes sharedMusicPlayerID]);
    XCTAssertEqualObjects(players.selectedMusicPlayer.name, @"iTunes");
}


- (void) testNoSelectedMusicPlayerStored_vlcDefault {
    // Test the case where the user has never changed the music player preference.

    // Test with VLC as the default.
    NovaLINKMusicPlayers* players =
            [[NovaLINKMusicPlayers alloc] initWithAudioDevices:devices
                                     defaultMusicPlayerID:vlcID
                                       musicPlayerClasses:@[ NovaLINKiTunes.class,
                                                             NovaLINKVLC.class,
                                                             NovaLINKDecibel.class ]
                                             userDefaults:defaults];
    
    XCTAssertEqual(players.musicPlayers.count, 3);
    
    for (id<NovaLINKMusicPlayer> player in players.musicPlayers) {
        XCTAssertTrue([player isKindOfClass:NovaLINKiTunes.class] ||
                      [player isKindOfClass:NovaLINKVLC.class] ||
                      [player isKindOfClass:NovaLINKDecibel.class]);
    }
    
    XCTAssertEqualObjects(players.selectedMusicPlayer.musicPlayerID, vlcID);
    XCTAssertEqualObjects(players.selectedMusicPlayer.name, @"VLC");
}

- (void) testSelectedMusicPlayerInUserDefaults {
    defaults.selectedPlayerID = spotifyID;
    
    NovaLINKMusicPlayers* players = [[NovaLINKMusicPlayers alloc] initWithAudioDevices:devices
                                                        defaultMusicPlayerID:[NovaLINKiTunes sharedMusicPlayerID]
                                                          musicPlayerClasses:@[ NovaLINKiTunes.class,
                                                                                NovaLINKVLC.class,
                                                                                NovaLINKSpotify.class ]
                                                                userDefaults:defaults];
    
    XCTAssertEqual(players.musicPlayers.count, 3);
    
    XCTAssertEqualObjects(players.selectedMusicPlayer.musicPlayerID, spotifyID);
    XCTAssertEqualObjects(players.selectedMusicPlayer.name, @"Spotify");
}

- (void) testUnrecognizedSelectedMusicPlayerInUserDefaults {
    // If there's an unrecognized ID in user defaults, the default music player should be selected.
    defaults.selectedPlayerID = [[NSUUID alloc] initWithUUIDString:@"11111111-1111-1111-0000-000000000000"];
    
    // This initializer sets iTunes as the default music player and adds all the other music players.
    NovaLINKMusicPlayers* players = [[NovaLINKMusicPlayers alloc] initWithAudioDevices:devices
                                                                userDefaults:defaults];
    
    XCTAssert(players.musicPlayers.count >= 6);
    
    XCTAssertEqualObjects(players.selectedMusicPlayer.musicPlayerID, [NovaLINKiTunes sharedMusicPlayerID]);
    XCTAssertEqualObjects(players.selectedMusicPlayer.name, @"iTunes");
}

- (void) testSelectedMusicPlayerInNovaLINKDeviceProperties {
    // When it doesn't find a selected music player in user defaults, it should check NovaLINKDevice's music
    // player properties.
    
    [devices novaLINKDevice].SetMusicPlayerBundleID(CFSTR("org.videolan.vlc"));
    
    NovaLINKMusicPlayers* players = [[NovaLINKMusicPlayers alloc] initWithAudioDevices:devices
                                                                userDefaults:defaults];
    
    XCTAssert(players.musicPlayers.count >= 6);
    
    XCTAssertEqualObjects(players.selectedMusicPlayer.musicPlayerID, vlcID);
    XCTAssertEqualObjects(players.selectedMusicPlayer.name, @"VLC");
}

// TODO: Test setting the selectedMusicPlayer property

@end

