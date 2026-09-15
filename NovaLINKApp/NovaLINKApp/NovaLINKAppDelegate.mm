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
//  NovaLINKAppDelegate.mm
//  NovaLINKApp
//
//  Copyright © 2016-2022 Kyle Neideck
//  Copyright © 2021 Marcus Wu
//

// Self Include
#import "NovaLINKAppDelegate.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINKAutoPauseMusic.h"
#import "NovaLINKAutoPauseMenuItem.h"
#import "NovaLINKDebugLoggingMenuItem.h"
#import "NovaLINKMicrophoneAccess.h"
#import "NovaLINKMusicPlayers.h"
#import "NovaLINKOutputDeviceMenuSection.h"
#import "NovaLINKPreferencesMenu.h"
#import "NovaLINKPreferredOutputDevices.h"
#import "NovaLINKStatusBarItem.h"
#import "NovaLINKTermination.h"
#import "NovaLINKUserDefaults.h"
#import "NovaLINKXPCListener.h"
#import "SystemPreferences.h"

#include <unistd.h>


#pragma clang assume_nonnull begin

static NSString* const kOptNoPersistentData  = @"--no-persistent-data";
static NSString* const kOptShowDockIcon      = @"--show-dock-icon";
// Background LaunchAgent mode: playthrough + XPC only. Does not steal the OS default device.
static NSString* const kOptAgent             = @"--agent";
// Must match main.m — GUI launch posts these when the agent already holds the single-instance lock.
static NSString* const kNovaLINKShowCompanionUINotification =
    @"life.thenurim.novalink.ShowCompanionUI";
static NSString* const kNovaLINKQuitAgentForCompanionUINotification =
    @"life.thenurim.novalink.QuitAgentForCompanionUI";
static NSString* const kPassthroughAgentLabel = @"life.thenurim.novalink.PassthroughAgent";

@implementation NovaLINKAppDelegate {
    // The button in the system status bar that shows the main menu.
    NovaLINKStatusBarItem* statusBarItem;
    
    // Only show the 'NovaLINKXPCHelper is missing' error dialog once.
    BOOL haveShownXPCHelperErrorMessage;

    // Persistently stores user settings and data.
    NovaLINKUserDefaults* userDefaults;

    NovaLINKAutoPauseMusic* autoPauseMusic;
    NovaLINKAutoPauseMenuItem* autoPauseMenuItem;
    NovaLINKMusicPlayers* musicPlayers;
    NovaLINKOutputDeviceMenuSection* outputDeviceMenuSection;
    NovaLINKPreferencesMenu* prefsMenu;
    NovaLINKDebugLoggingMenuItem* debugLoggingMenuItem;
    NovaLINKXPCListener* xpcListener;
    NovaLINKPreferredOutputDevices* preferredOutputDevices;

    BOOL agentMode;
    BOOL didSetNovaLINKAsOSDefault;
    BOOL continueLaunchCompleted;
    BOOL uiPromotedFromAgent;
    // Agent was asked to exit so a Finder/GUI launch can take over (do not kickstart agent).
    BOOL quittingForCompanionUIHandoff;
}

@synthesize audioDevices = audioDevices;

