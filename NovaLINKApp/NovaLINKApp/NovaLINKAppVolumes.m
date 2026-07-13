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
//  NovaLINKAppVolumes.m
//  NovaLINKApp
//
//  Copyright © 2016-2020 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//  Copyright © 2021 Marcus Wu
//  Copyright © 2022 Jon Egan
//

// Self Include
#import "NovaLINKAppVolumes.h"

// Local Includes
#import "NovaLINK_Types.h"
#import "NovaLINK_Utils.h"
#import "NovaLINKAppDelegate.h"

// PublicUtility Includes
#import "CADebugMacros.h"


static float const     kSlidersSnapWithin          = 5;
static CGFloat const   kAppVolumeViewInitialHeight = 20;
static NSString* const kMoreAppsMenuTitle          = @"More Apps";

@implementation NovaLINKAppVolumes {
    NovaLINKAppVolumesController* controller;

    NSMenu* novaLINKMenu;
    NSMenu* moreAppsMenu;
    
    NSView* appVolumeView;
    CGFloat appVolumeViewFullHeight;

    // The number of menu items this class has added to novaLINKMenu. Doesn't include the More Apps menu.
    NSInteger numMenuItems;
}

- (id) initWithController:(NovaLINKAppVolumesController*)inController
                  novaLINKMenu:(NSMenu*)inMenu
            appVolumeView:(NSView*)inView {
    if ((self = [super init])) {
        controller = inController;
        novaLINKMenu = inMenu;
        moreAppsMenu = [[NSMenu alloc] initWithTitle:kMoreAppsMenuTitle];
        appVolumeView = inView;
        appVolumeViewFullHeight = appVolumeView.frame.size.height;
        numMenuItems = 0;

        // Add the More Apps menu to the main menu.
        NSMenuItem* moreAppsMenuItem =
            [[NSMenuItem alloc] initWithTitle:kMoreAppsMenuTitle action:nil keyEquivalent:@""];
        moreAppsMenuItem.submenu = moreAppsMenu;

        [novaLINKMenu insertItem:moreAppsMenuItem atIndex:([self lastMenuItemIndex] + 1)];
        numMenuItems++;

        // Put an empty menu item above the More Apps menu item to fix its top margin.
        NSMenuItem* spacer = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
        spacer.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 0, 4)];
        spacer.hidden = YES;  // Tells accessibility clients to ignore this menu item.

        [novaLINKMenu insertItem:spacer atIndex:[self lastMenuItemIndex]];
        numMenuItems++;
    }
    
    return self;
}

#pragma mark UI Modifications

- (void) insertMenuItemForApp:(NSRunningApplication*)app
                initialVolume:(int)volume
                   initialPan:(int)pan {
    NSMenuItem* appVolItem = [self createBlankAppVolumeMenuItem];

    // Look through the menu item's subviews for the ones we want to set up
    for (NSView* subview in appVolItem.view.subviews) {
        if ([subview conformsToProtocol:@protocol(NovaLINKAppVolumeMenuItemSubview)]) {
            [(NSView<NovaLINKAppVolumeMenuItemSubview>*)subview setUpWithApp:app
                                                                context:self
                                                             controller:controller
                                                               menuItem:appVolItem];
        }
    }

    // Store the NSRunningApplication object with the menu item so when the app closes we can find the item to remove it
    appVolItem.representedObject = app;

    // Set the slider to the volume for this app if we got one from the driver
    [self setVolumeOfMenuItem:appVolItem relativeVolume:volume panPosition:pan];

    // NSMenuItem didn't implement NSAccessibility before OS X SDK 10.12.
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101200  // MAC_OS_X_VERSION_10_12
    if ([appVolItem respondsToSelector:@selector(setAccessibilityTitle:)]) {
        // TODO: This doesn't show up in Accessibility Inspector for me. Not sure why.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        appVolItem.accessibilityTitle = [NSString stringWithFormat:@"%@", [app localizedName]];
#pragma clang diagnostic pop
    }
#endif

    // Add the menu item to its menu.
    if (app.activationPolicy == NSApplicationActivationPolicyRegular) {
        [novaLINKMenu insertItem:appVolItem atIndex:[self firstMenuItemIndex]];
        numMenuItems++;
    } else if (app.activationPolicy == NSApplicationActivationPolicyAccessory) {
        [moreAppsMenu insertItem:appVolItem atIndex:0];
    }
}

