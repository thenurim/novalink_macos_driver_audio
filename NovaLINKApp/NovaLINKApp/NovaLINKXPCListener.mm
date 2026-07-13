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
//  NovaLINKXPCListener.mm
//  NovaLINKApp
//
//  Copyright © 2016, 2017 Kyle Neideck
//

// Self Include
#import "NovaLINKXPCListener.h"

// Local Includes
#import "NovaLINKPlayThrough.h"  // For kDeviceNotStarting.


#pragma clang assume_nonnull begin

@implementation NovaLINKXPCListener {
    NSXPCListener* listener;
    // The connection to NovaLINKXPCHelper. We keep the connection alive so if NovaLINKXPCHelper is killed or crashes our interruptionHandler
    // runs and we can re-register our listener endpoint. Otherwise NovaLINKXPCHelper would have no way to initiate connections to NovaLINKApp.
    // (That's because NovaLINKXPCHelper is in the global bootstrap context, so it can't look NovaLINKApp up, and XPC services are supposed to
    // be as stateless as possible, so when it restarts it doesn't restore its copy of NovaLINKApp's listener endpoint.)
    NSXPCConnection* __nullable helperConnection;
    NovaLINKAudioDeviceManager* audioDevices;
    // Used to regularly try reconnecting to NovaLINKXPCHelper if the connection has failed.
    NSTimer* __nullable retryTimer;
}

- (id) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices helperConnectionErrorHandler:(void (^)(NSError* error))errorHandler {
    if ((self = [super init])) {
        audioDevices = devices;
        
        // Create NovaLINKApp's local listener.
        listener = [NSXPCListener anonymousListener];
        
        // Set this class as the listener's delegate so our listener:shouldAcceptNewConnection method handles incoming connections.
        listener.delegate = self;
        [listener resume];
        
        // Set up the connection to NovaLINKXPCHelper.
        [self initHelperConnectionWithErrorHandler:errorHandler];

        // Pass the connection to the audio device manager so it can tell NovaLINKXPCHelper the output device's ID.
        [audioDevices setNovaLINKXPCHelperConnection:helperConnection];
    }
    
    return self;
}

- (void) initHelperConnection {
    // Note that this is called when the helper connection's interruption handler is retrying the connection after a timeout.
    [self initHelperConnectionWithErrorHandler:^(NSError* error) {
#if !DEBUG
    #pragma unused (error)
#endif
        
        DebugMsg("NovaLINKXPCListener::initHelperConnection: Connection error: %s", [[error description] UTF8String]);
    }];
}

- (void) initHelperConnectionWithErrorHandler:(void (^)(NSError* error))errorHandler {
    // Connect to the helper service so we can call its remote methods (and vice versa).
    
    // Clean up if there's an existing connection.
    if (helperConnection) {
        [helperConnection invalidate];
        helperConnection = nil;
    }
    
    // Uses the NSXPCConnectionPrivileged option because NovaLINKXPCHelper has to run in the privileged/global bootstrap context for
    // NovaLINKDriver to be able to look it up. NovaLINKDriver runs in the coreaudiod process, which runs in the global context, and services
    // in the global context are only able to look up other services in that context.
    helperConnection = [[NSXPCConnection alloc] initWithMachServiceName:kNovaLINKXPCHelperMachServiceName
                                                                options:NSXPCConnectionPrivileged];
    
    helperConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKXPCHelperXPCProtocol)];
    
    // Export this object so NovaLINKXPCHelper can send messages through helperConnection if it wants to.
    helperConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKAppXPCProtocol)];
    helperConnection.exportedObject = self;
    
    // If we lose the connection to NovaLINKXPCHelper, we try to reconnect every 10 seconds.
    void (^retryAfterTimeout)(NSError*) = ^(NSError* error) {
        dispatch_async(dispatch_get_main_queue(), ^{
            NSLog(@"NovaLINKXPCListener::initHelperConnectionWithErrorHandler: Lost connection to NovaLINKXPCHelper. Will try to reconnect "
                  "every 10 seconds. Details: %@",
                  error);
            
            if (!retryTimer) {
                retryTimer = [NSTimer scheduledTimerWithTimeInterval:10.0
                                                              target:self
                                                            selector:@selector(initHelperConnection)
                                                            userInfo:nil
                                                             repeats:YES];
            }
        });
    };
    
    // If the XPC helper crashes or is killed, this handler gets invoked and attempts to reestablish the connection by sending
    // a message.
    NSXPCConnection* __weak helperConnectionWeakRef = helperConnection;
    NSXPCListener* __weak listenerWeakRef = listener;
    helperConnection.interruptionHandler = ^{
        DebugMsg("NovaLINKXPCListener::initHelperConnectionWithErrorHandler: Connection to NovaLINKXPCHelper interrupted. Trying to reconnect.");
        [[helperConnectionWeakRef remoteObjectProxyWithErrorHandler:retryAfterTimeout]
         registerAsNovaLINKAppWithListenerEndpoint:[listenerWeakRef endpoint] reply:^{}];
    };
    
    // Use the initialization error handler while we send the initial message. This is so we can warn the user, since failing at
    // start up probably means we won't be able to recover.
    helperConnection.invalidationHandler = ^{
        errorHandler([NSError errorWithDomain:@kNovaLINKAppBundleID
                                         code:kNovaLINKXPC_MessageFailure
                                     userInfo:@{ NSLocalizedDescriptionKey: @"Failed to send initial message to NovaLINKXPCHelper." }]);
    };
    
    [helperConnection resume];
        
    // Send the listener's endpoint to NovaLINKXPCHelper so it can initiate connections to NovaLINKApp.
    [[helperConnection remoteObjectProxyWithErrorHandler:errorHandler] registerAsNovaLINKAppWithListenerEndpoint:[listener endpoint] reply:^{
        // If we were retrying the connection, we can stop now.
        if (retryTimer) {
            NSLog(@"NovaLINKXPCListener::initHelperConnectionWithErrorHandler: Reconnected to NovaLINKXPCHelper.");
            [retryTimer invalidate];
            retryTimer = nil;
        }
        
        // Silently retry the connection, after a timeout, if it fails after having worked initially.
        helperConnection.invalidationHandler = ^{
            DebugMsg("NovaLINKXPCListener::initHelperConnectionWithErrorHandler: Connection to NovaLINKXPCHelper invalidated");
            helperConnection = nil;
            retryAfterTimeout(nil);
        };
    }];

    // Pass the new connection to the audio device manager.
    [audioDevices setNovaLINKXPCHelperConnection:helperConnection];
}