- (void) awakeFromNib {
    [super awakeFromNib];

    agentMode = [NSProcessInfo.processInfo.arguments indexOfObject:kOptAgent] != NSNotFound;
    didSetNovaLINKAsOSDefault = NO;
    continueLaunchCompleted = NO;
    uiPromotedFromAgent = NO;
    quittingForCompanionUIHandoff = NO;
    
    // Show NovaLINKApp in the dock, if the command-line option for that was passed. This is used by the
    // UI tests.
    if ([NSProcessInfo.processInfo.arguments indexOfObject:kOptShowDockIcon] != NSNotFound) {
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    } else {
        // Accessory so the status item registers with Control Center / menu bar. Agent mode still
        // shows the icon; it only skips stealing the OS default device (see continueLaunch).
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    }
    
    haveShownXPCHelperErrorMessage = NO;

    // Set up audioDevices, which coordinates NovaLINKDevice and the output device. It manages
    // playthrough, volume/mute controls, etc.
    if (![self initAudioDeviceManager]) {
        return;
    }

    // Stored user settings
    userDefaults = [self createUserDefaults];

    // Status bar companion UI (output device menu, prefs). Shown in agent mode too so the
    // LaunchAgent-hosted process is visible and usable without a separate Finder launch.
    statusBarItem = [[NovaLINKStatusBarItem alloc] initWithMenu:self.novaLINKMenu
                                              audioDevices:audioDevices
                                              userDefaults:userDefaults];
    if (agentMode) {
        NSLog(@"NovaLINKAppDelegate: starting in --agent mode (status bar on, OS default unchanged)");
    }

    NSDistributedNotificationCenter* center = [NSDistributedNotificationCenter defaultCenter];

    // Preferred path: GUI launch asks the agent to exit so a fresh UI process can take the lock.
    [center addObserver:self
               selector:@selector(handleQuitAgentForCompanionUINotification:)
                   name:kNovaLINKQuitAgentForCompanionUINotification
                 object:nil
     suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];

    // Fallback: promote the already-running agent in-process if handoff timed out.
    [center addObserver:self
               selector:@selector(handleShowCompanionUINotification:)
                   name:kNovaLINKShowCompanionUINotification
                 object:nil
     suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
}

- (void) applicationDidFinishLaunching:(NSNotification*)aNotification {
    #pragma unused (aNotification)
    
    // Log the version/build number.
    //
    // TODO: NSLog should only be used for logging errors.
    // TODO: Automatically add the commit ID to the end of the build number for unreleased builds. (In the
    //       Info.plist or something -- not here.)
    NSLog(@"NovaLINKApp version: %@, NovaLINKApp build number: %@",
          NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"],
          NSBundle.mainBundle.infoDictionary[@"CFBundleVersion"]);

    // Handles changing (or not changing) the output device when devices are added or removed. Must
    // be initialised before calling setNovaLINKDeviceAsDefault.
    preferredOutputDevices =
        [[NovaLINKPreferredOutputDevices alloc] initWithDevices:audioDevices userDefaults:userDefaults];

    // Skip this if we're compiling on a version of macOS before 10.14 as won't compile and it
    // isn't needed.
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 101400  // MAC_OS_X_VERSION_10_14
    if (@available(macOS 10.14, *)) {
        // On macOS 10.14+ we need microphone permission for playthrough (virtual input) and
        // hardware-mic inject. Use a single coalesced prompt — never call requestAccess or
        // StartIOProc on input devices until this completes.
        [NovaLINKMicrophoneAccess requestAccessIfNeededWithCompletion:^(BOOL granted) {
            if (granted) {
                DebugMsg("NovaLINKAppDelegate::applicationDidFinishLaunching: Permission granted");
            } else {
                NSLog(@"NovaLINKAppDelegate::applicationDidFinishLaunching: Permission denied");
                if (self->agentMode) {
                    // Stay alive with the menu bar so the user can open System Settings.
                    // Exiting here races KeepAlive and can restart-loop. Input IO stays gated
                    // until Microphone access is enabled for this binary.
                    NSLog(@"NovaLINKAppDelegate: grant Microphone access to "
                          "\"NovaLINK Audio Passthrough\" in System Settings → Privacy & Security "
                          "→ Microphone, then pick an output device again (or relaunch the agent).");
                } else {
                    [self showErrorMessage:@"NovaLINK Audio Passthrough needs microphone permission."
                           informativeText:@"It uses a virtual microphone to access your system's "
                                            "audio.\n\nGrant access in System Settings > Privacy & "
                                            "Security > Microphone for \"NovaLINK Audio Passthrough\"."
                 exitAfterMessageDismissed:YES];
                    return;
                }
            }
            [self continueLaunchAfterInputDevicePermissionGranted];
        }];
    }
    else
#endif
    {
        // We can change the device immediately on older versions of macOS because they don't
        // require user permission for input devices.
        [self continueLaunchAfterInputDevicePermissionGranted];
    }
}

- (void) continueLaunchAfterInputDevicePermissionGranted {
    // Choose an output device for NovaLINKApp to use to play audio.
    if (![self setInitialOutputDevice]) {
        return;
    }

    // Make NovaLINKDevice the default device — but not in agent mode. The agent only hosts
    // playthrough for when the user (or another client) has already selected NovaLINK.
    if (!agentMode) {
        [self setNovaLINKDeviceAsDefault];
        didSetNovaLINKAsOSDefault = YES;
    }

    // Handle some of the unusual reasons NovaLINKApp might have to exit, mostly crashes.
    NovaLINKTermination::SetUpTerminationCleanUp(audioDevices);

    // Set up the rest of the UI and other external interfaces.
    musicPlayers = [[NovaLINKMusicPlayers alloc] initWithAudioDevices:audioDevices
                                                    userDefaults:userDefaults];

    autoPauseMusic = [[NovaLINKAutoPauseMusic alloc] initWithAudioDevices:audioDevices
                                                        musicPlayers:musicPlayers];

    [self setUpMainMenu];

    xpcListener = [[NovaLINKXPCListener alloc] initWithAudioDevices:audioDevices
                                  helperConnectionErrorHandler:^(NSError* error) {
        NSLog(@"NovaLINKAppDelegate::continueLaunchAfterInputDevicePermissionGranted: "
              "(helperConnectionErrorHandler) NovaLINKXPCHelper connection error: %@",
              error);
        // Agent mode: log only — avoid modal dialogs that the user cannot dismiss easily.
        if (!self->agentMode) {
            [self showXPCHelperErrorMessage:error];
        }
    }];

    if (agentMode) {
        NSLog(@"NovaLINKAppDelegate: agent playthrough host ready (status bar visible, OS default unchanged)");
        // If clients are already writing to NovaLINK (user selected it before the agent
        // finished launching), kick playthrough without waiting for another StartIO/XPC edge.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            OSStatus errMain = [self->audioDevices startPlayThroughSync:NO];
            OSStatus errUI = [self->audioDevices startPlayThroughSync:YES];
            NSLog(@"NovaLINKAppDelegate: agent initial playthrough kick "
                  "(main=%d ui=%d)", (int)errMain, (int)errUI);
        });
    }

    continueLaunchCompleted = YES;
}

