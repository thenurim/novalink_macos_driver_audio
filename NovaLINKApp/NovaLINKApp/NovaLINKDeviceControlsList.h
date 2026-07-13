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
//  NovaLINKDeviceControlsList.h
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//

#ifndef NovaLINKApp__NovaLINKDeviceControlsList
#define NovaLINKApp__NovaLINKDeviceControlsList

// Local Includes
#include "NovaLINKAudioDevice.h"

// PublicUtility Includes
#include "CAHALAudioDevice.h"
#include "CAHALAudioSystemObject.h"
#include "CAMutex.h"

// System Includes
#include <dispatch/dispatch.h>
#include <AudioToolbox/AudioServices.h>


#pragma clang assume_nonnull begin

class NovaLINKDeviceControlsList
{

#pragma mark Construction/Destruction

public:

                        NovaLINKDeviceControlsList(AudioObjectID inNovaLINKDevice,
                                              CAHALAudioSystemObject inAudioSystem
                                                      = CAHALAudioSystemObject());
                        ~NovaLINKDeviceControlsList();
                        // Disallow copying
                        NovaLINKDeviceControlsList(const NovaLINKDeviceControlsList&) = delete;
                        NovaLINKDeviceControlsList& operator=(const NovaLINKDeviceControlsList&) = delete;

#pragma mark Accessors

    /*! @param inNovaLINKDeviceID The ID of NovaLINKDevice. */
    void                SetNovaLINKDevice(AudioObjectID inNovaLINKDeviceID);

#pragma mark Update Controls List

    /*!
     Enable the NovaLINKDevice controls (volume and mute currently) that can be matched to controls of
     the given device, and disable the ones that can't.

     @param inDeviceID The ID of the device.
     @return True if NovaLINKDevice's list of controls was updated.
     @throws CAException if an error is received from either device.
     */
    bool                MatchControlsListOf(AudioObjectID inDeviceID);
    /*!
     After updating NovaLINKDevice's controls list, we need to change the default device so programs
     (including OS X's audio UI) will update themselves. We could just change to the real output
     device and change back, but that could have side effects the user wouldn't expect. For example,
     an app the user has muted might be unmuted for a short period.

     Instead we tell NovaLINKDriver to enable the Null Device -- a device that does nothing -- so we can
     use it to toggle the default device. The Null Device is normally disabled so it can be hidden
     from the user. OS X won't let us make a hidden device temporarily visible or set a hidden
     device as the default, so we have to completely remove the Null Device from the system while
     we're not using it.
     
     @throws CAException if it fails to enable the Null Device.
     */
    void                PropagateControlListChange();

#pragma mark Implementation

private:
    /*! Lazily initialises the fields used to toggle the default device. */
    void                InitDeviceToggling();
    /*! Changes the OS X default audio device to the Null Device and then back to NovaLINKDevice. */
    void                ToggleDefaultDevice();
    /*!
     Enable or disable the Null Device. See PropagateControlListChange and NovaLINK_NullDevice in 
     NovaLINKDriver.

     @throws CAException if we can't get the NovaLINKDriver plug-in audio object from the HAL or the HAL
                         returns an error when setting kAudioPlugInCustomPropertyNullDeviceActive.
     */
    void                SetNullDeviceEnabled(bool inEnabled);

    dispatch_block_t __nullable CreateDeviceToggleBlock();
    dispatch_block_t __nullable CreateDeviceToggleBackBlock();
    dispatch_block_t __nullable CreateDisableNullDeviceBlock();

    void                DestroyBlock(dispatch_block_t __nullable & block);

private:
    CAMutex             mMutex { "Device Controls List" };
    bool                mDeviceTogglingInitialised = false;
    // OS X 10.9 doesn't have the functions we use for PropagateControlListChange.
    bool                mCanToggleDeviceOnSystem;

    NovaLINKAudioDevice      mNovaLINKDevice;
    CAHALAudioSystemObject mAudioSystem;  // Not guarded by the mutex.

    enum ToggleState
    {
        NotToggling, SettingNullDeviceAsDefault, SettingNovaLINKDeviceAsDefault, DisablingNullDevice
    };
    NovaLINKDeviceControlsList::ToggleState mDeviceToggleState = ToggleState::NotToggling;

    dispatch_block_t __nullable mDeviceToggleBlock      = nullptr;
    dispatch_block_t __nullable mDeviceToggleBackBlock  = nullptr;
    dispatch_block_t __nullable mDisableNullDeviceBlock = nullptr;

    // These will only ever be null after construction on 10.9, since toggling will be disabled.
    dispatch_queue_t __nullable                 mListenerQueue = nullptr;
    AudioObjectPropertyListenerBlock __nullable mListenerBlock = nullptr;

};

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKDeviceControlsList */

