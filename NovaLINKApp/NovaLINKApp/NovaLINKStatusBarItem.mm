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
//  NovaLINKStatusBarItem.m
//  NovaLINKApp
//
//  Copyright © 2019, 2020 Kyle Neideck
//

// Self Include
#import "NovaLINKStatusBarItem.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINKUserDefaults.h"
#import "NovaLINKVolumeChangeListener.h"


#pragma clang assume_nonnull begin

static CGFloat const kStatusBarIconPadding                = 0.25;
static CGFloat const kVolumeIconAdditionalVerticalPadding = 0.075;

@implementation NovaLINKStatusBarItem
{
    NovaLINKAudioDeviceManager* audioDevices;

    // User settings and data.
    NovaLINKUserDefaults* userDefaults;

    NSImage* fermataIcon;
    NSImage* volumeIcon0SoundWaves;
    NSImage* volumeIcon1SoundWave;
    NSImage* volumeIcon2SoundWaves;
    NSImage* volumeIcon3SoundWaves;

    NSStatusItem* statusBarItem;
    NSMenu* companionMenu;
    NovaLINKDebugLoggingMenuItem* debugLoggingMenuItem;

    NovaLINKVolumeChangeListener* volumeChangeListener;
    id __nullable clickEventHandler;
    id __nullable workspaceWakeObserver;

    NovaLINKStatusBarIcon _icon;
}

#pragma mark Initialisation

- (instancetype) initWithMenu:(NSMenu*)novaLINKMenu
                 audioDevices:(NovaLINKAudioDeviceManager*)devices
                 userDefaults:(NovaLINKUserDefaults*)defaults {
    if ((self = [super init])) {
        companionMenu = novaLINKMenu;
        audioDevices = devices;
        userDefaults = defaults;

        [self createStatusItem];

        // Initialise the icons.
        [self initIcons];

        // Set the initial icon.
        self.icon = userDefaults.statusBarIcon;

        // Monitor click events so we can show extra options in the menu if the user was holding the
        // option key. Only needed on OS X 10.9, where NSStatusItem has no button/action.
        if (![NovaLINKStatusBarItem buttonAvailable]) {
            clickEventHandler = [self addClickMonitor];
        }

        NovaLINKStatusBarItem* __weak weakSelf = self;
        workspaceWakeObserver =
            [[[NSWorkspace sharedWorkspace] notificationCenter]
                addObserverForName:NSWorkspaceDidWakeNotification
                            object:nil
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification* notification) {
                            #pragma unused (notification)
                            [weakSelf handleWorkspaceDidWake];
                        }];

        // Update the icon when NovaLINKDevice's volume changes.
        volumeChangeListener = new NovaLINKVolumeChangeListener(audioDevices.novaLINKDevice, [=] {
            [weakSelf novaLINKDeviceVolumeDidChange];
        });
    }

    return self;
}

- (id __nullable) addClickMonitor {
    NSEvent* __nullable (^handlerBlock)(NSEvent*) =
        ^NSEvent* __nullable (NSEvent* event) {
            [self statusBarItemWasClicked:event];
            return event;
        };

    // TODO: I doubt this works well with VoiceOver.
    return [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown
                                                 handler:handlerBlock];
}

- (void) dealloc {
    delete volumeChangeListener;

    if (clickEventHandler) {
        [NSEvent removeMonitor:(id)clickEventHandler];
        clickEventHandler = nil;
    }

    if (workspaceWakeObserver) {
        id observer = workspaceWakeObserver;
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:observer];
        workspaceWakeObserver = nil;
    }

    if (statusBarItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:statusBarItem];
        statusBarItem = nil;
    }
}

- (void) createStatusItem {
    statusBarItem =
            [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    if (@available(macOS 10.12, *)) {
        statusBarItem.visible = YES;
    }

    // Assigning statusBarItem.menu and letting AppKit auto-track clicks works at launch, but
    // after long uptime (and especially after sleep) the extra can stop opening while the
    // process is otherwise healthy. Drive the menu from the button action instead.
    if ([NovaLINKStatusBarItem buttonAvailable]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        statusBarItem.button.target = self;
        statusBarItem.button.action = @selector(statusBarButtonClicked:);
        [statusBarItem.button sendActionOn:NSEventMaskLeftMouseDown];
        statusBarItem.button.accessibilityLabel =
                [NSRunningApplication currentApplication].localizedName;
#pragma clang diagnostic pop
    } else {
        statusBarItem.menu = companionMenu;
    }
}

- (void) handleWorkspaceDidWake {
    // NSStatusItem click tracking often dies across sleep. Recreate the extra.
    if (statusBarItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:statusBarItem];
        statusBarItem = nil;
    }
    [self createStatusItem];
    [self applyIconSizes];
    NovaLINKStatusBarIcon current = _icon;
    self.icon = current;
}

