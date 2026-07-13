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
//  NovaLINKASApplication.m
//  NovaLINKApp
//
//  Copyright © 2021 Marcus Wu
//  Copyright © 2021 Kyle Neideck
//

// Self Include
#import "NovaLINKASApplication.h"

// Local Includes
#import "NovaLINK_Types.h"

@implementation NovaLINKASApplication {
    NSScriptObjectSpecifier* parentSpecifier;
    NSRunningApplication *application;
    NovaLINKAppVolumesController* appVolumesController;
    int index;
}

- (instancetype) initWithApplication:(NSRunningApplication*)app
                          volumeController:(NovaLINKAppVolumesController*)volumeController
                     parentSpecifier:(NSScriptObjectSpecifier* __nullable)parent
                               index:(int)i {
    if ((self = [super init])) {
        parentSpecifier = parent;
        application = app;
        appVolumesController = volumeController;
        index = i;
    }

    return self;
}

- (NSString*) name {
    return [NSString stringWithFormat:@"%@", [application localizedName]];
}

- (NSString*) bundleID {
    return [NSString stringWithFormat:@"%@", [application bundleIdentifier]];
}

- (int) volume {
    return [appVolumesController getVolumeAndPanForApp:application].volume;
}

- (void) setVolume:(int)vol {
    NovaLINKAppVolumeAndPan volume = {
        .volume = vol,
        .pan = kAppPanNoValue
    };
    [appVolumesController setVolumeAndPan:volume forApp:application];
}

- (int) pan {
    return [appVolumesController getVolumeAndPanForApp:application].pan;
}

- (void) setPan:(int)pan {
    NovaLINKAppVolumeAndPan thePan = {
        .volume = -1,
        .pan = pan
    };
    [appVolumesController setVolumeAndPan:thePan forApp:application];
}

- (NSScriptObjectSpecifier* __nullable) objectSpecifier {
    NSScriptClassDescription* parentClassDescription = [parentSpecifier keyClassDescription];
    return [[NSNameSpecifier alloc] initWithContainerClassDescription:parentClassDescription
                                                   containerSpecifier:parentSpecifier
                                                                  key:@"applications"
                                                                 name:self.name];
}

@end
