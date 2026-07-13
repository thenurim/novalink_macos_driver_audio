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
//  NovaLINKAppUITests.mm
//  NovaLINKAppUITests
//
//  Copyright © 2017, 2018, 2020, 2022 Kyle Neideck
//
//  You might want to use Xcode's UI test recording feature if you add new tests.
//

// Local Includes
#import "NovaLINK_TestUtils.h"

// Scripting Bridge Includes
#import "NovaLINKApp.h"

// System Includes
#import <XCTest/XCTest.h>


// TODO: Mock NovaLINKDevice and music players.

#if __clang_major__ >= 9

@interface NovaLINKAppUITests : XCTestCase
@end

@implementation NovaLINKAppUITests {
    // The NovaLINKApp instance.
    XCUIApplication* app;

    // Convenience vars.
    //
    // The menu bar icon. (Called the status bar icon in some places.)
    XCUIElement* icon;
    // The menu items in the main menu.
    XCUIElementQuery* menuItems;
    // The Preferences menu item.
    XCUIElement* prefs;
}

- (void) setUp {
    [super setUp];
    
    // In UI tests it is usually best to stop immediately when a failure occurs.
    self.continueAfterFailure = NO;

    // Set up the app object and some convenience vars.
    app = [[XCUIApplication alloc] init];
    menuItems = app.menuBars.menuItems;
    prefs = menuItems[@"Preferences"];
    icon = [app.menuBars childrenMatchingType:XCUIElementTypeStatusItem].element;

    // TODO: Make sure NovaLINKDevice isn't set as the OS X default device before launching NovaLINKApp.

    // Tell NovaLINKApp not to load/store user defaults (settings) and to use
    // NSApplicationActivationPolicyRegular. If it used the "accessory" policy as usual, the tests
    // would fail to start because of a bug in Xcode.
    app.launchArguments = @[ @"--no-persistent-data", @"--show-dock-icon" ];

    // Make the "NovaLINK wants to use the microphone" dialog appear every time so the test
    // doesn't need logic to handle both cases.
    // TODO: Commented out until acceptMicrophoneAuthorizationDialog work again. See below.
    // if (@available(macOS 10.15.4, *)) {
    //     [app resetAuthorizationStatusForResource:XCUIProtectedResourceMicrophone];
    // }

    // Launch NovaLINKApp.
    [app launch];

    // TODO: This doesn't seem to be working on macOS 12.4 (21F79). You can click OK manually for
    //       now.
    // [self acceptMicrophoneAuthorizationDialog];

    if (![icon waitForExistenceWithTimeout:20.0]) {
        // The status bar icon/button has this type when using older versions of XCTest, so try
        // both. (Actually, it might depend on the macOS or Xcode version. I'm not sure.)
        XCUIElement* iconOldType =
            [app.menuBars childrenMatchingType:XCUIElementTypeMenuBarItem].element;
        if ([iconOldType waitForExistenceWithTimeout:20.0]) {
            NSLog(@"icon = iconOldType");
            icon = iconOldType;
        }
    }

    // Wait for the initial elements.
    XCTAssert([app waitForExistenceWithTimeout:20.0]);
    XCTAssert([icon waitForExistenceWithTimeout:20.0]);
}

// Clicks the OK button in the "NovaLINK wants to use the microphone" dialog.
- (void) acceptMicrophoneAuthorizationDialog {
    XCUIApplication* unc =
        [[XCUIApplication alloc] initWithBundleIdentifier:@"com.apple.UserNotificationCenter"];
    NSLog(@"UserNotificationCenter: %@", unc);
    XCUIElement* okButton = unc.dialogs.buttons[@"OK"];

    XCTAssert([okButton waitForExistenceWithTimeout:20.0]);

    // This click is failing on GH Actions. No idea why, so try a sleep.
    (void)[XCTWaiter waitForExpectations:@[[XCTestExpectation new]] timeout:5.0];
    [okButton click];

    int retries = 10;
    while (retries > 0 && [okButton waitForExistenceWithTimeout:3.0]) {
        NSLog(@"Microphone authorization dialog is still open. Trying to click OK again.");
        [okButton click];
        retries--;
    }
}

- (void) tearDown {
    // Click the quit menu item.
    if (!menuItems.count) {
        [icon click];
    }

    [menuItems[@"Quit NovaLINK Audio Passthrough"] click];

    // NovaLINKApp should quit.
    XCTAssertTrue([app waitForState:XCUIApplicationStateNotRunning timeout:10.0]);
    
    [super tearDown];
}

- (void) testCycleOutputDevices {
    const int NUM_CYCLES = 1;

    // sbApp lets us use AppleScript to query NovaLINKApp and check the test has made the changes to its
    // settings we expect.
    NovaLINKAppApplication* sbApp = [SBApplication applicationWithBundleIdentifier:@kNovaLINKAppBundleID];

    // Get macOS to show the "'Xcode' wants to control 'NovaLINK'" dialog before we start
    // the test so it doesn't interrupt it.
    [[sbApp selectedOutputDevice] name];

    // Click the icon to open the main menu.
    [icon click];

    // Get the list of output devices from the main menu.
    // NovaLINKOutputDeviceMenuSection::createMenuItemForDevice gives every output device menu item the
    // accessibility identifier "output-device" so we can find all of them here.
    NSArray<XCUIElement*>* outputDeviceMenuItems =
        [menuItems matchingIdentifier:@"output-device"].allElementsBoundByIndex;

    // For debugging certain issues, it can be useful to repeatedly switch between two
    // devices:
    // outputDeviceMenuItems = [outputDeviceMenuItems subarrayWithRange:NSMakeRange(0,2)];

    XCTAssertGreaterThan(outputDeviceMenuItems.count, 0);

    // Click the last device to close the menu again.
    [outputDeviceMenuItems.lastObject click];

    for (int i = 0; i < NUM_CYCLES; i++) {
        // Select each output device.
        for (XCUIElement* item in outputDeviceMenuItems) {
            [icon click];
            [item click];

            // Assert that the device we clicked is the selected device now.
            for (NovaLINKAppOutputDevice* device in [sbApp outputDevices]) {
                // TODO: This seems a bit fragile. Would it still work with long device names?
                if ([device.name isEqualToString:[item title]]) {
                    XCTAssert(device.selected);
                } else {
                    XCTAssertFalse(device.selected);
                }
            }
        }
    }
}

- (void) testSelectMusicPlayer {
    // Select VLC as the music player.
    [icon click];
    [prefs hover];
    [prefs.menuItems[@"VLC"] click];

    // The name of the Auto-pause menu item should change. Also check the accessibility identifier.
    [icon click];
    XCTAssertEqualObjects(menuItems[@"Auto-pause VLC"].identifier, @"Auto-pause enabled");
    
    // Select iTunes as the music player.
    [prefs hover];
    [prefs.menuItems[@"iTunes"] click];

    // The name of the Auto-pause menu item should change back.
    [icon click];
    XCTAssert(menuItems[@"Auto-pause iTunes"].exists);
}

@end

#endif /* __clang_major__ >= 9 */