- (void) statusBarButtonClicked:(id)sender {
    #pragma unused (sender)

    BOOL optionDown = ([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0;
    [debugLoggingMenuItem setMenuShowingExtraOptions:optionDown];

    if (!companionMenu) {
        return;
    }

    // Let the delegate fill in Auto-pause / device checkmarks before the first paint.
    [companionMenu update];

    // popUpMenuPositioningItem:nil is a contextual popup: recent macOS anchors it on the
    // checked item, so rows above that (Auto-pause) are clipped by the menu bar until the
    // mouse moves and AppKit reflows. popUpStatusItemMenu always hangs the full menu
    // under the extra.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    [statusBarItem popUpStatusItemMenu:companionMenu];
#pragma clang diagnostic pop
}

- (void) initIcons {
    // Load the icons.
    fermataIcon = [NSImage imageNamed:@"FermataIcon"];
    if (@available(macOS 11.0, *)) {
        volumeIcon0SoundWaves = [NSImage imageWithSystemSymbolName:@"speaker.fill" accessibilityDescription:nil];
        volumeIcon1SoundWave = [NSImage imageWithSystemSymbolName:@"speaker.wave.1.fill" accessibilityDescription:nil];
        volumeIcon2SoundWaves = [NSImage imageWithSystemSymbolName:@"speaker.wave.2.fill" accessibilityDescription:nil];
        volumeIcon3SoundWaves = [NSImage imageWithSystemSymbolName:@"speaker.wave.3.fill" accessibilityDescription:nil];
    } else {
        volumeIcon0SoundWaves = [NSImage imageNamed:@"Volume0"];
        volumeIcon1SoundWave = [NSImage imageNamed:@"Volume1"];
        volumeIcon2SoundWaves = [NSImage imageNamed:@"Volume2"];
        volumeIcon3SoundWaves = [NSImage imageNamed:@"Volume3"];
    }

    [self applyIconSizes];

    // Make the icons "template images" so they get drawn colour-inverted when they're highlighted
    // or the system is in dark mode.
    [fermataIcon setTemplate:YES];
    [volumeIcon0SoundWaves setTemplate:YES];
    [volumeIcon1SoundWave setTemplate:YES];
    [volumeIcon2SoundWaves setTemplate:YES];
    [volumeIcon3SoundWaves setTemplate:YES];
}

- (CGFloat) statusBarIconHeight {
    // LaunchAgent awakeFromNib often runs before the status item has a real button frame
    // (height 0). Sizing icons to 0 makes the companion menu-bar extra invisible even though
    // the NSStatusItem exists — user only sees the system orange mic indicator.
    NSRect statusBarItemFrame = NSZeroRect;
    if ([NovaLINKStatusBarItem buttonAvailable]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        statusBarItemFrame = statusBarItem.button.frame;
#pragma clang diagnostic pop
    } else {
        statusBarItemFrame = statusBarItem.view.frame;
    }

    CGFloat height = statusBarItemFrame.size.height;
    if (height < 1.0) {
        height = [NSStatusBar systemStatusBar].thickness;
    }
    if (height < 1.0) {
        height = 22.0;
    }
    return height * (1.0 - kStatusBarIconPadding);
}

- (void) applyIconSizes {
    CGFloat heightMinusPadding = [self statusBarIconHeight];

    if (fermataIcon) {
        [fermataIcon setSize:NSMakeSize(heightMinusPadding, heightMinusPadding)];
    }

    if (!volumeIcon0SoundWaves || volumeIcon0SoundWaves.size.height < 0.5) {
        return;
    }

    CGFloat volumeIconWidthToHeightRatio =
            volumeIcon0SoundWaves.size.width / volumeIcon0SoundWaves.size.height;
    CGFloat volumeIconWidth = heightMinusPadding * volumeIconWidthToHeightRatio;
    CGFloat volumeIconHeight = heightMinusPadding * (1 - kVolumeIconAdditionalVerticalPadding);

    [volumeIcon0SoundWaves setSize:NSMakeSize(volumeIconWidth, volumeIconHeight)];
    [volumeIcon1SoundWave setSize:NSMakeSize(volumeIconWidth, volumeIconHeight)];
    [volumeIcon2SoundWaves setSize:NSMakeSize(volumeIconWidth, volumeIconHeight)];
    [volumeIcon3SoundWaves setSize:NSMakeSize(volumeIconWidth, volumeIconHeight)];
}

- (void) ensureVisible {
    if (@available(macOS 10.12, *)) {
        statusBarItem.visible = YES;
    }
    [self applyIconSizes];
    // Re-assign image so AppKit picks up non-zero sizes after the button laid out.
    NovaLINKStatusBarIcon current = _icon;
    self.icon = current;
}

#pragma mark Accessors

+ (BOOL) buttonAvailable {
    // NSStatusItem doesn't have the "button" property on OS X 10.9.
    return (floor(NSAppKitVersionNumber) >= NSAppKitVersionNumber10_10);
}

- (void) setImage:(NSImage*)image {
    if ([NovaLINKStatusBarItem buttonAvailable]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        statusBarItem.button.image = image;
#pragma clang diagnostic pop
    } else {
        statusBarItem.image = image;
    }
}

- (NovaLINKStatusBarIcon) icon {
    return _icon;
}

- (void) setIcon:(NovaLINKStatusBarIcon)icon {
    _icon = icon;

    // Save the setting.
    userDefaults.statusBarIcon = self.icon;

    // Change the icon (i.e. the image). Dispatch this to the main thread because it changes the UI.
    dispatch_async(dispatch_get_main_queue(), ^{
        if (_icon == NovaLINKFermataStatusBarIcon) {
            [self setImage:fermataIcon];

            // If the icon was greyed out, change it back.
            if ([NovaLINKStatusBarItem buttonAvailable]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
                statusBarItem.button.appearsDisabled = NO;
#pragma clang diagnostic pop
            }
        } else {
            NovaLINKAssert((_icon == NovaLINKVolumeStatusBarIcon), "Unknown icon in enum");

            [self updateVolumeStatusBarIcon];
        }
    });
}

#pragma mark Volume Icon

- (void) novaLINKDeviceVolumeDidChange {
    if (self.icon != NovaLINKVolumeStatusBarIcon) {
        return;
    }

    // HAL reads stay off the main thread so a slow/stuck device cannot freeze the menu extra.
    if ([NSThread isMainThread]) {
        dispatch_async(NovaLINKGetDispatchQueue_PriorityUserInteractive(), ^{
            [self novaLINKDeviceVolumeDidChange];
        });
        return;
    }

    BOOL hasVolume = NO;
    BOOL muted = NO;
    double volume = 0.0;

    NovaLINKAudioDevice novaLINKDevice = [audioDevices novaLINKDevice];
    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        AudioObjectPropertyScope scope = kAudioObjectPropertyScopeOutput;
        AudioObjectPropertyScope element = kAudioObjectPropertyElementMaster;

        hasVolume = novaLINKDevice.HasVolumeControl(scope, element);
        if (hasVolume && novaLINKDevice.HasMuteControl(scope, element)) {
            muted = novaLINKDevice.GetMuteControlValue(scope, element);
        }
        if (hasVolume) {
            volume = novaLINKDevice.GetVolumeControlScalarValue(scope, element);
        }
    });

    BOOL hasVolumeUI = hasVolume;
    BOOL mutedUI = muted;
    double volumeUI = volume;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self applyVolumeStatusBarIconHasVolume:hasVolumeUI muted:mutedUI volume:volumeUI];
    });
}

