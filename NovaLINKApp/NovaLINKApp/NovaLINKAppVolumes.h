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
//  NovaLINKAppVolumes.h
//  NovaLINKApp
//
//  Copyright © 2016, 2017 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//

// Local Includes
#import "NovaLINKAppVolumesController.h"

// System Includes
#import <Cocoa/Cocoa.h>


#pragma clang assume_nonnull begin

@interface NovaLINKAppVolumes : NSObject

- (id) initWithController:(NovaLINKAppVolumesController*)inController
                  novaLINKMenu:(NSMenu*)inMenu
            appVolumeView:(NSView*)inView;

// Pass -1 for initialVolume or kAppPanNoValue for initialPan to leave the volume/pan at its default level.
- (void) insertMenuItemForApp:(NSRunningApplication*)app
                initialVolume:(int)volume
                   initialPan:(int)pan;

- (void) removeMenuItemForApp:(NSRunningApplication*)app;

- (void) removeAllAppVolumeMenuItems;

- (NovaLINKAppVolumeAndPan) getVolumeAndPanForApp:(NSRunningApplication*)app;
- (void) setVolumeAndPan:(NovaLINKAppVolumeAndPan)volumeAndPan forApp:(NSRunningApplication*)app;

@end

// Protocol for the UI custom classes

@protocol NovaLINKAppVolumeMenuItemSubview <NSObject>

- (void) setUpWithApp:(NSRunningApplication*)app
              context:(NovaLINKAppVolumes*)ctx
           controller:(NovaLINKAppVolumesController*)ctrl
             menuItem:(NSMenuItem*)item;

@end

// Custom classes for the UI elements in the app volume menu items

@interface NovaLINKAVM_AppIcon : NSImageView <NovaLINKAppVolumeMenuItemSubview>
@end

@interface NovaLINKAVM_AppNameLabel : NSTextField <NovaLINKAppVolumeMenuItemSubview>
@end

@interface NovaLINKAVM_ShowMoreControlsButton : NSButton <NovaLINKAppVolumeMenuItemSubview>
@end

@interface NovaLINKAVM_VolumeSlider : NSSlider <NovaLINKAppVolumeMenuItemSubview>

- (void) setRelativeVolume:(int)relativeVolume;

@end

@interface NovaLINKAVM_PanSlider : NSSlider <NovaLINKAppVolumeMenuItemSubview>

- (void) setPanPosition:(int)panPosition;

@end

#pragma clang assume_nonnull end


