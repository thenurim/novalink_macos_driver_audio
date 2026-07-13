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
//  NovaLINK_Types.h
//  SharedSource
//
//  Copyright © 2016, 2017, 2019, 2024 Kyle Neideck
//

#ifndef SharedSource__NovaLINK_Types
#define SharedSource__NovaLINK_Types

// STL Includes
#if defined(__cplusplus)
#include <stdexcept>
#endif

// System Includes
#include <CoreAudio/AudioServerPlugIn.h>


#pragma mark Project URLs

static const char* const kNovaLINKProjectURL = "https://github.com/thenurim/novalink_macos_driver_audio";
static const char* const kNovaLINKWebsiteURL = "https://novalink.thenurim.life";
static const char* const kNovaLINKIssueTrackerURL = "https://github.com/thenurim/novalink_macos_driver_audio/issues";
static const char* const kNovaLINKContributorsURL = "https://github.com/thenurim/novalink_macos_driver_audio/issues/contributors";

#pragma mark IDs

// TODO: Change these and the other defines to const strings?
#define kNovaLINKDriverBundleID           "life.thenurim.novalink.AudioDriver"
#define kNovaLINKAppBundleID              "life.thenurim.novalink.App"
#define kNovaLINKXPCHelperBundleID        "life.thenurim.novalink.XPCHelper"

#define kNovaLINKDeviceUID                "NovaLINKDevice"
#define kNovaLINKDeviceModelUID           "NovaLINKDeviceModelUID"
#define kNovaLINKDeviceUID_UISounds       "NovaLINKDevice_UISounds"
#define kNovaLINKDeviceModelUID_UISounds  "NovaLINKDeviceModelUID_UISounds"
#define kNovaLINKNullDeviceUID            "NovaLINKNullDevice"
#define kNovaLINKNullDeviceModelUID       "NovaLINKNullDeviceModelUID"

// The object IDs for the audio objects this driver implements.
//
// NovaLINKDevice always publishes this fixed set of objects (except when NovaLINKDevice's volume or mute
// controls are disabled). We might need to change that at some point, but so far it hasn't caused
// any problems and it makes the driver much simpler.
enum
{
	kObjectID_PlugIn                            = kAudioObjectPlugInObject,
    // NovaLINKDevice
	kObjectID_Device                            = 2,   // Belongs to kObjectID_PlugIn
	kObjectID_Stream_Input                      = 3,   // Belongs to kObjectID_Device
	kObjectID_Stream_Output                     = 4,   // Belongs to kObjectID_Device
	kObjectID_Volume_Output_Master              = 5,   // Belongs to kObjectID_Device
	kObjectID_Mute_Output_Master                = 6,   // Belongs to kObjectID_Device
    // Null Device
    kObjectID_Device_Null                       = 7,   // Belongs to kObjectID_PlugIn
    kObjectID_Stream_Null                       = 8,   // Belongs to kObjectID_Device_Null
    // NovaLINKDevice for UI sounds
    kObjectID_Device_UI_Sounds                  = 9,   // Belongs to kObjectID_PlugIn
    kObjectID_Stream_Input_UI_Sounds            = 10,  // Belongs to kObjectID_Device_UI_Sounds
    kObjectID_Stream_Output_UI_Sounds           = 11,  // Belongs to kObjectID_Device_UI_Sounds
    kObjectID_Volume_Output_Master_UI_Sounds    = 12,  // Belongs to kObjectID_Device_UI_Sounds
};

// AudioObjectPropertyElement docs: "Elements are numbered sequentially where 0 represents the
// master element."
static const AudioObjectPropertyElement kMasterChannel = kAudioObjectPropertyElementMaster;

#pragma NovaLINK Plug-in Custom Properties

enum
{
    // A CFBoolean. True if the null device is enabled. Settable, false by default.
    kAudioPlugInCustomPropertyNullDeviceActive = 'nuld'
};

#pragma mark NovaLINKDevice Custom Properties

enum
{
    // TODO: Combine the two music player properties
    
