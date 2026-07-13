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

NovaLINKVolumeChangeListener::NovaLINKVolumeChangeListener(NovaLINKAudioDevice device,
                                                 std::function<void(void)> handler)
:
    mDevice(device)
{
    // Register a listener that will update the slider when the user changes the volume or
    // mutes/unmutes their audio.
    mListenerBlock =
            Block_copy(^(UInt32 inNumberAddresses, const AudioObjectPropertyAddress* inAddresses) {
                // The docs for AudioObjectPropertyListenerBlock say inAddresses will always contain
                // at least one property the block is listening to, so there's no need to check it.
                (void)inNumberAddresses;
                (void)inAddresses;

                // Call the callback.
                handler();
            });

    // Register for a number of properties that might indicate that clients need to update. For
    // example, the mute property changing means UI elements that display the volume will need to be
    // updated, even though it's not strictly a change in volume.
    for(CAPropertyAddress property : kVolumeChangeProperties)
    {
        // Instead of swallowing exceptions here, we could try again later, but I doubt it would be
        // worth the effort. And the documentation doesn't actually explain what could cause this
        // call to fail.
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            mDevice.AddPropertyListenerBlock(property, dispatch_get_main_queue(), mListenerBlock);
        });
    }
}

NovaLINKVolumeChangeListener::~NovaLINKVolumeChangeListener()
{
    // Deregister and release the listener block.
    for(CAPropertyAddress property : kVolumeChangeProperties)
    {
        NovaLINK_Utils::LogAndSwallowExceptions(NovaLINKDbgArgs, [&] {
            mDevice.RemovePropertyListenerBlock(property,
                                                dispatch_get_main_queue(),
                                                mListenerBlock);
        });
    }

    Block_release(mListenerBlock);
}

#pragma clang assume_nonnull end

