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
//  NovaLINK_ClientMapTests.mm
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

// Unit Include
#include "NovaLINK_ClientMap.h"

// Local Includes
#include "NovaLINK_TestUtils.h"

// NovaLINKDriver Includes
#include "NovaLINK_Client.h"
#include "NovaLINK_TaskQueue.h"
#include "NovaLINK_Types.h"


static NovaLINK_TaskQueue taskQueue;

static const AudioServerPlugInClientInfo client1Info = {
    /* mClientID = */ 1,
    /* mProcessID = */ 2291,
    /* mIsNativeEndian = */ true,
    /* mBundleID = */ CFSTR("com.example.background.music.client.one")
};

static const AudioServerPlugInClientInfo client2Info = {
    /* mClientID = */ 921,
    /* mProcessID = */ 64372,
    /* mIsNativeEndian = */ true,
    /* mBundleID = */ CFSTR("com.example.background.music.client.two")
};

static NovaLINK_Client client1(&client1Info);
static NovaLINK_Client client2(&client2Info);

@interface NovaLINK_ClientMapTests : XCTestCase

@end

@implementation NovaLINK_ClientMapTests

- (void)setUp {
    [super setUp];
    
    client1.mRelativeVolume = 0.625;
    client2.mIsMusicPlayer = true;
}

- (void)tearDown {
    [super tearDown];
}

// Asserts that in client the fields that come from AudioServerPlugInClientInfo are equal to the
// corresponding fields in info.
//
// Requires that the mBundleID fields of both are either NULL or have not been released.
+ (void)assertClient:(const NovaLINK_Client*)client
      hasInfoEqualTo:(const AudioServerPlugInClientInfo*)info {
    XCTAssertEqual(client->mClientID, info->mClientID);
    XCTAssertEqual(client->mProcessID, info->mProcessID);
    XCTAssertEqual(client->mIsNativeEndian, info->mIsNativeEndian);
    
    if (!client->mBundleID.IsValid() || info->mBundleID == NULL) {
        XCTAssert(!client->mBundleID.IsValid());
        XCTAssert(info->mBundleID == NULL);
    } else {
        XCTAssert(client->mBundleID == info->mBundleID);
    }
}

// Requires that the mBundleID fields of both clients are either NULL or have not been released.
+ (void)assertClient:(const NovaLINK_Client*)c1
           isEqualTo:(const NovaLINK_Client*)c2 {
    const AudioServerPlugInClientInfo info =
        { c2->mClientID, c2->mProcessID, c2->mIsNativeEndian, c2->mBundleID.GetCFString() };
    
    [NovaLINK_ClientMapTests assertClient:c1 hasInfoEqualTo:&info];
    
    XCTAssertEqual(c1->mDoingIO, c2->mDoingIO);
    XCTAssertEqual(c1->mIsMusicPlayer, c2->mIsMusicPlayer);
    XCTAssertEqual(c1->mRelativeVolume, c2->mRelativeVolume);
}

- (void)testClientConstruction {
    // Check that the NovaLINK_Client instances we're testing with match the AudioServerPlugInClientInfos
    // they were constructed from.
    //
    // TODO: This should be in a NovaLINK_ClientTests class rather than here.
    [NovaLINK_ClientMapTests assertClient:&client1 hasInfoEqualTo:&client1Info];
    [NovaLINK_ClientMapTests assertClient:&client2 hasInfoEqualTo:&client2Info];
}