- (void) handleQuitAgentForCompanionUINotification:(NSNotification*)notification {
    #pragma unused (notification)
    if (!agentMode) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self->quittingForCompanionUIHandoff) {
            return;
        }
        self->quittingForCompanionUIHandoff = YES;
        NSLog(@"NovaLINKAppDelegate: agent exiting so companion UI process can take over");
        [NSApp terminate:nil];
    });
}

- (void) handleShowCompanionUINotification:(NSNotification*)notification {
    #pragma unused (notification)
    dispatch_async(dispatch_get_main_queue(), ^{
        [self promoteAgentToCompanionUI];
    });
}

// Turns a background --agent process into a full companion (sets NovaLINK as OS default).
// Prefer QuitAgentForCompanionUI + fresh GUI process when opening from Finder (see main.m).
- (void) promoteAgentToCompanionUI {
    if (quittingForCompanionUIHandoff) {
        return;
    }

    if (!agentMode && didSetNovaLINKAsOSDefault && statusBarItem) {
        NSLog(@"NovaLINKAppDelegate: companion UI already fully active");
        return;
    }

    if (!continueLaunchCompleted) {
        // Finish launch first, then promote.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)),
                       dispatch_get_main_queue(), ^{
            [self promoteAgentToCompanionUI];
        });
        return;
    }

    NSLog(@"NovaLINKAppDelegate: promoting agent → full companion (set OS default)");
    agentMode = NO;
    uiPromotedFromAgent = YES;

    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    if (!statusBarItem) {
        statusBarItem = [[NovaLINKStatusBarItem alloc] initWithMenu:self.novaLINKMenu
                                                  audioDevices:audioDevices
                                                  userDefaults:userDefaults];
    }

    if (!outputDeviceMenuSection) {
        [self setUpMainMenu];
    }

    if (!didSetNovaLINKAsOSDefault) {
        [self setNovaLINKDeviceAsDefault];
        didSetNovaLINKAsOSDefault = YES;
    }
}

