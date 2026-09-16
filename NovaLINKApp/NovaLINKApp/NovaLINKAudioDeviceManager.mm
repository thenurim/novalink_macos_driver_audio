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
//  NovaLINKAudioDeviceManager.mm
//  NovaLINKApp
//
//  Copyright © 2016-2018 Kyle Neideck
//

// Self Include
#import "NovaLINKAudioDeviceManager.h"

// Local Includes
#import "NovaLINK_Types.h"
#import "NovaLINK_Utils.h"
#import "NovaLINKAudioDevice.h"
#import "NovaLINKDeviceControlSync.h"
#import "NovaLINKOutputDeviceMenuSection.h"
#import "NovaLINKPlayThrough.h"
#import "NovaLINKMicInputMixer.h"
#import "NovaLINKXPCProtocols.h"

// PublicUtility Includes
#import "CAAtomic.h"
#import "CAAutoDisposer.h"
#import "CAHALAudioSystemObject.h"


#pragma clang assume_nonnull begin

@implementation NovaLINKAudioDeviceManager {
    // This ivar is a pointer so that NovaLINKDevice's constructor doesn't get called
    // during [NovaLINKAudioDeviceManager alloc] when the ivars are initialised. It queries the HAL for
    // NovaLINKDevice's AudioObject ID, which might throw a CAException, most likely because NovaLINKDevice
    // isn't installed.
    //
    // That would be the only way for [NovaLINKAudioDeviceManager alloc] to throw a CAException, so we
    // could wrap that call in a try/catch block instead, but it would make the code a bit
    // confusing.
    NovaLINKDevice* novaLINKDevice;
    NovaLINKAudioDevice outputDevice;
    
    NovaLINKDeviceControlSync deviceControlSync;
    NovaLINKPlayThrough playThrough;
    NovaLINKPlayThrough playThrough_UISounds;

    // A connection to NovaLINKXPCHelper so we can send it the ID of the output device.
    NSXPCConnection* __nullable novaLINKXPCHelperConnection;

    NovaLINKOutputDeviceMenuSection* __nullable outputDeviceMenuSection;

    NSRecursiveLock* stateLock;
}

#pragma mark Construction/Destruction

- (instancetype) init {
    if ((self = [super init])) {
        stateLock = [NSRecursiveLock new];
        novaLINKXPCHelperConnection = nil;
        outputDeviceMenuSection = nil;
        outputDevice = kAudioObjectUnknown;

        try {
            novaLINKDevice = new NovaLINKDevice;
            [[NovaLINKMicInputMixer sharedInstance] startDemandMonitoring];
        } catch (const CAException& e) {
            LogError("NovaLINKAudioDeviceManager::init: NovaLINKDevice not found. (%d)", e.GetError());
            self = nil;
            return self;
        }
    }
    
    return self;
}

- (void) dealloc {
    @try {
        [stateLock lock];

        [[NovaLINKMicInputMixer sharedInstance] stop];

        if (novaLINKDevice) {
            delete novaLINKDevice;
            novaLINKDevice = nullptr;
        }
    } @finally {
        [stateLock unlock];
    }
}

- (void) setOutputDeviceMenuSection:(NovaLINKOutputDeviceMenuSection*)menuSection {
    outputDeviceMenuSection = menuSection;
}

#pragma mark Systemwide Default Device

// Note that there are two different "default" output devices on OS X: "output" and "system output". See
// kAudioHardwarePropertyDefaultSystemOutputDevice in AudioHardware.h.

- (NSError* __nullable) setNovaLINKDeviceAsOSDefault {
    try {
        // Intentionally avoid taking stateLock before making calls to the HAL. See
        // startPlayThroughSync.
        CAMemoryBarrier();
        novaLINKDevice->SetAsOSDefault();
    } catch (const CAException& e) {
        NovaLINKLogExceptionIn("NovaLINKAudioDeviceManager::setNovaLINKDeviceAsOSDefault", e);
        return [NSError errorWithDomain:@kNovaLINKAppBundleID code:e.GetError() userInfo:nil];
    }

    return nil;
}