- (void)testAddRemoveClient {
    NovaLINK_ClientMap clientMap(&taskQueue);
    
    // Add a client
    clientMap.AddClient(client1);
    
    // Get the client back out of the map
    NovaLINK_Client retrievedClient;
    bool didGetClient = clientMap.GetClientNonRT(client1.mClientID, &retrievedClient);
    XCTAssert(didGetClient);
    
    // Compare the client we added to the one we got back
    [NovaLINK_ClientMapTests assertClient:&retrievedClient isEqualTo:&client1];
    [NovaLINK_ClientMapTests assertClient:&retrievedClient hasInfoEqualTo:&client1Info];
    
    // A client to use as the out argument for GetClientNonRT when we don't expect to get a client back
    NovaLINK_Client notRetrievedClient(&client2Info);
    notRetrievedClient.mRelativeVolume = 3.5;
    notRetrievedClient.mDoingIO = true;
    
    // A known-good copy to check against
    NovaLINK_Client notRetrievedClientCopy(notRetrievedClient);
    
    // Try getting a client that we never added
    didGetClient = clientMap.GetClientNonRT(/* inClientID = */ 12345, &retrievedClient);
    XCTAssertFalse(didGetClient);
    [NovaLINK_ClientMapTests assertClient:&notRetrievedClient isEqualTo:&notRetrievedClientCopy];
    
    // Remove the client
    NovaLINK_Client removedClient = clientMap.RemoveClient(client1.mClientID);
    
    // Check that the client RemoveClient says we removed matches the one we added in the first place
    [NovaLINK_ClientMapTests assertClient:&removedClient isEqualTo:&client1];
    
    // We shouldn't be able to get the client after we've removed it
    didGetClient = clientMap.GetClientNonRT(client1Info.mClientID, &notRetrievedClient);
    XCTAssertFalse(didGetClient);
    
    // Calling GetClientNonRT should have left notRetrievedClient unchanged
    [NovaLINK_ClientMapTests assertClient:&notRetrievedClient isEqualTo:&notRetrievedClientCopy];
    
    // Check against hardcoded values as well just in case there's a problem with NovaLINK_Client's copy constructor
    XCTAssertEqual(notRetrievedClient.mRelativeVolume, 3.5);
    XCTAssert(notRetrievedClient.mDoingIO);
}

- (void)testAddRemoveMultipleClients {
    NovaLINK_ClientMap clientMap(&taskQueue);
    
    // Add the clients
    clientMap.AddClient(client1);
    clientMap.AddClient(client2);
    
    // Get both clients from the map and check they match what we added
    {
        NovaLINK_Client retrievedClient1, retrievedClient2;
        bool didGetClient = clientMap.GetClientNonRT(client1Info.mClientID, &retrievedClient1);
        XCTAssert(didGetClient);
        [NovaLINK_ClientMapTests assertClient:&retrievedClient1 isEqualTo:&client1];
        
        didGetClient = clientMap.GetClientNonRT(client2Info.mClientID, &retrievedClient2);
        XCTAssert(didGetClient);
        [NovaLINK_ClientMapTests assertClient:&retrievedClient2 isEqualTo:&client2];
    }
    
    // Remove one and check we can still get the other
    clientMap.RemoveClient(client1Info.mClientID);
    
    {
        NovaLINK_Client retrievedClient2;
        bool didGetClient = clientMap.GetClientNonRT(client2Info.mClientID, &retrievedClient2);
        XCTAssert(didGetClient);
        [NovaLINK_ClientMapTests assertClient:&retrievedClient2 isEqualTo:&client2];
    }
    
    // Remove the other
    NovaLINK_Client removedClient = clientMap.RemoveClient(client2Info.mClientID);
    [NovaLINK_ClientMapTests assertClient:&removedClient isEqualTo:&client2];
    
    // Check that we can't get either client from the map anymore
    const AudioServerPlugInClientInfo notRetrievedClientInfo = { 5, 10, true, CFSTR("not.retrieved.client") };
    NovaLINK_Client notRetrievedClient(&notRetrievedClientInfo);
    
    bool didGetClient = clientMap.GetClientNonRT(client1Info.mClientID, &notRetrievedClient);
    XCTAssertFalse(didGetClient);
    [NovaLINK_ClientMapTests assertClient:&notRetrievedClient hasInfoEqualTo:&notRetrievedClientInfo];
    
    didGetClient = clientMap.GetClientNonRT(client2Info.mClientID, &notRetrievedClient);
    XCTAssertFalse(didGetClient);
    [NovaLINK_ClientMapTests assertClient:&notRetrievedClient hasInfoEqualTo:&notRetrievedClientInfo];
}

- (void)testAddClientSeveralTimes {
    NovaLINK_ClientMap clientMap(&taskQueue);
    
    // Adding a client once should work
    clientMap.AddClient(client2);
    
    // Adding a different client should work
    clientMap.AddClient(client1);
    
    // Adding the same client twice should fail
    NovaLINKShouldThrow<NovaLINK_InvalidClientException>(self, [&](){
        clientMap.AddClient(client2);
    });
    
    // Adding the other client again should fail too
    NovaLINKShouldThrow<NovaLINK_InvalidClientException>(self, [&](){
        clientMap.AddClient(client1);
    });
}

@end