// Returns NO if (and only if) NovaLINKApp is about to terminate because of a fatal error.
- (BOOL) initAudioDeviceManager {
    audioDevices = [NovaLINKAudioDeviceManager new];

    if (!audioDevices) {
        [self showNovaLINKDeviceNotFoundErrorMessageAndExit];
        return NO;
    }

    return YES;
}

// Returns NO if (and only if) NovaLINKApp is about to terminate because of a fatal error.
- (BOOL) setInitialOutputDevice {
    AudioObjectID preferredDevice = [preferredOutputDevices findPreferredDevice];

    if (preferredDevice != kAudioObjectUnknown) {
        NSError* __nullable error = [audioDevices setOutputDeviceWithID:preferredDevice
                                                        revertOnFailure:NO];
        if (error) {
            // Show the error message.
            [self showFailedToSetOutputDeviceErrorMessage:NovaLINKNN(error)
                                          preferredDevice:preferredDevice];
        }
    } else {
        // We couldn't find a device to use, so show an error message and quit.
        [self showOutputDeviceNotFoundErrorMessageAndExit];
        return NO;
    }

    return YES;
}

// Sets the "NovaLINK" virtual audio device (NovaLINKDevice) as the user's default audio device.
- (void) setNovaLINKDeviceAsDefault {
    NSError* error = [audioDevices setNovaLINKDeviceAsOSDefault];

    if (error) {
        [self showSetDeviceAsDefaultError:error
                                  message:@"Could not set the NovaLINK device as your"
                                           "default audio device."
                          informativeText:@"You might be able to change it yourself."];
    }
}

- (void) setUpMainMenu {
    autoPauseMenuItem =
        [[NovaLINKAutoPauseMenuItem alloc] initWithMenuItem:self.autoPauseMenuItemUnwrapped
                                        autoPauseMusic:autoPauseMusic
                                          musicPlayers:musicPlayers
                                          userDefaults:userDefaults];

    // Output device selection.
    outputDeviceMenuSection =
            [[NovaLINKOutputDeviceMenuSection alloc] initWithNovaLINKMenu:self.novaLINKMenu
                                                   audioDevices:audioDevices
                                               preferredDevices:preferredOutputDevices];
    [audioDevices setOutputDeviceMenuSection:outputDeviceMenuSection];

    // Preferences submenu.
    prefsMenu = [[NovaLINKPreferencesMenu alloc] initWithNovaLINKMenu:self.novaLINKMenu
                                               audioDevices:audioDevices
                                               musicPlayers:musicPlayers
                                              statusBarItem:statusBarItem
                                                 aboutPanel:self.aboutPanel
                                      aboutPanelLicenseView:self.aboutPanelLicenseView];

    // Enable/disable debug logging. Hidden unless you option-click the status bar icon.
    debugLoggingMenuItem =
        [[NovaLINKDebugLoggingMenuItem alloc] initWithMenuItem:self.debugLoggingMenuItemUnwrapped];
    [statusBarItem setDebugLoggingMenuItem:debugLoggingMenuItem];

    // Handle events about the main menu. (See the NSMenuDelegate methods below.)
    self.novaLINKMenu.delegate = self;
}

- (NovaLINKUserDefaults*) createUserDefaults {
    BOOL persistentDefaults =
        [NSProcessInfo.processInfo.arguments indexOfObject:kOptNoPersistentData] == NSNotFound;
    NSUserDefaults* wrappedDefaults = persistentDefaults ? [NSUserDefaults standardUserDefaults] : nil;
    return [[NovaLINKUserDefaults alloc] initWithDefaults:wrappedDefaults];
}