- (NSError* __nullable) unsetNovaLINKDeviceAsOSDefault {
    // Copy the devices so we can call the HAL without holding stateLock. See startPlayThroughSync.
    NovaLINKDevice* novaLINKDeviceCopy;
    AudioDeviceID outputDeviceID;

    @try {
        [stateLock lock];
        novaLINKDeviceCopy = novaLINKDevice;
        outputDeviceID = outputDevice.GetObjectID();
    } @finally {
        [stateLock unlock];
    }

    if (outputDeviceID == kAudioObjectUnknown) {
        return [NSError errorWithDomain:@kNovaLINKAppBundleID
                                   code:kNovaLINKErrorCode_OutputDeviceNotFound
                               userInfo:nil];
    }

    try {
        novaLINKDeviceCopy->UnsetAsOSDefault(outputDeviceID);
    } catch (const CAException& e) {
        NovaLINKLogExceptionIn("NovaLINKAudioDeviceManager::unsetNovaLINKDeviceAsOSDefault", e);
        return [NSError errorWithDomain:@kNovaLINKAppBundleID code:e.GetError() userInfo:nil];
    }
    
    return nil;
}

#pragma mark Accessors

- (NovaLINKDevice) novaLINKDevice {
    return *novaLINKDevice;
}

- (CAHALAudioDevice) outputDevice {
    return outputDevice;
}

- (BOOL) isOutputDevice:(AudioObjectID)deviceID {
    @try {
        [stateLock lock];
        return deviceID == outputDevice.GetObjectID();
    } @finally {
        [stateLock unlock];
    }
}

- (BOOL) isOutputDataSource:(UInt32)dataSourceID {
    BOOL isOutputDataSource = NO;

    @try {
        [stateLock lock];
        
        try {
            AudioObjectPropertyScope scope = kAudioDevicePropertyScopeOutput;
            UInt32 channel = 0;
            
            isOutputDataSource =
                    outputDevice.HasDataSourceControl(scope, channel) &&
                            (dataSourceID == outputDevice.GetCurrentDataSourceID(scope, channel));
        } catch (const CAException& e) {
            NovaLINKLogException(e);
        }
    } @finally {
        [stateLock unlock];
    }

    return isOutputDataSource;
}

#pragma mark Output Device

- (NSError* __nullable) setOutputDeviceWithID:(AudioObjectID)deviceID
                              revertOnFailure:(BOOL)revertOnFailure {
    return [self setOutputDeviceWithIDImpl:deviceID
                              dataSourceID:nil
                           revertOnFailure:revertOnFailure];
}

- (NSError* __nullable) setOutputDeviceWithID:(AudioObjectID)deviceID
                                 dataSourceID:(UInt32)dataSourceID
                              revertOnFailure:(BOOL)revertOnFailure {
    return [self setOutputDeviceWithIDImpl:deviceID
                              dataSourceID:&dataSourceID
                           revertOnFailure:revertOnFailure];
}

// If NovaLINKDevice's own AudioObjectID has gone bad (most likely because something restarted
// coreaudiod out from under us — e.g. another installer replacing the driver bundle), replace
// `novaLINKDevice` with a freshly re-resolved instance. Every other object here (deviceControlSync,
// playThrough, playThrough_UISounds) re-reads `*novaLINKDevice` on every switch, so updating this
// one ivar is enough to un-stick them without recreating the whole manager.
- (void) reresolveNovaLINKDeviceIfDead {
    @try {
        [stateLock lock];

        bool novaLINKDeviceAlive = false;
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            novaLINKDeviceAlive = novaLINKDevice->IsAlive();
        });

        if (!novaLINKDeviceAlive) {
            try {
                NovaLINKDevice* freshNovaLINKDevice = new NovaLINKDevice;
                delete novaLINKDevice;
                novaLINKDevice = freshNovaLINKDevice;
                LogWarning("NovaLINKAudioDeviceManager::reresolveNovaLINKDeviceIfDead: "
                           "NovaLINKDevice's own AudioObjectID was invalid (probably coreaudiod "
                           "restarted externally) — re-resolved a fresh one. newID=%u",
                           novaLINKDevice->GetObjectID());
            } catch (const CAException& e) {
                LogError("NovaLINKAudioDeviceManager::reresolveNovaLINKDeviceIfDead: Failed to "
                         "re-resolve NovaLINKDevice. (%d)",
                         e.GetError());
            }
        }
    } @finally {
        [stateLock unlock];
    }
}

