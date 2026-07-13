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
//  NovaLINKPreferencesMenu.mm
//  NovaLINKApp
//
//  Copyright © 2016, 2018, 2019 Kyle Neideck
//

// Self Include
#import "NovaLINKPreferencesMenu.h"

// Local Includes
#import "NovaLINKAutoPauseMusicPrefs.h"
#import "NovaLINKAboutPanel.h"


NS_ASSUME_NONNULL_BEGIN

// Interface Builder tags
static NSInteger const kPreferencesMenuItemTag = 1;
static NSInteger const kNovaLINKIconMenuItemTag     = 2;
static NSInteger const kVolumeIconMenuItemTag  = 3;
static NSInteger const kAboutPanelMenuItemTag  = 4;

@implementation NovaLINKPreferencesMenu {
    // Menu sections/items
    NovaLINKAutoPauseMusicPrefs* autoPauseMusicPrefs;
    NSMenuItem* novaLINKIconMenuItem;
    NSMenuItem* volumeIconMenuItem;

    // The menu item you press to open NovaLINKApp's main menu.
    NovaLINKStatusBarItem* statusBarItem;

    // The About NovaLINK window
    NovaLINKAboutPanel* aboutPanel;
}

- (id) initWithNovaLINKMenu:(NSMenu*)inNovaLINKMenu
          audioDevices:(NovaLINKAudioDeviceManager*)inAudioDevices
          musicPlayers:(NovaLINKMusicPlayers*)inMusicPlayers
         statusBarItem:(NovaLINKStatusBarItem*)inStatusBarItem
            aboutPanel:(NSPanel*)inAboutPanel
 aboutPanelLicenseView:(NSTextView*)inAboutPanelLicenseView {
    if ((self = [super init])) {
        NSMenu* prefsMenu = [[inNovaLINKMenu itemWithTag:kPreferencesMenuItemTag] submenu];
        
        autoPauseMusicPrefs = [[NovaLINKAutoPauseMusicPrefs alloc] initWithPreferencesMenu:prefsMenu
                                                                         audioDevices:inAudioDevices
                                                                         musicPlayers:inMusicPlayers];
        
        aboutPanel = [[NovaLINKAboutPanel alloc] initWithPanel:inAboutPanel licenseView:inAboutPanelLicenseView];

        statusBarItem = inStatusBarItem;

        // Set up the menu items under the "Status Bar Icon" heading.
        novaLINKIconMenuItem = [prefsMenu itemWithTag:kNovaLINKIconMenuItemTag];
        novaLINKIconMenuItem.state =
                (statusBarItem.icon == NovaLINKFermataStatusBarIcon) ? NSOnState : NSOffState;
        [novaLINKIconMenuItem setTarget:self];
        [novaLINKIconMenuItem setAction:@selector(useNovaLINKStatusBarIcon)];

        volumeIconMenuItem = [prefsMenu itemWithTag:kVolumeIconMenuItemTag];
        volumeIconMenuItem.state =
                (statusBarItem.icon == NovaLINKVolumeStatusBarIcon) ? NSOnState : NSOffState;
        [volumeIconMenuItem setTarget:self];
        [volumeIconMenuItem setAction:@selector(useVolumeStatusBarIcon)];

        // Set up the "About NovaLINK Audio Passthrough" menu item
        NSMenuItem* aboutMenuItem = [prefsMenu itemWithTag:kAboutPanelMenuItemTag];
        [aboutMenuItem setTarget:aboutPanel];
        [aboutMenuItem setAction:@selector(show)];
    }
    
    return self;
}

- (void) useNovaLINKStatusBarIcon {
    // Change the icon.
    statusBarItem.icon = NovaLINKFermataStatusBarIcon;

    // Select/deselect the menu items.
    novaLINKIconMenuItem.state = NSOnState;
    volumeIconMenuItem.state = NSOffState;
}

- (void) useVolumeStatusBarIcon {
    // TODO: Maybe we should show a message that tells the user how to hide the built-in volume
    //       icon. They probably won't want two status bar items that look the same. Or we might be
    //       able to automatically hide the built-in icon while NovaLINKApp is running and show it again
    //       when NovaLINKApp is closed.

    // Change the icon.
    statusBarItem.icon = NovaLINKVolumeStatusBarIcon;

    // Select/deselect the menu items.
    novaLINKIconMenuItem.state = NSOffState;
    volumeIconMenuItem.state = NSOnState;
}

@end

NS_ASSUME_NONNULL_END

