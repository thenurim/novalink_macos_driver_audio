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
//  NovaLINKXPCHelperService.mm
//  NovaLINKXPCHelper
//
//  Copyright © 2016, 2017 Kyle Neideck
//

// Self Include
#import "NovaLINKXPCHelperService.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINKFallbackPlayThrough.h"
#import "NovaLINKXPCListenerDelegate.h"
#import "NovaLINKDevice.h"

// PublicUtility Includes
#import "CADebugMacros.h"


#pragma clang assume_nonnull begin

static const int DELAY_BEFORE_CLEANING_UP_FOR_NovaLINKAPP_SECS = 1;

static NSXPCListenerEndpoint* __nullable sNovaLINKAppEndpoint = nil;
static NSXPCConnection* __nullable sNovaLINKAppConnection = nil;

@implementation NovaLINKXPCHelperService {
    NSXPCConnection* connection;
    AudioObjectID outputDeviceToMakeDefaultOnAbnormalTermination;
}

- (id) initWithConnection:_connection {
    if ((self = [super init])) {
        connection = _connection;
        outputDeviceToMakeDefaultOnAbnormalTermination = kAudioObjectUnknown;
    }
    
    return self;
}

+ (NSError*) errorWithCode:(NSInteger)code description:(NSString*)description {
        return [NSError errorWithDomain:kNovaLINKXPCHelperMachServiceName
                                   code:code
                               userInfo:@{ NSLocalizedDescriptionKey: description }];
}

+ (NSError*) errorWithCode:(NSInteger)code description:(NSString*)description underlyingError:(NSError*)underlyingError {
        return [NSError errorWithDomain:kNovaLINKXPCHelperMachServiceName
                                   code:code
                               userInfo:@{ NSLocalizedDescriptionKey: description,
                                           NSUnderlyingErrorKey: underlyingError }];
}

+ (void) withNovaLINKAppRemoteProxy:(void (^)(id))block errorHandler:(void (^)(NSError* error))errorHandler {
    // Retry by default
    return [NovaLINKXPCHelperService withNovaLINKAppRemoteProxy:block errorHandler:errorHandler retryOnError:YES];
}

+ (void) withNovaLINKAppRemoteProxy:(void (^)(id))block errorHandler:(void (^)(NSError* error))errorHandler retryOnError:(BOOL)retry {
    // Wraps some error handling around a block that calls NovaLINKApp remotely, and runs it.
    
    if (!sNovaLINKAppConnection && sNovaLINKAppEndpoint) {
        // Create a new connection to NovaLINKApp from the endpoint
        @synchronized(self) {
            sNovaLINKAppConnection = [[NSXPCConnection alloc] initWithListenerEndpoint:(NSXPCListenerEndpoint* __nonnull)sNovaLINKAppEndpoint];
            NSAssert(sNovaLINKAppConnection, @"NSXPCConnection::initWithListenerEndpoint returned nil");
            
            [sNovaLINKAppConnection setRemoteObjectInterface:[NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKAppXPCProtocol)]];
            sNovaLINKAppConnection.invalidationHandler = ^{
                sNovaLINKAppConnection = nil;
            };
            
            [sNovaLINKAppConnection resume];
        }
    }
    
    if (sNovaLINKAppConnection) {
        id proxy = [sNovaLINKAppConnection remoteObjectProxyWithErrorHandler:^(NSError* error) {
            if (retry) {
                DebugMsg("NovaLINKXPCHelperService::withNovaLINKAppRemoteProxy: %s error=<%lu, %s>",
                         "Error sending message to NovaLINKApp. Creating new connection and retrying.",
                         [error code],
                         [[error localizedDescription] UTF8String]);
                
                // Clear the stored connection so a new one will be created when we retry. (The connection might still be
                // valid, but it's simpler to just make a new one every time.)
                @synchronized(self) {
                    sNovaLINKAppConnection = nil;
                }
                
                // Retry the message.
                [NovaLINKXPCHelperService withNovaLINKAppRemoteProxy:block errorHandler:errorHandler retryOnError:NO];
            } else {
                NSLog(@"NovaLINKXPCHelperService::withNovaLINKAppRemoteProxy: Error sending message to NovaLINKApp: %@", error);
                errorHandler(error);
            }
        }];
        
        block(proxy);
    } else {
        errorHandler([NovaLINKXPCHelperService errorWithCode:kNovaLINKXPC_MessageFailure description:@"No connection to NovaLINKApp"]);
    }
}

