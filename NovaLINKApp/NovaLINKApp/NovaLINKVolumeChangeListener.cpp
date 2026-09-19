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
//  NovaLINKVolumeChangeListener.cpp
//  NovaLINKApp
//
//  Copyright © 2019 Kyle Neideck
//

// Self Include
#include "NovaLINKVolumeChangeListener.h"

// Local Includes
#import "NovaLINK_Utils.h"
#import "NovaLINKAudioDevice.h"

// PublicUtility Includes
#import "CAException.h"
#import "CAPropertyAddress.h"


#pragma clang assume_nonnull begin

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
const static std::vector<CAPropertyAddress> kVolumeChangeProperties = {
        // Output volume changes
        CAPropertyAddress(kAudioDevicePropertyVolumeScalar, kAudioObjectPropertyScopeOutput),
        // Mute/unmute
        CAPropertyAddress(kAudioDevicePropertyMute, kAudioObjectPropertyScopeOutput),
        // Received when controls are added to or removed from the device.
        CAPropertyAddress(kAudioObjectPropertyControlList),
        // Received when the device has changed and "clients should re-evaluate everything they need
        // to know about the device, particularly the layout and values of the controls".
        CAPropertyAddress(kAudioDevicePropertyDeviceHasChanged)
};
#pragma clang diagnostic pop

static dispatch_queue_t VolumeListenerQueue()
{
    static dispatch_once_t once;
    static dispatch_queue_t queue;
    dispatch_once(&once, ^{
        queue = dispatch_queue_create("life.thenurim.novalink.VolumeChangeListener",
                                      DISPATCH_QUEUE_SERIAL);
    });
    return queue;
}

NovaLINKVolumeChangeListener::NovaLINKVolumeChangeListener(NovaLINKAudioDevice device,
                                                 std::function<void(void)> handler)
:
    mListenerBlock(nullptr),
    mDevice(device)
{
    dispatch_queue_t listenerQueue = VolumeListenerQueue();

    // Register a listener that will update the slider when the user changes the volume or
    // mutes/unmutes their audio.
    mListenerBlock =
            Block_copy(^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress* inAddresses) {
                (void)inNumberAddresses;
                (void)inAddresses;

                // Bounce out of the HAL listener before calling the handler. The handler may query
                // the HAL; doing that synchronously here deadlocks coreaudiod and freezes the
                // status-item menu.
                dispatch_async(listenerQueue, ^{
                    handler();
                });
            });

    for(CAPropertyAddress property : kVolumeChangeProperties)
    {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            mDevice.AddPropertyListenerBlock(property, listenerQueue, mListenerBlock);
        });
    }
}

NovaLINKVolumeChangeListener::~NovaLINKVolumeChangeListener()
{
    dispatch_queue_t listenerQueue = VolumeListenerQueue();

    for(CAPropertyAddress property : kVolumeChangeProperties)
    {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            mDevice.RemovePropertyListenerBlock(property,
                                                listenerQueue,
                                                mListenerBlock);
        });
    }

    Block_release(mListenerBlock);
}

#pragma clang assume_nonnull end

