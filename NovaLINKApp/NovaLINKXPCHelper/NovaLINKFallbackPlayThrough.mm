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

// Self Include
#import "NovaLINKFallbackPlayThrough.h"

// Local Includes
#import "NovaLINKPlayThrough.h"
#import "NovaLINKDevice.h"
#import "NovaLINKAudioDevice.h"
#import "NovaLINK_Types.h"
#import "NovaLINK_Utils.h"
#import "NovaLINKXPCProtocols.h"

// PublicUtility Includes
#import "CADebugMacros.h"
#import "CAHALAudioSystemObject.h"

// STL Includes
#include <vector>


#pragma clang assume_nonnull begin

@implementation NovaLINKFallbackPlayThrough {
    NSObject* _lock;
    NovaLINKPlayThrough _playThrough;
    NovaLINKPlayThrough _playThroughUISounds;
    BOOL _devicesConfigured;
    AudioObjectID _configuredOutputDeviceID;
}

+ (instancetype) sharedInstance {
    static NovaLINKFallbackPlayThrough* instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[NovaLINKFallbackPlayThrough alloc] init];
    });
    return instance;
}

- (instancetype) init {
    if ((self = [super init])) {
        _lock = [NSObject new];
        _devicesConfigured = NO;
        _configuredOutputDeviceID = kAudioObjectUnknown;
    }
    return self;
}

+ (NSError*) errorWithCode:(NSInteger)code description:(NSString*)description {
    return [NSError errorWithDomain:kNovaLINKXPCHelperMachServiceName
                               code:code
                           userInfo:@{ NSLocalizedDescriptionKey: description }];
}

// Prefer the OS default output when it is a real device; otherwise system-default, then lowest latency.
+ (AudioObjectID) resolveRealOutputDeviceID {
    AudioObjectID outputDevice = kAudioObjectUnknown;
    CAHALAudioSystemObject audioSystem;

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKAudioDevice defaultDevice = audioSystem.GetDefaultAudioDevice(false, false);
        if (defaultDevice.GetObjectID() != kAudioObjectUnknown &&
            defaultDevice.CanBeOutputDeviceInNovaLINKApp()) {
            outputDevice = defaultDevice.GetObjectID();
        }
    });

    if (outputDevice == kAudioObjectUnknown) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            NovaLINKAudioDevice systemDefault = audioSystem.GetDefaultAudioDevice(false, true);
            if (systemDefault.GetObjectID() != kAudioObjectUnknown &&
                systemDefault.CanBeOutputDeviceInNovaLINKApp()) {
                outputDevice = systemDefault.GetObjectID();
            }
        });
    }

    if (outputDevice == kAudioObjectUnknown) {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            UInt32 numDevices = audioSystem.GetNumberAudioDevices();
            std::vector<AudioObjectID> devices(numDevices);
            audioSystem.GetAudioDevices(numDevices, devices.data());

            UInt32 minLatency = UINT32_MAX;
            for (UInt32 i = 0; i < numDevices; i++) {
                NovaLINKAudioDevice device(devices[i]);
                if (!device.CanBeOutputDeviceInNovaLINKApp()) {
                    continue;
                }
                UInt32 latency = device.GetLatency(false);
                if (latency < minLatency) {
                    minLatency = latency;
                    outputDevice = devices[i];
                }
            }
        });
    }

    return outputDevice;
}

- (BOOL) configureDevicesWithOutput:(AudioObjectID)outputDeviceID
                              error:(NSError* __autoreleasing *)outError {
    if (outputDeviceID == kAudioObjectUnknown) {
        if (outError) {
            *outError = [NovaLINKFallbackPlayThrough errorWithCode:kNovaLINKXPC_HardwareError
                                                       description:@"No real output device for fallback playthrough"];
        }
        return NO;
    }

    if (_devicesConfigured && _configuredOutputDeviceID == outputDeviceID) {
        return YES;
    }

    DebugMsg("NovaLINKFallbackPlayThrough::configureDevicesWithOutput: Using output device %u",
             outputDeviceID);

    BOOL ok = NO;

    NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
        NovaLINKDevice novaLINKDevice;
        NovaLINKAudioDevice outputDevice(outputDeviceID);

        _playThrough.SetDevices(&novaLINKDevice, &outputDevice);
        _playThrough.Activate();

        NovaLINKAudioDevice uiSoundsInput = novaLINKDevice.GetUISoundsNovaLINKDeviceInstance();
        _playThroughUISounds.SetDevices(&uiSoundsInput, &outputDevice);
        _playThroughUISounds.Activate();

        _devicesConfigured = YES;
        _configuredOutputDeviceID = outputDeviceID;
        ok = YES;
    });

    // Hardware-mic inject belongs only in the passthrough app (stable TCC identity).
    // XPCHelper must not open AVCapture / mic IO — that spams a second unbound prompt.

    if (!ok && outError) {
        *outError = [NovaLINKFallbackPlayThrough errorWithCode:kNovaLINKXPC_HardwareError
                                                   description:@"Failed to configure fallback playthrough devices"];
    }
    return ok;
}

- (NSError*) startForUISoundsDevice:(BOOL)isUI {
    @synchronized (_lock) {
        AudioObjectID outputDeviceID = [NovaLINKFallbackPlayThrough resolveRealOutputDeviceID];
        NSError* configureError = nil;
        if (![self configureDevicesWithOutput:outputDeviceID error:&configureError]) {
            return configureError ?:
                [NovaLINKFallbackPlayThrough errorWithCode:kNovaLINKXPC_HardwareError
                                               description:@"Fallback playthrough configure failed"];
        }

        // Mirror NovaLINKApp's macOS 11+ path: return early and start asynchronously so the
        // driver's StartIO is never blocked waiting on real hardware (esp. Bluetooth).
        constexpr int64_t kStartPlayThroughDeferNsec = 50 * NSEC_PER_MSEC;
        const BOOL forUI = isUI;

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, kStartPlayThroughDeferNsec),
                       dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
            @synchronized (self->_lock) {
                if (!self->_devicesConfigured) {
                    return;
                }
                NovaLINKPlayThrough& pt =
                        (forUI ? self->_playThroughUISounds : self->_playThrough);
                NovaLINKLogAndSwallowExceptionsMsg("NovaLINKFallbackPlayThrough::start",
                                              "Starting fallback playthrough", [&] {
                    pt.Start();
                });
            }
        });

        return [NovaLINKFallbackPlayThrough errorWithCode:kNovaLINKXPC_ReturningEarlyError
                                              description:@"Starting fallback playthrough asynchronously"];
    }
}

- (void) stop {
    @synchronized (_lock) {
        if (!_devicesConfigured) {
            return;
        }

        DebugMsg("NovaLINKFallbackPlayThrough::stop: Stopping fallback playthrough");

        NovaLINKLogAndSwallowExceptions("NovaLINKFallbackPlayThrough::stop", [&] {
            _playThrough.Stop();
            _playThroughUISounds.Stop();
            _playThrough.Deactivate();
            _playThroughUISounds.Deactivate();
        });

        _devicesConfigured = NO;
        _configuredOutputDeviceID = kAudioObjectUnknown;
    }
}

@end

#pragma clang assume_nonnull end