- (void) applicationWillTerminate:(NSNotification*)aNotification {
    #pragma unused (aNotification)
    
    DebugMsg("NovaLINKAppDelegate::applicationWillTerminate");

    NSDistributedNotificationCenter* center = [NSDistributedNotificationCenter defaultCenter];
    [center removeObserver:self
                      name:kNovaLINKShowCompanionUINotification
                    object:nil];
    [center removeObserver:self
                      name:kNovaLINKQuitAgentForCompanionUINotification
                    object:nil];

    // Only restore the OS default if this process changed it.
    if (didSetNovaLINKAsOSDefault) {
        NSError* error = [audioDevices unsetNovaLINKDeviceAsOSDefault];
        
        if (error) {
            [self showSetDeviceAsDefaultError:error
                                      message:@"Failed to reset your system's audio output device."
                              informativeText:@"You'll have to change it yourself to get audio working again."];
        }
    }

    // Agent is quitting so a GUI process can take over — do not relaunch the agent here.
    if (quittingForCompanionUIHandoff) {
        NSLog(@"NovaLINKAppDelegate: skipping agent relaunch (companion UI handoff)");
        return;
    }

    // Bring the background LaunchAgent back so passthrough keeps running without the UI
    // process (covers quitting a Finder-launched companion or an agent that showed the bar).
    [self schedulePassthroughAgentRelaunch];
}

- (void) schedulePassthroughAgentRelaunch {
    uid_t uid = getuid();
    NSString* domainLabel = [NSString stringWithFormat:@"gui/%u/%@", uid, kPassthroughAgentLabel];
    NSString* plistPath = [NSHomeDirectory() stringByAppendingPathComponent:
        @"Library/LaunchAgents/life.thenurim.novalink.PassthroughAgent.plist"];
    // Delay so this process releases the instance lock before the agent starts.
    // After a SuccessfulExit=false clean handoff the job is loaded but idle — kickstart is
    // enough; fall back to bootstrap if the job was unloaded.
    NSTask* task = [[NSTask alloc] init];
    task.executableURL = [NSURL fileURLWithPath:@"/bin/bash"];
    task.arguments = @[
        @"-c",
        [NSString stringWithFormat:
         @"sleep 1; "
         @"/bin/launchctl kickstart -k '%@' >/dev/null 2>&1 || "
         @"/bin/launchctl bootstrap 'gui/%u' '%@' >/dev/null 2>&1 || true",
         domainLabel, uid, plistPath]
    ];
    NSError* __nullable err = nil;
    if (![task launchAndReturnError:&err]) {
        NSLog(@"NovaLINKAppDelegate: failed to schedule agent relaunch: %@", err);
    }
}

#pragma mark Error messages

- (void) showNovaLINKDeviceNotFoundErrorMessageAndExit {
    // NovaLINKDevice wasn't found on the system. Most likely, NovaLINKDriver isn't installed. Show an error
    // dialog and exit.
    //
    // TODO: Check whether the driver files are in /Library/Audio/Plug-Ins/HAL? Might even want to
    //       offer to install them if not.
    [self showErrorMessage:@"Could not find the NovaLINK virtual audio device."
           informativeText:@"Make sure you've installed NovaLINK Device.driver to "
                            "/Library/Audio/Plug-Ins/HAL and restarted coreaudiod (e.g. \"sudo "
                            "killall coreaudiod\")."
 exitAfterMessageDismissed:YES];
}

- (void) showFailedToSetOutputDeviceErrorMessage:(NSError*)error
                                 preferredDevice:(NovaLINKAudioDevice)device {
    NSLog(@"Failed to set initial output device. Error: %@", error);

    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert* alert = [NSAlert alertWithError:NovaLINKNN(error)];
        alert.messageText = @"Failed to set the output device.";

        NSString* __nullable name = nil;
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            name = (__bridge NSString* __nullable)device.CopyName();
        });

        alert.informativeText =
                [NSString stringWithFormat:@"Could not start the device '%@'. (Error: %ld)",
                        name, error.code];

        [alert runModal];
    });
}

- (void) showOutputDeviceNotFoundErrorMessageAndExit {
    // We couldn't find any output devices. Show an error dialog and exit.
    [self showErrorMessage:@"Could not find an audio output device."
           informativeText:@"If you do have one installed, this is probably a bug. Sorry about "
                            "that. Feel free to file an issue on GitHub."
 exitAfterMessageDismissed:YES];
}

