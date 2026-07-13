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
//  NovaLINKXPCListenerDelegate.h
//  NovaLINKXPCHelper
//
//  Copyright © 2016 Kyle Neideck
//

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

@interface NovaLINKXPCListenerDelegate : NSObject <NSXPCListenerDelegate>

// The UID of the _coreaudiod user, which NovaLINKDriver runs under. This is used in debug builds, usually to warn if a remote method is
// called by NovaLINKApp when it's only meant to be called by NovaLINKDriver, or vice versa.
+ (uid_t) _coreaudiodUID;

- (BOOL) listener:(NSXPCListener*)listener shouldAcceptNewConnection:(NSXPCConnection*)newConnection;
    
@end

#pragma clang assume_nonnull end