- (NSError* __nullable) setOutputDeviceWithIDImpl:(AudioObjectID)newDeviceID
                                     dataSourceID:(UInt32* __nullable)dataSourceID
                                  revertOnFailure:(BOOL)revertOnFailure {
    DebugMsg("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl: Setting output device. newDeviceID=%u",
             newDeviceID);
    
    @try {
        [stateLock lock];

        AudioDeviceID currentDeviceID = outputDevice.GetObjectID();  // (Doesn't throw.)

        // Something external (another installer replacing the driver bundle and running
        // `killall coreaudiod`, a macOS update, ...) can restart coreaudiod out from under us.
        // That invalidates NovaLINKDevice's own AudioObjectID — not just whichever real output
        // device we're about to switch to — and every HAL call routed through it then fails the
        // same way regardless of the target, forever, because nothing else here ever re-resolves
        // it. Detect that up front and self-heal before touching the target device at all.
        [self reresolveNovaLINKDeviceIfDead];

        // AudioObjectIDs are just per-boot-session HAL handles. While a device is still
        // settling right after boot (Bluetooth reconnecting, coreaudiod restarting IO for a
        // plugin config change, ...), coreaudiod can renumber them — so retrying with the same
        // numeric ID chases a target that's already moved on and can never succeed even after
        // a long wait. Capture the target's UID (stable across renumbering/reboots) up front so
        // each retry can re-resolve the *current* AudioObjectID for the same physical device.
        CFStringRef __nullable targetDeviceUID = nullptr;
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            targetDeviceUID = NovaLINKAudioDevice(newDeviceID).CopyDeviceUID();
        });

        NSDate* retryDeadline = [NSDate dateWithTimeIntervalSinceNow:8.0];
        BOOL succeeded = NO;
        NSError* __nullable lastError = nil;

        while (!succeeded) {
            try {
                [self setOutputDeviceWithIDImpl:newDeviceID
                                   dataSourceID:dataSourceID
                                currentDeviceID:currentDeviceID];
                succeeded = YES;
            } catch (const CAException& e) {
                NovaLINKAssert(e.GetError() != kAudioHardwareNoError,
                          "CAException with kAudioHardwareNoError");

                if (e.GetError() == kAudioHardwareBadObjectError &&
                    retryDeadline.timeIntervalSinceNow > 0) {
                    [self reresolveNovaLINKDeviceIfDead];

                    AudioObjectID reresolvedID = kAudioObjectUnknown;
                    if (targetDeviceUID) {
                        CAHALAudioSystemObject audioSystem;
                        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
                            reresolvedID = audioSystem.GetAudioDeviceForUID(targetDeviceUID);
                        });
                    }

                    LogWarning("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl: Got "
                               "kAudioHardwareBadObjectError for device %u, probably renumbered "
                               "while settling. Re-resolved by UID to %u. Retrying for up to "
                               "%.1fs more.",
                               newDeviceID,
                               reresolvedID,
                               retryDeadline.timeIntervalSinceNow);

                    if (reresolvedID != kAudioObjectUnknown) {
                        newDeviceID = reresolvedID;
                    }

                    [NSThread sleepForTimeInterval:0.3];
                    continue;
                }

                lastError = [self failedToSetOutputDevice:newDeviceID
                                                 errorCode:e.GetError()
                                                  revertTo:(revertOnFailure ? &currentDeviceID : nullptr)];
                break;
            } catch (...) {
                lastError = [self failedToSetOutputDevice:newDeviceID
                                                 errorCode:kAudioHardwareUnspecifiedError
                                                  revertTo:(revertOnFailure ? &currentDeviceID : nullptr)];
                break;
            }
        }

        if (targetDeviceUID) {
            CFRelease(targetDeviceUID);
        }

        if (lastError) {
            return lastError;
        }

        // Tell other classes and NovaLINKXPCHelper that we changed the output device.
        [self propagateOutputDeviceChange];
    } @finally {
        [stateLock unlock];
    }

    return nil;
}