- (NSMenuItem*) getMenuItemForApp:(NSRunningApplication*)app {
    NSInteger lastAppVolumeMenuItemIndex = [self lastMenuItemIndex] - 2;

    for (NSInteger i = [self firstMenuItemIndex]; i <= lastAppVolumeMenuItemIndex; i++) {
        NSMenuItem* item = [novaLINKMenu itemAtIndex:i];
        NSRunningApplication* itemApp = item.representedObject;
        NovaLINKAssert(itemApp, "!itemApp for %s", item.title.UTF8String);

        if ([itemApp isEqual:app]) {
            return item;
        }
    }
    for (NSInteger i = 0; i < [moreAppsMenu numberOfItems]; i++) {
        NSMenuItem* item = [moreAppsMenu itemAtIndex:i];
        NSRunningApplication* itemApp = item.representedObject;
        NovaLINKAssert(itemApp, "!itemApp for %s", item.title.UTF8String);

        if ([itemApp isEqual:app]) {
            return item;
        }
    }

    return nil;
}

- (NovaLINKAppVolumeAndPan) getVolumeAndPanForApp:(NSRunningApplication*)app {
    NovaLINKAppVolumeAndPan result = {
        .volume = -1,
        .pan = kAppPanNoValue
    };

    NSMenuItem *item = [self getMenuItemForApp:app];

    if (item == nil) {
        return result;
    }

    for (NSView* subview in item.view.subviews) {
        // Get the volume.
        if ([subview isKindOfClass:[NovaLINKAVM_VolumeSlider class]]) {
            result.volume = [(NovaLINKAVM_VolumeSlider*)subview intValue];
        }

        // Get the pan position.
        if ([subview isKindOfClass:[NovaLINKAVM_PanSlider class]]) {
            result.pan = [(NovaLINKAVM_PanSlider*)subview intValue];
        }
    }

    return result;
}

- (void) setVolumeAndPan:(NovaLINKAppVolumeAndPan)volumeAndPan forApp:(NSRunningApplication*)app {
    NSMenuItem *item = [self getMenuItemForApp:app];

    if (item == nil) {
        return;
    }

    for (NSView* subview in item.view.subviews) {
        // Set the volume.
        if (volumeAndPan.volume != -1 && [subview isKindOfClass:[NovaLINKAVM_VolumeSlider class]]) {
            [(NovaLINKAVM_VolumeSlider*)subview setRelativeVolume:volumeAndPan.volume];
        }

        // Set the pan position.
        if (volumeAndPan.pan != kAppPanNoValue && [subview isKindOfClass:[NovaLINKAVM_PanSlider class]]) {
            [(NovaLINKAVM_PanSlider*)subview setPanPosition:volumeAndPan.pan];
        }
    }

}

// Create a blank menu item to copy as a template.
- (NSMenuItem*) createBlankAppVolumeMenuItem {
    NSMenuItem* menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    
    menuItem.view = appVolumeView;
    menuItem = [menuItem copy];  // So we can modify a copy of the view, rather than the template itself.
    
    return menuItem;
}

- (void) setVolumeOfMenuItem:(NSMenuItem*)menuItem relativeVolume:(int)volume panPosition:(int)pan {
    // Update the sliders.
    for (NSView* subview in menuItem.view.subviews) {
        // Set the volume.
        if (volume != -1 && [subview isKindOfClass:[NovaLINKAVM_VolumeSlider class]]) {
            [(NovaLINKAVM_VolumeSlider*)subview setRelativeVolume:volume];
        }

        // Set the pan position.
        if (pan != kAppPanNoValue && [subview isKindOfClass:[NovaLINKAVM_PanSlider class]]) {
            [(NovaLINKAVM_PanSlider*)subview setPanPosition:pan];
        }
    }
}

- (NSInteger) firstMenuItemIndex {
    return [self lastMenuItemIndex] - numMenuItems + 1;
}

- (NSInteger) lastMenuItemIndex {
    return [novaLINKMenu indexOfItemWithTag:kSeparatorBelowVolumesMenuItemTag] - 1;
}

- (void) removeMenuItemForApp:(NSRunningApplication*)app {
    // Subtract two extra positions to skip the More Apps menu and the spacer menu item above it.
    NSInteger lastAppVolumeMenuItemIndex = [self lastMenuItemIndex] - 2;

    // Check each app volume menu item and remove the item that controls the given app.

    // Look through the main menu.
    for (NSInteger i = [self firstMenuItemIndex]; i <= lastAppVolumeMenuItemIndex; i++) {
        NSMenuItem* item = [novaLINKMenu itemAtIndex:i];
        NSRunningApplication* itemApp = item.representedObject;
        NovaLINKAssert(itemApp, "!itemApp for %s", item.title.UTF8String);

        if ([itemApp isEqual:app]) {
            [novaLINKMenu removeItem:item];
            numMenuItems--;
            return;
        }
    }

    // Look through the More Apps menu.
    for (NSInteger i = 0; i < [moreAppsMenu numberOfItems]; i++) {
        NSMenuItem* item = [moreAppsMenu itemAtIndex:i];
        NSRunningApplication* itemApp = item.representedObject;
        NovaLINKAssert(itemApp, "!itemApp for %s", item.title.UTF8String);

        if ([itemApp isEqual:app]) {
            [moreAppsMenu removeItem:item];
            return;
        }
    }
}

