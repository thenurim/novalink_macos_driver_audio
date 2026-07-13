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
//  NovaLINKScriptingBridge.m
//  NovaLINKApp
//
//  Copyright © 2016-2019 Kyle Neideck
//

// Self Include
#import "NovaLINKScriptingBridge.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINKAppWatcher.h"

// PublicUtility Includes
#import "CADebugMacros.h"


#pragma clang assume_nonnull begin

@implementation NovaLINKScriptingBridge {
    id<NovaLINKMusicPlayer> __weak _musicPlayer;
    NovaLINKAppWatcher* appWatcher;
}

@synthesize application = _application;

- (instancetype) initWithMusicPlayer:(id<NovaLINKMusicPlayer>)musicPlayer {
    if ((self = [super init])) {
        _musicPlayer = musicPlayer;
        
        [self initApplication];
    }
    
    return self;
}

- (void) initApplication {
    NSString* bundleID = _musicPlayer.bundleID;
    NovaLINKAssert(bundleID, "Music players need a bundle ID to use ScriptingBridge");

    NovaLINKScriptingBridge* __weak weakSelf = self;

    void (^createSBApplication)(void) = ^{
        NovaLINKScriptingBridge* strongSelf = weakSelf;
        strongSelf->_application = [SBApplication applicationWithBundleIdentifier:bundleID];
        // TODO: The SBApplication will still keep a strong ref to this object, so we would have to
        //       make a separate delegate object to avoid the retain cycle. Not currently a problem
        //       because we only ever create instances that live forever.
        strongSelf->_application.delegate = strongSelf;
    };
    
    // Add observers that create/destroy the SBApplication when the music player is launched/terminated. We
    // only create the SBApplication when the music player is open. If it isn't open, creating the
    // SBApplication or sending it events could launch the music player. Whether or not it does depends on
    // the music player, and possibly the version of the music player, so to be safe we assume they all do.
    //
    // From the docs for SBApplication's applicationWithBundleIdentifier method:
    //     "For applications that declare themselves to have a dynamic scripting interface, this method will
    //     launch the application if it is not already running."
    appWatcher =
        [[NovaLINKAppWatcher alloc] initWithBundleID:bundleID
                                    appLaunched:^{
                                        DebugMsg("NovaLINKScriptingBridge::initApplication: %s launched",
                                                 bundleID.UTF8String);
                                        createSBApplication();
                                        [weakSelf ensurePermission];
                                    }
                                  appTerminated:^{
                                      NovaLINKScriptingBridge* strongSelf = weakSelf;
                                      DebugMsg("NovaLINKScriptingBridge::initApplication: %s terminated",
                                               bundleID.UTF8String);
                                      strongSelf->_application = nil;
                                  }];
    
    // Create the SBApplication if the music player is already running.
    if ([NSRunningApplication runningApplicationsWithBundleIdentifier:bundleID].count > 0) {
        createSBApplication();
    }
}

- (void) ensurePermission {
    // Skip this check if running on a version of macOS before 10.14. In that case, we don't require
    // user permission to send Apple Events. Also skip it if compiling on an earlier version.
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400  // MAC_OS_X_VERSION_10_14
    if (@available(macOS 10.14, *)) {
        id<NovaLINKMusicPlayer> musicPlayer = _musicPlayer;

        if (!musicPlayer.selected) {
            DebugMsg("NovaLINKScriptingBridge::ensurePermission: %s not selected. Nothing to do.",
                     musicPlayer.name.UTF8String);
            return;
        }

        if (!musicPlayer.running) {
            DebugMsg("NovaLINKScriptingBridge::ensurePermission: %s not running. Nothing to do.",
                     musicPlayer.name.UTF8String);
            return;
        }

        // AEDeterminePermissionToAutomateTarget will block if it has to show a dialog to the user
        // to ask for permission, so dispatch this to make sure it doesn't run on the main thread.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            NSAppleEventDescriptor* musicPlayerEventDescriptor =
                [NSAppleEventDescriptor
                 descriptorWithBundleIdentifier:(NSString*)musicPlayer.bundleID];

            OSStatus status =
                AEDeterminePermissionToAutomateTarget(musicPlayerEventDescriptor.aeDesc,
                                                      typeWildCard,
                                                      typeWildCard,
                                                      true);

            DebugMsg("NovaLINKScriptingBridge::ensurePermission: "
                     "Apple Events permission status for %s: %d",
                     musicPlayer.name.UTF8String,
                     status);

            if (status != noErr) {
                // TODO: If they deny permission, we should grey-out the auto-pause menu item and
                //       add something to the UI that indicates the problem. Maybe a warning icon
                //       that shows an explanation when you hover your mouse over it. (We can't just
                //       ask them again later because the API doesn't support it. They can only fix
                //       it in System Preferences.)
                NSLog(@"NovaLINKScriptingBridge::ensurePermission: Permission denied for %@. status=%d",
                      musicPlayer.name,
                      status);
            }
        });
    } else {
        DebugMsg("NovaLINKScriptingBridge::ensurePermission: Not macOS 10.14+. Nothing to do.");
    }
#endif /* MAC_OS_X_VERSION_MAX_ALLOWED >= 101400 */
}

#pragma mark SBApplicationDelegate

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability"  // See explanation in the header file.
- (id __nullable) eventDidFail:(const AppleEvent*)event withError:(NSError*)error {
#pragma clang diagnostic pop
    // So far, this just logs the error.
    
#if DEBUG
    NSString* vars = [NSString stringWithFormat:@"event='%4.4s' error=%@ application=%@",
                         (char*)&(event->descriptorType), error, self.application];
    DebugMsg("NovaLINKScriptingBridge::eventDidFail: Apple event sent to %s failed. %s",
             _musicPlayer.bundleID.UTF8String,
             vars.UTF8String);
#else
    #pragma unused (event, error)
#endif

    return nil;
}

@end

#pragma clang assume_nonnull end

