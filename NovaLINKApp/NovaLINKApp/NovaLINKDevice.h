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
//  NovaLINKDevice.h
//  NovaLINKApp
//
//  Copyright © 2017 Kyle Neideck
//
//  The interface to NovaLINKDevice, the main virtual device published by NovaLINKDriver, and the second
//  instance of that device, which handles UI-related audio. In most cases, users of this class
//  should be able to think of it as representing a single device.
//
//  NovaLINKDevice is the device that appears as "NovaLINK" in programs that list the output
//  devices, e.g. System Preferences. It receives the system's audio, processes it and sends it to
//  NovaLINKApp by publishing an input stream. NovaLINKApp then plays the audio on the user's real output
//  device.
//
//  See NovaLINKDriver/NovaLINKDriver/NovaLINK_Device.h.
//

#ifndef NovaLINKApp__NovaLINKDevice
#define NovaLINKApp__NovaLINKDevice

// Superclass Includes
#include "NovaLINKAudioDevice.h"

// Local Includes
#include "NovaLINK_Types.h"

#pragma clang assume_nonnull begin

class NovaLINKDevice
:
    public NovaLINKAudioDevice
{

#pragma mark Construction/Destruction

public:
    /*!
     @throws CAException If NovaLINKDevice is not found or the HAL returns an error when queried for
                         NovaLINKDevice's current Audio Object ID.
     */
                        NovaLINKDevice();
    virtual            ~NovaLINKDevice();

#pragma mark Systemwide Default Device

public:
    /*!
     Set NovaLINKDevice as the default audio device for all processes.

     @throws CAException If the HAL responds with an error.
     */
    void                SetAsOSDefault();
    /*!
     Replace NovaLINKDevice as the default device with the output device.

     @throws CAException If the HAL responds with an error.
     */
    void                UnsetAsOSDefault(AudioDeviceID inOutputDeviceID);

#pragma mark Audible State

public:
    /*!
     @return NovaLINKDevice's current "audible state", which can be either silent, silent except for the
             user's music player or audible, meaning a program other than the music player is
             playing audio.
     @throws CAException If the HAL returns an error or invalid data when queried.
     @see kAudioDeviceCustomPropertyDeviceAudibleState in NovaLINK_Types.h.
     */
    NovaLINKDeviceAudibleState GetAudibleState() const;

#pragma mark Music Player

public:
    /*!
     @return The value of NovaLINKDevice's property for the selected music player's process ID. Zero if
             the property is unset. (We assume kernel_task will never be the user's music player.)
     @throws CAException If the HAL returns an error or an invalid PID when queried.
     @see kAudioDeviceCustomPropertyMusicPlayerProcessID in NovaLINK_Types.h.
     */
    virtual pid_t       GetMusicPlayerProcessID() const;
    /*!
     Set the value of NovaLINKDevice's property for the selected music player's process ID. Pass zero to
     unset the property. Setting this property will unset the bundle ID version of the property.

     @throws CAException If the HAL returns an error.
     @see kAudioDeviceCustomPropertyMusicPlayerProcessID in NovaLINK_Types.h.
     */
    virtual void        SetMusicPlayerProcessID(CFNumberRef inProcessID) {
                            SetPropertyData_CFType(kNovaLINKMusicPlayerProcessIDAddress, inProcessID); }
    /*!
     @return The value of NovaLINKDevice's property for the selected music player's bundle ID. The empty
             string if the property is unset.
     @throws CAException If the HAL returns an error or an invalid bundle ID when queried.
     @see kAudioDeviceCustomPropertyMusicPlayerBundleID in NovaLINK_Types.h.
     */
    virtual CFStringRef GetMusicPlayerBundleID() const;
    /*!
     Set the value of NovaLINKDevice's property for the selected music player's bundle ID. Pass the empty
     string to unset the property. Setting this property will unset the process ID version of the
     property.

     @throws CAException If the HAL returns an error.
     @see kAudioDeviceCustomPropertyMusicPlayerBundleID in NovaLINK_Types.h.
     */
    virtual void        SetMusicPlayerBundleID(CFStringRef inBundleID) {
                            SetPropertyData_CFString(kNovaLINKMusicPlayerBundleIDAddress, inBundleID); }

#pragma mark UI Sounds Instance

public:
    /*! @return The instance of NovaLINKDevice that handles UI sounds. */
    NovaLINKAudioDevice      GetUISoundsNovaLINKDeviceInstance() { return mUISoundsNovaLINKDevice; }

private:
    /*! The instance of NovaLINKDevice that handles UI sounds. */
    NovaLINKAudioDevice      mUISoundsNovaLINKDevice;

};

#pragma clang assume_nonnull end

#endif /* NovaLINKApp__NovaLINKDevice */