// Called after the connection from NovaLINKApp is invalidated. (If it's only been interrupted, launchd
// might restore it, so we wait for it to be invalidated.)
- (void) cleanUpForNovaLINKApp {
    // Wait a bit to see if NovaLINKApp reconnects. Then start Helper-hosted fallback playthrough so
    // NovaLINK remains usable without the companion app. Only unset NovaLINK as the OS default if
    // fallback cannot be started.
    int64_t delay = DELAY_BEFORE_CLEANING_UP_FOR_NovaLINKAPP_SECS * NSEC_PER_SEC;

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay),
                   dispatch_get_main_queue(),
                   ^{
                       [self startFallbackOrUnsetNovaLINKDeviceAsDefault];
                   });
}

- (void) startFallbackOrUnsetNovaLINKDeviceAsDefault {
    // Check that NovaLINKApp hasn't reconnected.
    if (sNovaLINKAppConnection) {
        DebugMsg("NovaLINKXPCHelperService::startFallbackOrUnsetNovaLINKDeviceAsDefault: "
                 "NovaLINKApp connected. Doing nothing.");
        return;
    }

    NSError* startError = [[NovaLINKFallbackPlayThrough sharedInstance] startForUISoundsDevice:NO];
    const NSInteger code = startError ? [startError code] : kNovaLINKXPC_InternalError;
    if (code == kNovaLINKXPC_Success || code == kNovaLINKXPC_ReturningEarlyError) {
        DebugMsg("NovaLINKXPCHelperService::startFallbackOrUnsetNovaLINKDeviceAsDefault: "
                 "Fallback playthrough started (code=%ld).", (long)code);
        // Also start UI-sounds instance so system sounds keep working.
        [[NovaLINKFallbackPlayThrough sharedInstance] startForUISoundsDevice:YES];
        return;
    }

    LogWarning("NovaLINKXPCHelperService::startFallbackOrUnsetNovaLINKDeviceAsDefault: "
               "Fallback failed (%s). Falling back to unsetting NovaLINK as default.",
               [[startError localizedDescription] UTF8String]);

    AudioObjectID outputDevice = outputDeviceToMakeDefaultOnAbnormalTermination;

    if (outputDevice == kAudioObjectUnknown) {
        DebugMsg("NovaLINKXPCHelperService::startFallbackOrUnsetNovaLINKDeviceAsDefault: "
                 "No device to set. Doing nothing.");
        return;
    }

    NovaLINKLogAndSwallowExceptions("NovaLINKXPCHelperService::unsetNovaLINKDeviceAsDefault", ([&] {
        NSLog(@"NovaLINKXPCHelperService::unsetNovaLINKDeviceAsDefault: Changing default device to %u",
              outputDevice);

        NovaLINKDevice().UnsetAsOSDefault(outputDevice);
    }));
}

#pragma mark Exported Methods

- (void) registerAsNovaLINKAppWithListenerEndpoint:(NSXPCListenerEndpoint*)endpoint reply:(void (^)(void))reply {
    [self debugWarnIfCalledByNovaLINKDriver];
    
    DebugMsg("NovaLINKXPCHelperService::registerAsNovaLINKAppWithListenerEndpoint: Received NovaLINKApp listener endpoint");

    // Hand playthrough ownership back to the companion app.
    [[NovaLINKFallbackPlayThrough sharedInstance] stop];
    
    // Store the connection (which we now know is from NovaLINKApp) and endpoint so all instances of this class can use them.
    @synchronized([self class]) {
        sNovaLINKAppEndpoint = endpoint;
        sNovaLINKAppConnection = connection;
        
        [sNovaLINKAppConnection setRemoteObjectInterface:[NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKAppXPCProtocol)]];
        
        // Set the stored connection back to nil when NovaLINKApp closes, dies or invalidates the connection.
        sNovaLINKAppConnection.interruptionHandler = ^{
            @synchronized([self class]) {
                sNovaLINKAppConnection = nil;
            }
        };

        sNovaLINKAppConnection.invalidationHandler = ^{
            @synchronized([self class]) {
                sNovaLINKAppConnection = nil;
                [self cleanUpForNovaLINKApp];
            }
        };
    }
    
    reply();
}

- (void) unregisterAsNovaLINKApp {
    [self debugWarnIfCalledByNovaLINKDriver];
    
    DebugMsg("NovaLINKXPCHelperService::unregisterAsNovaLINKApp: Destroying connection to NovaLINKApp");
    
    // TODO: We don't want to assume only one instance of NovaLINKApp will be running, in case multiple users are running it.
    @synchronized([self class]) {
        if (sNovaLINKAppConnection) {
            [sNovaLINKAppConnection invalidate];
            sNovaLINKAppConnection = nil;
            sNovaLINKAppEndpoint = nil;
        }
    }
}

