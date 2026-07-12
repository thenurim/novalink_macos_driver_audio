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
//  NovaLINK_DeviceTests.mm
//  NovaLINKDriver
//
//  Copyright © 2016 Kyle Neideck
//

// Unit Include
#include "NovaLINK_Device.h"

// Local Includes
#include "NovaLINK_TestUtils.h"

// NovaLINKDriver Includes
#include "NovaLINK_Types.h"

// PublicUtility Includes
#include "CAException.h"

// STL Includes
#include <stdexcept>


// Subclass NovaLINK_Device to add some test-only functions.
class TestNovaLINK_Device
:
    public NovaLINK_Device
{

public:
    TestNovaLINK_Device();
    ~TestNovaLINK_Device() = default;

};

TestNovaLINK_Device::TestNovaLINK_Device()
:
    NovaLINK_Device(kObjectID_Device,
               CFSTR(kDeviceName),
               CFSTR(kNovaLINKDeviceUID),
               CFSTR(kNovaLINKDeviceModelUID),
               kObjectID_Stream_Input,
               kObjectID_Stream_Output,
               kObjectID_Volume_Output_Master,
               kObjectID_Mute_Output_Master)
{
    Activate();
}


@interface NovaLINK_DeviceTests : XCTestCase {
    TestNovaLINK_Device* testDevice;
}

@end


@implementation NovaLINK_DeviceTests

- (void) setUp {
    [super setUp];
    testDevice = new TestNovaLINK_Device();
}

- (void) tearDown {
    delete testDevice;
    [super tearDown];
}

- (void) testDoIOOperation_writeMix_readInput {
    // The number of audio frames to send/receive in the IO operations.
    const int kFrameSize = 512;

    // Choose a sample time that will make the data wrap around to the start of the device's
    // internal ring buffer.
    AudioServerPlugInIOCycleInfo cycleInfo {};
    cycleInfo.mOutputTime.mSampleTime = kLoopbackRingBufferFrameSize - 25.0;

    // Generate the test input data.
    Float32 inputBuffer[kFrameSize * 2];

    for(int i = 0; i < kFrameSize * 2; i++)
    {
        inputBuffer[i] = static_cast<Float32>(i);
    }

    // Send a copy of the input buffer just in case DoIOOperation modifies the data for some reason.
    Float32 inputBufferCopy[kFrameSize * 2];
    memcpy(inputBufferCopy, inputBuffer, sizeof(inputBuffer));

    // Send the input data to the device.
    testDevice->DoIOOperation(/* inStreamObjectID = */ kObjectID_Stream_Output,
                              /* inClientID = */ 0,
                              /* inOperationID = */ kAudioServerPlugInIOOperationWriteMix,
                              /* inIOBufferFrameSize = */ kFrameSize,
                              /* inIOCycleInfo = */ cycleInfo,
                              /* ioMainBuffer = */ inputBuffer,
                              /* ioSecondaryBuffer = */ nullptr);

    // Request data from the same point in time so we get the same data back.
    cycleInfo.mInputTime.mSampleTime = kLoopbackRingBufferFrameSize - 25.0;

    // Read the data back from the device.
    Float32 outputBuffer[kFrameSize * 2];

    testDevice->DoIOOperation(/* inStreamObjectID = */ kObjectID_Stream_Output,
                              /* inClientID = */ 0,
                              /* inOperationID = */ kAudioServerPlugInIOOperationReadInput,
                              /* inIOBufferFrameSize = */ kFrameSize,
                              /* inIOCycleInfo = */ cycleInfo,
                              /* ioMainBuffer = */ outputBuffer,
                              /* ioSecondaryBuffer = */ nullptr);

    // Check the output matches the input.
    for(int i = 0; i < kFrameSize * 2; i++)
    {
        XCTAssertEqual(inputBuffer[i], outputBuffer[i]);
    }
}