- (void) dealloc {
    if (retryTimer) {
        [retryTimer invalidate];
    }
    
    [[helperConnection remoteObjectProxy] unregisterAsNovaLINKApp];
}

- (BOOL) listener:(NSXPCListener*)listener shouldAcceptNewConnection:(NSXPCConnection*)newConnection {
    #pragma unused (listener)
    
    // This method is where the NSXPCListener configures, accepts, and resumes a new incoming NSXPCConnection.
    
    DebugMsg("NovaLINKXPCListener::listener: Received new connection");
    
    // Configure the connection.
    // First, set the interface that the exported object implements.
    newConnection.exportedInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKAppXPCProtocol)];
    
    // Next, set the object that the connection exports. All messages sent on the connection to this service will be sent to
    // the exported object to handle. The connection retains the exported object.
    newConnection.exportedObject = self;
    
    // Resuming the connection allows the system to deliver more incoming messages.
    [newConnection resume];
    
    // Returning YES from this method tells the system that you have accepted this connection. If you want to reject the
    // connection for some reason, call -invalidate on the connection and return NO.
    return YES;
}

- (void) startPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI {
    NSString* description;
    OSStatus err;
    
    try {
        err = [audioDevices startPlayThroughSync:isUI];
    } catch (CAException e) {
        // startPlayThroughSync should never throw a CAException, but check anyway in case we change that at some point.
        LogError("NovaLINKXPCListener::startPlayThroughSyncWithReply: Caught CAException (%d). Replying kNovaLINKXPC_HardwareError.",
                 e.GetError());
        err = kNovaLINKXPC_HardwareError;
    } catch (...) {
        LogError("NovaLINKXPCListener::startPlayThroughSyncWithReply: Caught unknown exception. Replying kNovaLINKXPC_InternalError.");
        err = kNovaLINKXPC_InternalError;
#if DEBUG
        throw;
#endif
    }
    
    switch (err) {
        case kAudioHardwareNoError:
            description = @"NovaLINKApp started the output device.";
            err = kNovaLINKXPC_Success;
            break;
            
        case kAudioHardwareNotRunningError:
            description = @"NovaLINKApp is not ready for audio play-through.";
            err = kNovaLINKXPC_NovaLINKAppStateError;
            break;
            
        case kAudioHardwareIllegalOperationError:
            description = @"The output device is not available.";
            err = kNovaLINKXPC_HardwareError;
            break;
            
        case kNovaLINKErrorCode_ReturningEarly:
            // We have to send a more specific error in this case because NovaLINKDevice handles this case differently.
            description = @"NovaLINKApp could not wait for the output device to be ready for IO.";
            err = kNovaLINKXPC_ReturningEarlyError;
            break;
            
        default:
            description = @"Unknown error while waiting for the output device.";
            err = kNovaLINKXPC_InternalError;
            break;
    }
    
    reply([NSError errorWithDomain:@kNovaLINKAppBundleID
                              code:err
                          userInfo:@{ NSLocalizedDescriptionKey: description }]);
}

@end

#pragma clang assume_nonnull end

