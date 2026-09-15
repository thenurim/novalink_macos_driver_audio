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
//  NovaLINK_XPCHelper.m
//  NovaLINKDriver
//
//  Copyright © 2016, 2017, 2020, 2024 Kyle Neideck
//  Copyright © 2020 Aleksey Yurkevich
//

// Self Include
#import "NovaLINK_XPCHelper.h"

// Local Includes
#import "NovaLINKXPCProtocols.h"

// PublicUtility Includes
#include "CADebugMacros.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

static const UInt64 REMOTE_CALL_DEFAULT_TIMEOUT_SECS = 30;

static NSXPCConnection* CreateXPCHelperConnection(void)
{
    // Create a connection to NovaLINKXPCHelper's Mach service. If it isn't already running, launchd will start NovaLINKXPCHelper when we send
    // a message to this connection.
    //
    // Uses the NSXPCConnectionPrivileged option because NovaLINKXPCHelper has to run in the privileged/global bootstrap context for
    // NovaLINKDriver to be able to look it up. NovaLINKDriver runs in the coreaudiod process, which runs in the global context, and services
    // in the global context are only able to look up other services in that context.
    NSXPCConnection* theConnection = [[NSXPCConnection alloc] initWithMachServiceName:kNovaLINKXPCHelperMachServiceName
                                                                              options:NSXPCConnectionPrivileged];
    
    if (theConnection) {
        theConnection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKXPCHelperXPCProtocol)];
        [theConnection resume];
    } else {
        @throw(@"NovaLINK_XPCHelper::CreateXPCHelperConnection: initWithMachServiceName returned nil");
    }
    
    return theConnection;
}

UInt64 StartNovaLINKAppPlayThroughSync(bool inIsForUISoundsDevice)
{
    __block UInt64 theAnswer = kNovaLINKXPC_Success;
    
    // Connect to our XPC helper.
    //
    // We can't initiate an XPC connection with NovaLINKApp directly for security reasons, so we use NovaLINKXPCHelper as an intermediary. (We
    // could use NovaLINKXPCHelper to initiate the connection and then talk to NovaLINKApp directly, but so far we haven't had any reason to.)
    //
    // It would be faster to keep the connection ready whenever NovaLINKApp is a client of NovaLINKDevice, but it's not important for this case.
    NSXPCConnection* theConnection = CreateXPCHelperConnection();
    
    // This semaphore will be signalled when we get a reply from NovaLINKXPCHelper, or the message fails.
    dispatch_semaphore_t theReplySemaphore = dispatch_semaphore_create(0);
   
    // Set the failure callbacks to signal the reply semaphore so we can return immediately if NovaLINKXPCHelper can't be reached. (It
    // doesn't matter how many times we signal the reply semaphore because we create a new one each time.)
    void (^failureHandler)(void) = ^{
        DebugMsg("NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Connection to NovaLINKXPCHelper failed");
        
        theAnswer = kNovaLINKXPC_MessageFailure;
        dispatch_semaphore_signal(theReplySemaphore);
    };
    theConnection.interruptionHandler = failureHandler;
    theConnection.invalidationHandler = failureHandler;
    
    // This remote call to NovaLINKXPCHelper will send a reply when the output device is ready to receive IO. Note that, for security
    // reasons, we shouldn't trust the reply object.
    [[theConnection remoteObjectProxyWithErrorHandler:^(NSError* error) {
        (void)error;
        DebugMsg("NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Remote call error: %s",
                 [[error debugDescription] UTF8String]);
        
        failureHandler();
    }] startNovaLINKAppPlayThroughSyncWithReply:^(NSError* reply) {
        DebugMsg("NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Got reply from NovaLINKXPCHelper: \"%s\"",
                 [[reply localizedDescription] UTF8String]);
        
        theAnswer = kNovaLINKXPC_MessageFailure;

        @try {
            if (reply)
            {
                theAnswer = (UInt64)[reply code];
            }
        } @catch(...) {
            NSLog(@"NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Exception while reading reply code");
        }
        
        // We only need the connection for one call, which was successful, so the losing the connection is no longer a problem.
        theConnection.interruptionHandler = nil;
        theConnection.invalidationHandler = nil;
        
        // Tell the enclosing function it can return now.
        dispatch_semaphore_signal(theReplySemaphore);
    } forUISoundsDevice:inIsForUISoundsDevice];
    
    DebugMsg("NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Waiting for NovaLINKApp to tell us the output device is ready for IO");
    
    // Wait on the reply semaphore until we get the reply (or a connection failure).
    if (0 != dispatch_semaphore_wait(theReplySemaphore,
                                     dispatch_time(DISPATCH_TIME_NOW, REMOTE_CALL_DEFAULT_TIMEOUT_SECS * NSEC_PER_SEC))) {
        // Log a warning if we timeout.
        //
        // TODO: It's possible that the output device is just taking a really long time to start. Is there some way we could check for
        //       that, rather than timing out?
        NSLog(@"NovaLINK_XPCHelper::StartNovaLINKAppPlayThroughSync: Timed out waiting for the NovaLINK app to start the output device");
        
        theAnswer = kNovaLINKXPC_Timeout;
    }
    
   [theConnection invalidate];
    
    return theAnswer;
}

UInt64 StartFallbackPlayThroughSync(bool inIsForUISoundsDevice)
{
    __block UInt64 theAnswer = kNovaLINKXPC_Success;

    NSXPCConnection* theConnection = CreateXPCHelperConnection();
    dispatch_semaphore_t theReplySemaphore = dispatch_semaphore_create(0);

    void (^failureHandler)(void) = ^{
        DebugMsg("NovaLINK_XPCHelper::StartFallbackPlayThroughSync: Connection to NovaLINKXPCHelper failed");
        theAnswer = kNovaLINKXPC_MessageFailure;
        dispatch_semaphore_signal(theReplySemaphore);
    };
    theConnection.interruptionHandler = failureHandler;
    theConnection.invalidationHandler = failureHandler;

    [[theConnection remoteObjectProxyWithErrorHandler:^(NSError* error) {
        (void)error;
        DebugMsg("NovaLINK_XPCHelper::StartFallbackPlayThroughSync: Remote call error: %s",
                 [[error debugDescription] UTF8String]);
        failureHandler();
    }] startFallbackPlayThroughSyncWithReply:^(NSError* reply) {
        DebugMsg("NovaLINK_XPCHelper::StartFallbackPlayThroughSync: Got reply: \"%s\"",
                 [[reply localizedDescription] UTF8String]);

        theAnswer = kNovaLINKXPC_MessageFailure;
        @try {
            if (reply)
            {
                theAnswer = (UInt64)[reply code];
            }
        } @catch(...) {
            NSLog(@"NovaLINK_XPCHelper::StartFallbackPlayThroughSync: Exception while reading reply code");
        }

        theConnection.interruptionHandler = nil;
        theConnection.invalidationHandler = nil;
        dispatch_semaphore_signal(theReplySemaphore);
    } forUISoundsDevice:inIsForUISoundsDevice];

    if (0 != dispatch_semaphore_wait(theReplySemaphore,
                                     dispatch_time(DISPATCH_TIME_NOW, REMOTE_CALL_DEFAULT_TIMEOUT_SECS * NSEC_PER_SEC))) {
        NSLog(@"NovaLINK_XPCHelper::StartFallbackPlayThroughSync: Timed out waiting for fallback playthrough");
        theAnswer = kNovaLINKXPC_Timeout;
    }

    [theConnection invalidate];
    return theAnswer;
}

#pragma clang assume_nonnull end