- (void) updateVolumeStatusBarIcon {
    [self novaLINKDeviceVolumeDidChange];
}

- (void) applyVolumeStatusBarIconHasVolume:(BOOL)hasVolume
                                     muted:(BOOL)muted
                                    volume:(double)volume {
    NovaLINKAssert([[NSThread currentThread] isMainThread],
              "applyVolumeStatusBarIconHasVolume called on non-main thread.");

    if (self.icon != NovaLINKVolumeStatusBarIcon) {
        return;
    }

    if ([NovaLINKStatusBarItem buttonAvailable]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        statusBarItem.button.appearsDisabled = !hasVolume;
#pragma clang diagnostic pop
    }

    if (hasVolume) {
        if (muted) {
            [self setImage:volumeIcon0SoundWaves];
        } else if (volume == 0.05) {
            [self setImage:volumeIcon0SoundWaves];
        } else if (volume < 0.33) {
            [self setImage:volumeIcon1SoundWave];
        } else if (volume < 0.66) {
            [self setImage:volumeIcon2SoundWaves];
        } else {
            [self setImage:volumeIcon3SoundWaves];
        }
    } else {
        [self setImage:volumeIcon3SoundWaves];
    }

    DebugMsg("NovaLINKStatusBarItem::applyVolumeStatusBarIconHasVolume: Set icon to %s",
             statusBarItem.image.name.UTF8String);
}

#pragma mark Debug Logging Menu Item

- (void) statusBarItemWasClicked:(NSEvent* __nonnull)event {
    if ((event.modifierFlags & NSEventModifierFlagOption) != 0) {
        DebugMsg("NovaLINKStatusBarItem::statusBarItemWasClicked: Option key held");
        [debugLoggingMenuItem setMenuShowingExtraOptions:YES];
    } else {
        [debugLoggingMenuItem setMenuShowingExtraOptions:NO];
    }
}

- (void) setDebugLoggingMenuItem:(NovaLINKDebugLoggingMenuItem*)menuItem {
    debugLoggingMenuItem = menuItem;
}

@end

#pragma clang assume_nonnull end