// Throws CAException.
- (void) setOutputDeviceWithIDImpl:(AudioObjectID)newDeviceID
                      dataSourceID:(UInt32* __nullable)dataSourceID
                   currentDeviceID:(AudioObjectID)currentDeviceID {
    // Snapshot before we tear playthrough down / change NovaLINK's sample rate. Config changes
    // can make Chrome briefly drop IO, so a post-switch StopIfIdle would race and kill the new
    // output path while YouTube is still intended to be playing.
    BOOL clientsPlaying = NO;
    if (newDeviceID != currentDeviceID) {
        NovaLINKLogAndSwallowExceptions("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl", [&] {
            clientsPlaying = playThrough.ClientsArePlaying() || playThrough_UISounds.ClientsArePlaying();
        });

        NovaLINKAudioDevice newOutputDevice(newDeviceID);
        [self setOutputDeviceForPlaythroughAndControlSync:newOutputDevice];
        outputDevice = newOutputDevice;
    }

    // Set the output device to use the new data source.
    if (dataSourceID) {
        // TODO: If this fails, ideally we'd still start playthrough and return an error, but not
        //       revert the device. It would probably be a bit awkward, though.
        [self setDataSource:*dataSourceID device:outputDevice];
    }

    if (newDeviceID != currentDeviceID) {
        // We successfully changed to the new device. Start playthrough on it, since audio might be
        // playing. (If we only changed the data source, playthrough will already be running if it
        // needs to be.)
        //
        // Do not call StopIfIdle here. Device switches interrupt Chrome's IO; an idle-stop would
        // tear down playthrough while audio is supposed to keep flowing. StopIfIdle is armed again
        // only after a non-App client is observed playing (see NovaLINKPlayThrough::StopIfIdle).
        playThrough.Start();
        playThrough_UISounds.Start();
        // If Chrome (etc.) never dropped IO across the switch, this arms idle-stop without
        // stopping. If they did drop, StopIfIdle stays suppressed until they return.
        NovaLINKLogAndSwallowExceptions("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl", [&] {
            playThrough.StopIfIdle();
            playThrough_UISounds.StopIfIdle();
        });

        if (clientsPlaying) {
            // Clients often restart IO after NovaLINK's sample rate/buffer change — nudge Start.
            auto restartPlaythrough = ^{
                @try {
                    [stateLock lock];
                    NovaLINKLogAndSwallowExceptions("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl", [&] {
                        playThrough.Start();
                        playThrough_UISounds.Start();
                        playThrough.StopIfIdle();
                        playThrough_UISounds.StopIfIdle();
                    });
                } @finally {
                    [stateLock unlock];
                }
            };
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.35 * NSEC_PER_SEC)),
                           NovaLINKGetDispatchQueue_PriorityUserInteractive(),
                           restartPlaythrough);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.0 * NSEC_PER_SEC)),
                           NovaLINKGetDispatchQueue_PriorityUserInteractive(),
                           restartPlaythrough);
        }
    }

    CFStringRef outputDeviceUID = outputDevice.CopyDeviceUID();
    DebugMsg("NovaLINKAudioDeviceManager::setOutputDeviceWithIDImpl: Set output device to %s (%d)",
             CFStringGetCStringPtr(outputDeviceUID, kCFStringEncodingUTF8),
             outputDevice.GetObjectID());
    CFRelease(outputDeviceUID);
}

