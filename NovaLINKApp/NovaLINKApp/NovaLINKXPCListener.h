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
//  NovaLINKXPCListener.h
//  NovaLINKApp
//
//  Copyright © 2016 Kyle Neideck
//
//  Connects to NovaLINKXPCHelper via XPC. When NovaLINKDriver wants NovaLINKApp to do something it can call one of NovaLINKHelper's
//  XPC methods, which passes the request along to this class.
//

// Local Includes
#import "NovaLINKAudioDeviceManager.h"
#import "NovaLINKXPCProtocols.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKXPCListener : NSObject <NovaLINKAppXPCProtocol, NSXPCListenerDelegate>

- (id) initWithAudioDevices:(NovaLINKAudioDeviceManager*)devices helperConnectionErrorHandler:(void (^)(NSError* error))errorHandler;

- (void) initHelperConnection;

@end

#pragma clang assume_nonnull end