- (void) showHideExtraControls:(NovaLINKAVM_ShowMoreControlsButton*)button {
    // Show or hide an app's extra controls, currently only pan, in its App Volumes menu item.
    
    NSMenuItem* menuItem = button.cell.representedObject;
    
    NovaLINKAssert(button, "!button");
    NovaLINKAssert(menuItem, "!menuItem");

    CGFloat width = menuItem.view.frame.size.width;
#if DEBUG
    CGFloat height = menuItem.view.frame.size.height;
#endif

    const char* appName =
        [((NSRunningApplication*)menuItem.representedObject).localizedName UTF8String];

    // Using this function (instead of just ==) shouldn't be necessary, but just in case.
#if DEBUG
    BOOL(^nearEnough)(CGFloat x, CGFloat y) = ^BOOL(CGFloat x, CGFloat y) {
        return fabs(x - y) < 0.01;  // We don't need much precision.
    };
#endif
    
    bool allSubviewsShowing = true;
    for (NSView* subview in menuItem.view.subviews) {
        if (subview.hidden) {
            allSubviewsShowing = false;
            break;
        }
        //DebugMsg("NovaLINKAppVolumes:: subview hash / hidden: (%lu) / (%hhd)", (unsigned long)subview.hash, subview.hidden);
    }

    if (allSubviewsShowing) {
        // Hide extra controls
        DebugMsg("NovaLINKAppVolumes::showHideExtraControls: Hiding extra controls (%s)", appName);
        
        NovaLINKAssert(nearEnough(height, appVolumeViewFullHeight), "Extra controls were already hidden");
        
        // Make the menu item shorter to hide the extra controls. Keep the width unchanged.
        menuItem.view.frameSize = NSMakeSize(width, kAppVolumeViewInitialHeight);
        // Turn the button upside down so the arrowhead points down.
        button.frameCenterRotation = 180.0;
        // Move the button up slightly so it aligns with the volume slider.
        [button setFrameOrigin:NSMakePoint(button.frame.origin.x, button.frame.origin.y - 1)];

        // Set the extra controls, and anything else below the fold, to hidden so accessibility
        // clients can skip over them.
        for (NSView* subview in menuItem.view.subviews) {
            CGFloat top = subview.frame.origin.y + subview.frame.size.height;
            if (top <= 0.0) {
                subview.hidden = YES;
            }
        }
    } else {
        // Show extra controls
        DebugMsg("NovaLINKAppVolumes::showHideExtraControls: Showing extra controls (%s)", appName);
        
        NovaLINKAssert(nearEnough(button.frameCenterRotation, 180.0), "Unexpected button rotation");
        NovaLINKAssert(nearEnough(height, kAppVolumeViewInitialHeight), "Extra controls were already shown");
        
        // Make the menu item taller to show the extra controls. Keep the width unchanged.
        menuItem.view.frameSize = NSMakeSize(width, appVolumeViewFullHeight);
        // Turn the button rightside up so the arrowhead points up.
        button.frameCenterRotation = 0.0;
        // Move the button down slightly, back to its original position.
        [button setFrameOrigin:NSMakePoint(button.frame.origin.x, button.frame.origin.y + 1)];

        // Set all of the UI elements in the menu item to "not hidden" for accessibility clients.
        for (NSView* subview in menuItem.view.subviews) {
            subview.hidden = NO;
        }
    }
}

- (void) removeAllAppVolumeMenuItems {
    // Remove all of the menu items this class adds to the menu except for the last two, which are
    // the More Apps menu item and the invisible spacer above it.
    while (numMenuItems > 2) {
        [novaLINKMenu removeItemAtIndex:[self firstMenuItemIndex]];
        numMenuItems--;
    }

    // The More Apps menu only contains app volume menu items, so we can just remove everything.
    [moreAppsMenu removeAllItems];
}

@end

#pragma mark Custom Classes (IB)

// Custom classes for the UI elements in the app volume menu items

