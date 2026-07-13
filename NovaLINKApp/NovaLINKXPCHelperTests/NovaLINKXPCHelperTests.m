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
//  NovaLINKXPCHelperTests.m
//  NovaLINKXPCHelperTests
//
//  Copyright © 2016 Kyle Neideck
//

// Local Includes
#import "NovaLINK_TestUtils.h"
#import "NovaLINKXPCProtocols.h"

// System Includes
#import <Foundation/Foundation.h>


#pragma clang assume_nonnull begin

// To run these tests, NovaLINKXPCHelper has to be installed and its launchd job enabled.

@interface NovaLINKXPCHelperTests : XCTestCase
@end

@implementation NovaLINKXPCHelperTests {
    NSXPCConnection* connection;
}

- (void) setUp {
    [super setUp];
    
    connection = [[NSXPCConnection alloc] initWithMachServiceName:kNovaLINKXPCHelperMachServiceName
                                                          options:NSXPCConnectionPrivileged];
    
    connection.remoteObjectInterface = [NSXPCInterface interfaceWithProtocol:@protocol(NovaLINKXPCHelperXPCProtocol)];
    [connection resume];
}

- (void) tearDown {
    [connection invalidate];
    
    [super tearDown];
}

- (void) testStartOutputDeviceWithoutNovaLINKAppConnected {
    dispatch_semaphore_t replySemaphore = dispatch_semaphore_create(0);

    // Unregister NovaLINKXPCHelper's connection to NovaLINKApp in case NovaLINKApp didn't shutdown cleanly the last time it ran.
    [[connection remoteObjectProxy] unregisterAsNovaLINKApp];
        
    [[connection remoteObjectProxy] startNovaLINKAppPlayThroughSyncWithReply:^(NSError* reply) {
        XCTAssertEqual([reply code],
                       kNovaLINKXPC_MessageFailure,
                       @"Check that NovaLINKApp isn't running, which would cause this failure");
        
        dispatch_semaphore_signal(replySemaphore);
    } forUISoundsDevice:NO];

    // Very long timeout to make it less likely to fail in CI builds when there's high contention.
    if (0 != dispatch_semaphore_wait(replySemaphore, dispatch_time(DISPATCH_TIME_NOW, 5 * 60 * NSEC_PER_SEC))) {
        XCTFail(@"Timed out waiting for NovaLINKXPCHelper");
    }
}

@end

#pragma clang assume_nonnull end