- (void) startNovaLINKAppPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI {
    [self debugWarnIfCalledByNovaLINKApp];
    
    // If this reply string isn't set before the end of this method, it's a bug
    __block NSError* replyToNovaLINKDriver = [NovaLINKXPCHelperService errorWithCode:kNovaLINKXPC_InternalError
                                                               description:@"Reply not set in startNovaLINKAppPlayThroughSyncWithReply"];
    
    // I couldn't find the Obj-C equivalent of xpc_connection_send_message_with_reply_sync so just wait on this
    // semaphore until we get a reply from NovaLINKApp (or timeout). Note that ARC handles dispatch semaphores.
    dispatch_semaphore_t novaLINKAppReplySemaphore = dispatch_semaphore_create(0);
    
    DebugMsg("NovaLINKXPCHelperService::startNovaLINKAppPlayThroughSyncWithReply: Waiting for NovaLINKApp to start IO on the output device");
    
    // Send the message to NovaLINKApp
    [NovaLINKXPCHelperService withNovaLINKAppRemoteProxy:^(id remoteObjectProxy) {
        [remoteObjectProxy startPlayThroughSyncWithReply:^(NSError* novaLINKAppReply) {
            replyToNovaLINKDriver = novaLINKAppReply;
            dispatch_semaphore_signal(novaLINKAppReplySemaphore);
        } forUISoundsDevice:isUI];
    } errorHandler:^(NSError* error) {
        replyToNovaLINKDriver = [NovaLINKXPCHelperService errorWithCode:kNovaLINKXPC_MessageFailure
                                                  description:[error localizedDescription]
                                              underlyingError:error];
        dispatch_semaphore_signal(novaLINKAppReplySemaphore);
    }];
    
    // Wait for NovaLINKApp's reply
    long err = dispatch_semaphore_wait(novaLINKAppReplySemaphore, dispatch_time(DISPATCH_TIME_NOW, kStartIOTimeoutNsec));
    
    if (err != 0) {
        replyToNovaLINKDriver = [NovaLINKXPCHelperService errorWithCode:kNovaLINKXPC_Timeout
                                                  description:@"Timed out waiting for NovaLINKApp"];
    }

    // Return the reply to NovaLINKDriver
    DebugMsg("NovaLINKXPCHelperService::startNovaLINKAppPlayThroughSyncWithReply: Reply to NovaLINKDriver: %s",
             [[replyToNovaLINKDriver localizedDescription] UTF8String]);
    reply(replyToNovaLINKDriver);
}

- (void) startFallbackPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI {
    [self debugWarnIfCalledByNovaLINKApp];

    // Prefer the companion app when it is connected.
    if (sNovaLINKAppConnection) {
        DebugMsg("NovaLINKXPCHelperService::startFallbackPlayThroughSyncWithReply: "
                 "NovaLINKApp is connected; forwarding to app playthrough.");
        [self startNovaLINKAppPlayThroughSyncWithReply:reply forUISoundsDevice:isUI];
        return;
    }

    DebugMsg("NovaLINKXPCHelperService::startFallbackPlayThroughSyncWithReply: "
             "Starting Helper-hosted fallback playthrough (ui=%d)", isUI);
    NSError* startError = [[NovaLINKFallbackPlayThrough sharedInstance] startForUISoundsDevice:isUI];
    reply(startError);
}

- (void) stopFallbackPlayThrough {
    [self debugWarnIfCalledByNovaLINKApp];
    [[NovaLINKFallbackPlayThrough sharedInstance] stop];
}

- (void) setOutputDeviceToMakeDefaultOnAbnormalTermination:(AudioObjectID)deviceID {
    outputDeviceToMakeDefaultOnAbnormalTermination = deviceID;
    DebugMsg("NovaLINKXPCHelperService::setOutputDeviceToMakeDefaultOnAbnormalTermination: ID set to %u",
             deviceID);
}

#pragma mark Debug Utils

- (void) debugWarnIfCalledByNovaLINKApp {
#if DEBUG
    if ([connection effectiveUserIdentifier] != [NovaLINKXPCListenerDelegate _coreaudiodUID]) {
        DebugMsg("NovaLINKXPCHelperService::debugWarnIfCalledByNovaLINKDriver: A method intended for NovaLINKDriver only was (probably) called "
                 "by NovaLINKApp. Or it could have just been the tests.");
        NSLog(@"%@", [NSThread callStackSymbols]);
    }
#endif
}

- (void) debugWarnIfCalledByNovaLINKDriver {
#if DEBUG
    if ([connection effectiveUserIdentifier] == [NovaLINKXPCListenerDelegate _coreaudiodUID]) {
        DebugMsg("NovaLINKXPCHelperService::debugWarnIfCalledByNovaLINKDriver: A method intended for NovaLINKApp only was (probably) called by NovaLINKDriver");
        NSLog(@"%@", [NSThread callStackSymbols]);
    }
#endif
}

@end

#pragma clang assume_nonnull end