// Changes the output device that playthrough plays audio to and that NovaLINKDevice's controls are
// kept in sync with. Throws CAException.
- (void) setOutputDeviceForPlaythroughAndControlSync:(const NovaLINKAudioDevice&)newOutputDevice {
    // Deactivate playthrough rather than stopping it so it can't be started by HAL notifications
    // while we're updating deviceControlSync.
    playThrough.Deactivate();
    playThrough_UISounds.Deactivate();

    deviceControlSync.SetDevices(*novaLINKDevice, newOutputDevice);
    deviceControlSync.Activate();

    // Stream audio from NovaLINKDevice to the new output device. This blocks while the old device stops
    // IO.
    playThrough.SetDevices(novaLINKDevice, &newOutputDevice);
    playThrough.Activate();

    // TODO: Support setting different devices as the default output device and the default system
    //       output device the way OS X does?
    NovaLINKAudioDevice uiSoundsDevice = novaLINKDevice->GetUISoundsNovaLINKDeviceInstance();
    playThrough_UISounds.SetDevices(&uiSoundsDevice, &newOutputDevice);
    playThrough_UISounds.Activate();

    // Keep hardware mic mixed into NovaLINK virtual input only while capture clients
    // (Zoom/OBS/…) are reading that input — avoids a permanent microphone privacy indicator.
    [[NovaLINKMicInputMixer sharedInstance] syncToCaptureDemand];
}

- (void) setDataSource:(UInt32)dataSourceID device:(NovaLINKAudioDevice&)device {
    NovaLINKLogAndSwallowExceptions("NovaLINKAudioDeviceManager::setDataSource", ([&] {
        AudioObjectPropertyScope scope = kAudioObjectPropertyScopeOutput;
        UInt32 channel = 0;

        if (device.DataSourceControlIsSettable(scope, channel)) {
            DebugMsg("NovaLINKAudioDeviceManager::setOutputDeviceWithID: Setting dataSourceID=%u",
                     dataSourceID);
            
            device.SetCurrentDataSourceByID(scope, channel, dataSourceID);
        }
    }));
}

- (void) propagateOutputDeviceChange {
    // Tell NovaLINKXPCHelper that the output device has changed.
    [self sendOutputDeviceToNovaLINKXPCHelper];

    [outputDeviceMenuSection outputDeviceDidChange];
}

- (NSError*) failedToSetOutputDevice:(AudioDeviceID)deviceID
                           errorCode:(OSStatus)errorCode
                            revertTo:(AudioDeviceID*)revertTo {
    // Using LogWarning from PublicUtility instead of NSLog here crashes from a bad access. Not sure why.
    // TODO: Possibly caused by a bug in CADebugMacros.cpp. See commit ab9d4cd.
    NSLog(@"NovaLINKAudioDeviceManager::failedToSetOutputDevice: Couldn't set device with ID %u as output device. "
          "%s%d. %@",
          deviceID,
          "Error: ", errorCode,
          (revertTo ? [NSString stringWithFormat:@"Will attempt to revert to the previous device. "
                                                  "Previous device ID: %u.", *revertTo] : @""));
    
    NSDictionary* __nullable info = nil;
    
    if (revertTo) {
        // Try to reactivate the original device listener and playthrough. (Sorry about the mutual recursion.)
        NSError* __nullable revertError = [self setOutputDeviceWithID:*revertTo revertOnFailure:NO];
        
        if (revertError) {
            info = @{ @"revertError": (NSError*)revertError };
        }
    } else {
        // TODO: Handle this error better in callers. Maybe show an error dialog and try to set the original
        //       default device as the output device.
        NSLog(@"NovaLINKAudioDeviceManager::failedToSetOutputDevice: Failed to revert to the previous device.");
    }
    
    return [NSError errorWithDomain:@kNovaLINKAppBundleID code:errorCode userInfo:info];
}

