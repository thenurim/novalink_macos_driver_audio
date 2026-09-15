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
//  NovaLINKXPCProtocols.h
//  SharedSource
//
//  Copyright © 2016, 2017 Kyle Neideck
//

// Local Includes
#include "NovaLINK_Types.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

static NSString* kNovaLINKXPCHelperMachServiceName = @kNovaLINKXPCHelperBundleID;

// The protocol that NovaLINKXPCHelper will vend as its XPC API.
@protocol NovaLINKXPCHelperXPCProtocol

// Tells NovaLINKXPCHelper that the caller is NovaLINKApp and passes a listener endpoint that NovaLINKXPCHelper can use to create connections to NovaLINKApp.
// NovaLINKXPCHelper may also pass the endpoint on to NovaLINKDriver so it can do the same.
- (void) registerAsNovaLINKAppWithListenerEndpoint:(NSXPCListenerEndpoint*)endpoint reply:(void (^)(void))reply;
- (void) unregisterAsNovaLINKApp;

// NovaLINKDriver calls this remote method when it wants NovaLINKApp to start IO. NovaLINKXPCHelper passes the message along and then passes the response
// back. This allows NovaLINKDriver to wait for the audio hardware to start up, which means it can let the HAL know when it's safe to start
// sending us audio data from the client.
//
// If NovaLINKApp can be reached, the error it returns will be passed the reply block. Otherwise, the reply block will be passed an error with
// one of the kNovaLINKXPC_* error codes. It may have an underlying error using one of the NSXPCConnection* error codes from FoundationErrors.h.
- (void) startNovaLINKAppPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI;

// When the companion app is not registered, NovaLINKDriver asks NovaLINKXPCHelper to host playthrough itself,
// routing NovaLINKDevice to the system's current non-NovaLINK default output device.
- (void) startFallbackPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI;
- (void) stopFallbackPlayThrough;

// NovaLINKXPCHelper stores the last non-NovaLINK output device so that if fallback playthrough
// cannot be started after NovaLINKApp disconnects, it can restore that device as the OS default.
// Prefer keeping NovaLINK as default and hosting fallback playthrough instead.
- (void) setOutputDeviceToMakeDefaultOnAbnormalTermination:(AudioObjectID)deviceID;
    
@end


// The protocol that NovaLINKApp will vend as its XPC API.
@protocol NovaLINKAppXPCProtocol

- (void) startPlayThroughSyncWithReply:(void (^)(NSError*))reply forUISoundsDevice:(BOOL)isUI;

@end

#pragma clang assume_nonnull end