@implementation NovaLINKAVM_AppIcon

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)menuItem {
    #pragma unused (ctx, ctrl, menuItem)
    
    self.image = app.icon;

    // Remove the icon from the accessibility hierarchy.
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101000  // MAC_OS_X_VERSION_10_10
    if ([self.cell respondsToSelector:@selector(setAccessibilityElement:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        self.cell.accessibilityElement = NO;
#pragma clang diagnostic pop
    }
#endif
}

@end

@implementation NovaLINKAVM_AppNameLabel

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)menuItem {
    #pragma unused (ctx, ctrl, menuItem)
    
    NSString* name = app.localizedName ? (NSString*)app.localizedName : @"";
    self.stringValue = name;
}

@end

@implementation NovaLINKAVM_ShowMoreControlsButton

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)menuItem {
    #pragma unused (app, ctrl)
    
    // Set up the button that show/hide the extra controls (currently only a pan slider) for the app.
    self.cell.representedObject = menuItem;
    self.target = ctx;
    self.action = @selector(showHideExtraControls:);
    
    // The menu item starts out with the extra controls visible, so we hide them here.
    //
    // TODO: Leave them visible if any of the controls are set to non-default values. The user has no way to
    //       tell otherwise. Maybe we should also make this button look different if the controls are hidden
    //       when they have non-default values.
    [ctx showHideExtraControls:self];

    if ([self respondsToSelector:@selector(setAccessibilityTitle:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        self.accessibilityTitle = @"More options";
#pragma clang diagnostic pop
    }
}

@end

@implementation NovaLINKAVM_VolumeSlider {
    // Will be set to -1 for apps without a pid
    pid_t appProcessID;
    NSString* __nullable appBundleID;
    NovaLINKAppVolumesController* controller;
}

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)menuItem {
    #pragma unused (ctx, menuItem)
    
    controller = ctrl;
    
    self.target = self;
    self.action = @selector(appVolumeChanged);
    
    appProcessID = app.processIdentifier;
    appBundleID = app.bundleIdentifier;
    
    self.maxValue = kAppRelativeVolumeMaxRawValue;
    self.minValue = kAppRelativeVolumeMinRawValue;

    if ([self respondsToSelector:@selector(setAccessibilityTitle:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        self.accessibilityTitle = [NSString stringWithFormat:@"Volume for %@", [app localizedName]];
#pragma clang diagnostic pop
    }
}

// We have to handle snapping for volume sliders ourselves because adding a tick mark (snap point) in Interface Builder
// changes how the slider looks.
- (void) snap {
    // Snap to the 50% point.
    float midPoint = (float)((self.maxValue + self.minValue) / 2);
    if (self.floatValue > (midPoint - kSlidersSnapWithin) && self.floatValue < (midPoint + kSlidersSnapWithin)) {
        self.floatValue = midPoint;
    }
}

- (void) setRelativeVolume:(int)relativeVolume {
    self.intValue = relativeVolume;
    [self snap];
}

- (void) appVolumeChanged {
    // TODO: This (sending updates to the driver) should probably be rate-limited. It uses a fair bit of CPU for me.
    
    DebugMsg("NovaLINKAppVolumes::appVolumeChanged: App volume for %s (%d) changed to %d",
             appBundleID.UTF8String,
             appProcessID,
             self.intValue);
    
    [self snap];

    // The values from our sliders are in
    // [kAppRelativeVolumeMinRawValue, kAppRelativeVolumeMaxRawValue] already.
    [controller setVolume:self.intValue forAppWithProcessID:appProcessID bundleID:appBundleID];
}

@end

@implementation NovaLINKAVM_PanSlider {
    // Will be set to -1 for apps without a pid
    pid_t appProcessID;
    NSString* __nullable appBundleID;
    NovaLINKAppVolumesController* controller;
}

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)menuItem {
    #pragma unused (ctx, menuItem)
    
    controller = ctrl;
    
    self.target = self;
    self.action = @selector(appPanPositionChanged);
    
    appProcessID = app.processIdentifier;
    appBundleID = app.bundleIdentifier;
    
    self.minValue = kAppPanLeftRawValue;
    self.maxValue = kAppPanRightRawValue;

    if ([self respondsToSelector:@selector(setAccessibilityTitle:)]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpartial-availability"
        self.accessibilityTitle = [NSString stringWithFormat:@"Pan for %@", [app localizedName]];
#pragma clang diagnostic pop
    }
}

- (void) setPanPosition:(int)panPosition {
    self.intValue = panPosition;
}

- (void) appPanPositionChanged {
    // TODO: This (sending updates to the driver) should probably be rate-limited. It uses a fair bit of CPU for me.
    
    DebugMsg("NovaLINKAppVolumes::appPanPositionChanged: App pan position for %s changed to %d", appBundleID.UTF8String, self.intValue);

    // The values from our sliders are in [kAppPanLeftRawValue, kAppPanRightRawValue] already.
    [controller setPanPosition:self.intValue forAppWithProcessID:appProcessID bundleID:appBundleID];
}

@end