- (OSStatus) startPlayThroughSync:(BOOL)forUISoundsDevice {
    // We can only try for stateLock because setOutputDeviceWithID might have already taken it, then made a
    // HAL request to NovaLINKDevice and be waiting for the response. Some of the requests setOutputDeviceWithID
    // makes to NovaLINKDevice block in the HAL if another thread is in NovaLINK_Device::StartIO.
    //
    // Since NovaLINK_Device::StartIO calls this method (via XPC), waiting for setOutputDeviceWithID to release
    // stateLock could cause deadlocks. Instead we return early with an error code that NovaLINKDriver knows to
    // ignore, since the output device is (almost certainly) being changed and we can't avoid dropping frames
    // while the output device starts up.
    OSStatus err;
    BOOL gotLock;
    
    @try {
        gotLock = [stateLock tryLock];

        BOOL isBigSur = NO;
        if (@available(macOS 11.0, *)) {
            isBigSur = YES;
        }

        // Always start playthrough asynchronously on macOS 11+. Temp workaround for deadlock on Big Sur+:
        // CoreAudio blocks App HAL calls until the driver's StartIO returns, so we cannot wait for the
        // real output device here. See Background Music #328.
        if (!isBigSur && gotLock) {
            NovaLINKPlayThrough& pt = (forUISoundsDevice ? playThrough_UISounds : playThrough);

            // Playthrough might not have been notified that NovaLINKDevice is starting yet, so make sure
            // playthrough is starting. This way we won't drop any frames while waiting for the HAL to send
            // that notification. We can't be completely sure this is safe from deadlocking, though, since
            // CoreAudio is closed-source.
            //
            // TODO: Test this on older OS X versions. Differences in the CoreAudio implementations could
            //       cause deadlocks.
            NovaLINKLogAndSwallowExceptionsMsg("NovaLINKAudioDeviceManager::startPlayThroughSync",
                                          "Starting playthrough", [&] {
                pt.Start();
            });

            err = pt.WaitForOutputDeviceToStart();
            NovaLINKAssert(err != NovaLINKPlayThrough::kDeviceNotStarting, "Playthrough didn't start");
        } else {
            if (!gotLock) {
                LogWarning("NovaLINKAudioDeviceManager::startPlayThroughSync: Didn't get state lock. "
                           "Returning early with kNovaLINKErrorCode_ReturningEarly.");
            } else {
                DebugMsg("NovaLINKAudioDeviceManager::startPlayThroughSync: Starting playthrough "
                         "asynchronously (macOS 11+ deadlock workaround).");
            }
            err = kNovaLINKErrorCode_ReturningEarly;

            // Defer Start until after the client's StartIO has returned from the HAL. Starting
            // playthrough IOProcs in the same turn races with HAL StartIO completion and can
            // deadlock — especially when the real output is Bluetooth (slow StartIOProc).
            constexpr int64_t kStartPlayThroughDeferNsec = 50 * NSEC_PER_MSEC;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kStartPlayThroughDeferNsec),
                           NovaLINKGetDispatchQueue_PriorityUserInteractive(), ^{
                @try {
                    [stateLock lock];

                    NovaLINKPlayThrough& pt = (forUISoundsDevice ? playThrough_UISounds : playThrough);

                    NovaLINKLogAndSwallowExceptionsMsg("NovaLINKAudioDeviceManager::startPlayThroughSync",
                                                  "Starting playthrough (dispatched)", [&] {
                        pt.Start();
                    });
                    // Idle-stop is suppressed inside Start()/StopIfIdle until a non-App client is
                    // observed again — do not call StopIfIdle here.
                } @finally {
                    [stateLock unlock];
                }
            });
        }
    } @finally {
        if (gotLock) {
            [stateLock unlock];
        }
    }
    
    return err;
}

#pragma mark NovaLINKXPCHelper Communication

- (void) setNovaLINKXPCHelperConnection:(NSXPCConnection* __nullable)connection {
    novaLINKXPCHelperConnection = connection;

    // Tell NovaLINKXPCHelper which device is the output device, since it might not be up-to-date.
    [self sendOutputDeviceToNovaLINKXPCHelper];
}

- (void) sendOutputDeviceToNovaLINKXPCHelper {
    NSXPCConnection* __nullable connection = novaLINKXPCHelperConnection;

    if (connection)
    {
        id<NovaLINKXPCHelperXPCProtocol> helperProxy =
                [connection remoteObjectProxyWithErrorHandler:^(NSError* error) {
                    // We could wait a bit and try again, but it isn't that important.
                    NSLog(@"NovaLINKAudioDeviceManager::sendOutputDeviceToNovaLINKXPCHelper: Connection"
                           "error: %@", error);
                }];

        [helperProxy setOutputDeviceToMakeDefaultOnAbnormalTermination:outputDevice.GetObjectID()];
    }
}

@end

#pragma clang assume_nonnull end