    // The process ID of the music player as a CFNumber. Setting this property will also clear the value of
    // kAudioDeviceCustomPropertyMusicPlayerBundleID. We use 0 to mean unset.
    //
    // There is currently no way for a client to tell whether the process it has set as the music player is a
    // client of the NovaLINKDevice.
    kAudioDeviceCustomPropertyMusicPlayerProcessID                    = 'mppi',
    // The music player's bundle ID as a CFString (UTF8), or the empty string if it's unset/null. Setting this
    // property will also clear the value of kAudioDeviceCustomPropertyMusicPlayerProcessID.
    kAudioDeviceCustomPropertyMusicPlayerBundleID                     = 'mpbi',
    // A CFNumber that specifies whether the device is silent, playing only music (i.e. the client set as the
    // music player is the only client playing audio) or audible. See enum values below. This property is only
    // updated after the audible state has been different for kDeviceAudibleStateMinChangedFramesForUpdate
    // consecutive frames. (To avoid excessive CPU use if for some reason the audible state starts changing
    // very often.)
    kAudioDeviceCustomPropertyDeviceAudibleState                      = 'daud',
    // A CFBoolean similar to kAudioDevicePropertyDeviceIsRunning except it ignores whether IO is running for
    // NovaLINKApp. This is so NovaLINKApp knows when it can stop doing IO to save CPU.
    kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp = 'runo',
    // A CFArray of CFBooleans indicating which of NovaLINKDevice's controls are enabled. All controls are enabled
    // by default. This property is settable. See the array indices below for more info.
    kAudioDeviceCustomPropertyEnabledOutputControls                   = 'bgct'
};

// The number of silent/audible frames before NovaLINKDriver will change kAudioDeviceCustomPropertyDeviceAudibleState
#define kDeviceAudibleStateMinChangedFramesForUpdate (2 << 11)

enum NovaLINKDeviceAudibleState : SInt32
{
    // kAudioDeviceCustomPropertyDeviceAudibleState values
    //
    // No audio is playing on the device's streams (regardless of whether IO is running or not)
    kNovaLINKDeviceIsSilent              = 'silt',
    // The client whose bundle ID matches the current value of kCustomAudioDevicePropertyMusicPlayerBundleID is the
    // only audible client
    kNovaLINKDeviceIsSilentExceptMusic   = 'olym',
    kNovaLINKDeviceIsAudible             = 'audi'
};

// kAudioDeviceCustomPropertyEnabledOutputControls indices
enum
{
    // True if NovaLINKDevice's master output volume control is enabled.
    kNovaLINKEnabledOutputControlsIndex_Volume = 0,
    // True if NovaLINKDevice's master output mute control is enabled.
    kNovaLINKEnabledOutputControlsIndex_Mute   = 1
};

#pragma mark NovaLINKDevice Custom Property Addresses

// For convenience.

static const AudioObjectPropertyAddress kNovaLINKMusicPlayerProcessIDAddress = {
    kAudioDeviceCustomPropertyMusicPlayerProcessID,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
};

static const AudioObjectPropertyAddress kNovaLINKMusicPlayerBundleIDAddress = {
    kAudioDeviceCustomPropertyMusicPlayerBundleID,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
};

static const AudioObjectPropertyAddress kNovaLINKAudibleStateAddress = {
    kAudioDeviceCustomPropertyDeviceAudibleState,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
};

static const AudioObjectPropertyAddress kNovaLINKRunningSomewhereOtherThanNovaLINKAppAddress = {
    kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMaster
};

static const AudioObjectPropertyAddress kNovaLINKEnabledOutputControlsAddress = {
    kAudioDeviceCustomPropertyEnabledOutputControls,
    kAudioObjectPropertyScopeOutput,
    kAudioObjectPropertyElementMaster
};

#pragma mark XPC Return Codes

enum {
    kNovaLINKXPC_Success,
    kNovaLINKXPC_MessageFailure,
    kNovaLINKXPC_Timeout,
    kNovaLINKXPC_NovaLINKAppStateError,
    kNovaLINKXPC_HardwareError,
    kNovaLINKXPC_ReturningEarlyError,
    kNovaLINKXPC_InternalError
};

#pragma mark Exceptions

#if defined(__cplusplus)

class NovaLINK_InvalidClientException : public std::runtime_error {
public:
    NovaLINK_InvalidClientException() : std::runtime_error("InvalidClient") { }
};

class NovaLINK_InvalidClientPIDException : public std::runtime_error {
public:
    NovaLINK_InvalidClientPIDException() : std::runtime_error("InvalidClientPID") { }
};

class NovaLINK_DeviceNotSetException : public std::runtime_error {
public:
    NovaLINK_DeviceNotSetException() : std::runtime_error("DeviceNotSet") { }
};

#endif

// Assume we've failed to start the output device if it isn't running IO after this timeout expires.
//
// Currently set to 30s because some devices, e.g. AirPlay, can legitimately take that long to start.
//
// TODO: Should we have a timeout at all? Is there a notification we can subscribe to that will tell us whether the
//       device is still making progress? Should we regularly poll mOutputDevice.IsAlive() while we're waiting to
//       check it's still responsive?
static const UInt64 kStartIOTimeoutNsec = 30 * NSEC_PER_SEC;

#endif /* SharedSource__NovaLINK_Types */