- (void) testCustomPropertyMusicPlayerBundleID {
    // Convenience wrappers
    auto getBundleID = [&](UInt32 inDataSize = sizeof(CFStringRef)){
        CFStringRef bundleID = nullptr;
        UInt32 outDataSize;
        
        testDevice->GetPropertyData(/* inObjectID = */ kObjectID_Device,
                                    /* inClientPID = */ 3,
                                    /* inAddress = */ kNovaLINKMusicPlayerBundleIDAddress,
                                    /* inQualifierDataSize = */ 0,
                                    /* inQualifierData = */ nullptr,
                                    /* inDataSize = */ inDataSize,
                                    /* outDataSize = */ outDataSize,
                                    /* outData = */ reinterpret_cast<void* __nonnull>(&bundleID));
        
        // This isn't technically required, but we're unlikely to ever want to return any more/less data from GetPropertyData.
        XCTAssertEqual(outDataSize, sizeof(CFStringRef));
        
        return (__bridge_transfer NSString*)bundleID;
    };
    
    auto setBundleID = [&](const CFStringRef* __nullable bundleID, UInt32 dataSize = sizeof(CFStringRef)){
        testDevice->SetPropertyData(/* inObjectID = */ kObjectID_Device,
                                    /* inClientPID = */ 1234,
                                    /* inAddress = */ kNovaLINKMusicPlayerBundleIDAddress,
                                    /* inQualifierDataSize = */ 0,
                                    /* inQualifierData = */ nullptr,
                                    /* inDataSize = */ dataSize,
                                    /* inData = */ reinterpret_cast<const void* __nonnull>(bundleID));
    };
    
    // Should be set to the empty string by default.
    XCTAssertEqualObjects(getBundleID(), @"");
    
    // Should be able to set the property to an arbitrary string. (Purposefully not using CFSTR for this one just in case it
    // makes a difference.)
    CFStringRef newID = CFStringCreateWithCString(kCFAllocatorDefault, "test.bundle.ID", kCFStringEncodingUTF8);
    setBundleID(&newID);
    CFRelease(newID);
    XCTAssertEqualObjects(getBundleID(), @"test.bundle.ID");
    
    // Should be able to set the property back to the empty string.
    newID = CFSTR("");
    setBundleID(&newID);
    XCTAssertEqualObjects(getBundleID(), @"");
    
    // Arguments should be null-checked.
    NovaLINKShouldThrow<std::runtime_error>(self, [&](){
        UInt32 outDataSize;
        testDevice->GetPropertyData(kObjectID_Device, 0, kNovaLINKMusicPlayerBundleIDAddress, 0, nullptr,
                                    sizeof(CFStringRef), outDataSize,
                                    /* outData = */ reinterpret_cast<void* __nonnull>(NULL));
    });
    NovaLINKShouldThrow<std::runtime_error>(self, [&](){
        setBundleID(nullptr);
    });
    
    // Invalid data should be rejected.
    NovaLINKShouldThrow<CAException>(self, [&](){
        setBundleID((CFStringRef*)&kCFNull);
    });
    NovaLINKShouldThrow<CAException>(self, [&](){
        CFStringRef nullRef = nullptr;
        setBundleID(&nullRef);
    });
    NovaLINKShouldThrow<CAException>(self, [&](){
        CFArrayRef array = (__bridge_retained CFArrayRef)@[ @1, @2 ];
        setBundleID((CFStringRef*)&array);
    });
    
    // Should throw if not given enough space for the return data.
    NovaLINKShouldThrow<CAException>(self, [&](){
        getBundleID(/* inDataSize = */ 0);
    });
    
    newID = CFSTR("bundle");
    
    // Passing more data than needed should be fine as long as it starts with a CFStringRef.
    setBundleID(&newID, sizeof(CFStringRef) * 2);
    
    // Should throw if not enough data is passed.
    NovaLINKShouldThrow<CAException>(self, [&](){
        setBundleID(&newID, sizeof(CFStringRef) - 1);
    });
}

// TODO: Performance tests?
- (void) testPerformanceExample {
    // This is an example of a performance test case.
    [self measureBlock:^{
        // Put the code you want to measure the time of here.
    }];
}

@end