- (void) showXPCHelperErrorMessage:(NSError*)error {
    if (!haveShownXPCHelperErrorMessage) {
        haveShownXPCHelperErrorMessage = YES;
        
        // NSAlert should only be used on the main thread.
        dispatch_async(dispatch_get_main_queue(), ^{
            NSAlert* alert = [NSAlert new];
            
            // TODO: Offer to install NovaLINKXPCHelper if it's missing.
            // TODO: Show suppression button?
            [alert setMessageText:@"Error connecting to NovaLINKXPCHelper."];
            [alert setInformativeText:[NSString stringWithFormat:@"%s%s%@ (%lu)",
                                       "Make sure you have NovaLINKXPCHelper installed. There are instructions in the "
                                       "README.md file.\n\n"
                                       "NovaLINK might still work, but it won't work as well as it could.",
                                       "\n\nDetails:\n",
                                       [error localizedDescription],
                                       [error code]]];
            [alert runModal];
        });
    }
}

- (void) showErrorMessage:(NSString*)message
          informativeText:(NSString*)informativeText
exitAfterMessageDismissed:(BOOL)fatal {
    // NSAlert should only be used on the main thread.
    dispatch_async(dispatch_get_main_queue(), ^{
        NSAlert* alert = [NSAlert new];
        [alert setMessageText:message];
        [alert setInformativeText:informativeText];

        // This crashes if built with Xcode 9.0.1, but works with versions of Xcode before 9 and
        // with 9.1.
        [alert runModal];

        if (fatal) {
            [NSApp terminate:self];
        }
    });
}

- (void) showSetDeviceAsDefaultError:(NSError*)error
                             message:(NSString*)msg
                     informativeText:(NSString*)info {
    dispatch_async(dispatch_get_main_queue(), ^{
        NSLog(@"%@ %@ Error: %@", msg, info, error);
        
        NSAlert* alert = [NSAlert alertWithError:error];
        alert.messageText = msg;
        alert.informativeText = info;
        
        [alert addButtonWithTitle:@"OK"];
        [alert addButtonWithTitle:@"Open Sound in System Preferences"];
        
        NSModalResponse buttonClicked = [alert runModal];
        
        if (buttonClicked != NSAlertFirstButtonReturn) {  // 'OK' is the first button.
            [self openSysPrefsSoundOutput];
        }
    });
}

- (void) openSysPrefsSoundOutput {
    SystemPreferencesApplication* __nullable sysPrefs =
        [SBApplication applicationWithBundleIdentifier:@"com.apple.systempreferences"];
    
    if (!sysPrefs) {
        NSLog(@"Could not open System Preferences");
        return;
    }
    
    // In System Preferences, go to the "Output" tab on the "Sound" pane.
    for (SystemPreferencesPane* pane : [sysPrefs panes]) {
        DebugMsg("NovaLINKAppDelegate::openSysPrefsSoundOutput: pane = %s", [pane.name UTF8String]);
        
        if ([pane.id isEqualToString:@"com.apple.preference.sound"]) {
            sysPrefs.currentPane = pane;
            
            for (SystemPreferencesAnchor* anchor : [pane anchors]) {
                DebugMsg("NovaLINKAppDelegate::openSysPrefsSoundOutput: anchor = %s", [anchor.name UTF8String]);
                
                if ([[anchor.name lowercaseString] isEqualToString:@"output"]) {
                    DebugMsg("NovaLINKAppDelegate::openSysPrefsSoundOutput: Showing Output in Sound pane.");
                    
                    [anchor reveal];
                }
            }
        }
    }
    
    // Bring System Preferences to the foreground.
    [sysPrefs activate];
}

#pragma mark NSMenuDelegate

- (void) menuNeedsUpdate:(NSMenu*)menu {
    if ([menu isEqual:self.novaLINKMenu]) {
        [autoPauseMenuItem parentMenuNeedsUpdate];
    } else {
        DebugMsg("NovaLINKAppDelegate::menuNeedsUpdate: Warning: unexpected menu. menu=%s", menu.description.UTF8String);
    }
}

- (void) menu:(NSMenu*)menu willHighlightItem:(NSMenuItem* __nullable)item {
    if ([menu isEqual:self.novaLINKMenu]) {
        [autoPauseMenuItem parentMenuItemWillHighlight:item];
    } else {
        DebugMsg("NovaLINKAppDelegate::menu: Warning: unexpected menu. menu=%s", menu.description.UTF8String);
    }
}
@end

#pragma clang assume_nonnull end

