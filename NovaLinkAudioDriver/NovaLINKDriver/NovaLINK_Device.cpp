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
//  NovaLINK_Device.cpp
//  NovaLINKDriver
//
//  Copyright © 2016, 2017, 2019 Kyle Neideck
//  Copyright © 2017 Andrew Tonner
//  Copyright © 2019 Gordon Childs
//  Copyright © 2020 Aleksey Yurkevich
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_Device.cpp from Apple's SimpleAudioDriver Plug-In sample code. Also uses a few sections from Apple's
//  NullAudio.c sample code (found in the same sample project).
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

// Self Include
#include "NovaLINK_Device.h"

// Local Includes
#include "NovaLINK_PlugIn.h"
#include "NovaLINK_XPCHelper.h"
#include "NovaLINK_Utils.h"

// PublicUtility Includes
#include "CADispatchQueue.h"
#include "CAException.h"
#include "CACFArray.h"
#include "CACFString.h"
#include "CADebugMacros.h"
#include "CAHostTimeBase.h"

// STL Includes
#include <stdexcept>

// System Includes
#include <CoreAudio/AudioHardwareBase.h>


#pragma mark Construction/Destruction

pthread_once_t				NovaLINK_Device::sStaticInitializer = PTHREAD_ONCE_INIT;
NovaLINK_Device*					NovaLINK_Device::sInstance = nullptr;
NovaLINK_Device*					NovaLINK_Device::sUISoundsInstance = nullptr;

NovaLINK_Device&	NovaLINK_Device::GetInstance()
{
    pthread_once(&sStaticInitializer, StaticInitializer);
    return *sInstance;
}

NovaLINK_Device&	NovaLINK_Device::GetUISoundsInstance()
{
    pthread_once(&sStaticInitializer, StaticInitializer);
    return *sUISoundsInstance;
}

void	NovaLINK_Device::StaticInitializer()
{
    try
    {
        // The main instance, usually referred to in the code as "NovaLINKDevice". This is the device
        // that appears in System Preferences as "NovaLINK".
        sInstance = new NovaLINK_Device(kObjectID_Device,
                                   CFSTR(kDeviceName),
								   CFSTR(kNovaLINKDeviceUID),
								   CFSTR(kNovaLINKDeviceModelUID),
                                   kObjectID_Stream_Input,
                                   kObjectID_Stream_Output,
								   kObjectID_Volume_Output_Master,
								   kObjectID_Mute_Output_Master);
        sInstance->Activate();

        // The instance for system (UI) sounds.
        sUISoundsInstance = new NovaLINK_Device(kObjectID_Device_UI_Sounds,
										   CFSTR(kDeviceName_UISounds),
										   CFSTR(kNovaLINKDeviceUID_UISounds),
										   CFSTR(kNovaLINKDeviceModelUID_UISounds),
                                           kObjectID_Stream_Input_UI_Sounds,
                                           kObjectID_Stream_Output_UI_Sounds,
                                           kObjectID_Volume_Output_Master_UI_Sounds,
                                           kAudioObjectUnknown);  // No mute control.

        // Set up the UI sounds device's volume control.
        NovaLINK_VolumeControl& theUISoundsVolumeControl = sUISoundsInstance->mVolumeControl;
        // Default to full volume.
        theUISoundsVolumeControl.SetVolumeScalar(1.0f);
        // Make the volume curve a bit steeper than the default.
        theUISoundsVolumeControl.GetVolumeCurve().SetTransferFunction(CAVolumeCurve::kPow4Over1Curve);
        // Apply the volume to the device's output stream. The main instance of NovaLINK_Device doesn't
        // apply volume to its audio because NovaLINKApp changes the real output device's volume directly
        // instead.
        theUISoundsVolumeControl.SetWillApplyVolumeToAudio(true);

        sUISoundsInstance->Activate();
    }
    catch(...)
    {
        DebugMsg("NovaLINK_Device::StaticInitializer: failed to create the devices");

        delete sInstance;
        sInstance = nullptr;

        delete sUISoundsInstance;
        sUISoundsInstance = nullptr;
    }
}

NovaLINK_Device::NovaLINK_Device(AudioObjectID inObjectID,
					   const CFStringRef __nonnull inDeviceName,
					   const CFStringRef __nonnull inDeviceUID,
					   const CFStringRef __nonnull inDeviceModelUID,
                       AudioObjectID inInputStreamID,
                       AudioObjectID inOutputStreamID,
					   AudioObjectID inOutputVolumeControlID,
					   AudioObjectID inOutputMuteControlID)
:
	NovaLINK_AbstractDevice(inObjectID, kAudioObjectPlugInObject),
	mStateMutex("Device State"),
	mIOMutex("Device IO"),
	mDeviceName(inDeviceName),
	mDeviceUID(inDeviceUID),
	mDeviceModelUID(inDeviceModelUID),
    mWrappedAudioEngine(nullptr),
    mClients(inObjectID, &mTaskQueue),
    mInputStream(inInputStreamID, inObjectID, false, kSampleRateDefault),
    mOutputStream(inOutputStreamID, inObjectID, false, kSampleRateDefault),
    mAudibleState(),
    mVolumeControl(inOutputVolumeControlID, GetObjectID()),
    mMuteControl(inOutputMuteControlID, GetObjectID())
{
    // Initialises the loopback clock with the default sample rate and, if there is one, sets the wrapped device to the same sample rate
    SetSampleRate(kSampleRateDefault, true);
}

NovaLINK_Device::~NovaLINK_Device()
{
}

void	NovaLINK_Device::Activate()
{
	CAMutex::Locker theStateLocker(mStateMutex);

	//	Open the connection to the driver and initialize things.
	//_HW_Open();

	mInputStream.Activate();
	mOutputStream.Activate();

	if(mVolumeControl.GetObjectID() != kAudioObjectUnknown)
	{
		mVolumeControl.Activate();
	}

    if(mMuteControl.GetObjectID() != kAudioObjectUnknown)
	{
		mMuteControl.Activate();
	}
	
	//	Call the super-class, which just marks the object as active
	NovaLINK_AbstractDevice::Activate();
}

void	NovaLINK_Device::Deactivate()
{
	//	When this method is called, the object is basically dead, but we still need to be thread
	//	safe. In this case, we also need to be safe vs. any IO threads, so we need to take both
	//	locks.
	CAMutex::Locker theStateLocker(mStateMutex);
	CAMutex::Locker theIOLocker(mIOMutex);

    // Mark the device's sub-objects inactive.
	mInputStream.Deactivate();
	mOutputStream.Deactivate();
    mVolumeControl.Deactivate();
    mMuteControl.Deactivate();

	//	mark the object inactive by calling the super-class
	NovaLINK_AbstractDevice::Deactivate();
	
	//	close the connection to the driver
	//_HW_Close();
}

void    NovaLINK_Device::InitLoopback()
{
    // Calculate the number of host clock ticks per frame for our loopback clock.
    mLoopbackTime.hostTicksPerFrame = CAHostTimeBase::GetFrequency() / mLoopbackSampleRate;
    
    //  Allocate (or re-allocate) the loopback buffer.
    //  2 channels * 32-bit float = bytes in each frame
    //  Pass 1 for nChannels because it's going to be storing interleaved audio, which means we
    //  don't need a separate buffer for each channel.
	mLoopbackRingBuffer.Allocate(1, 2 * sizeof(Float32), kLoopbackRingBufferFrameSize);

    if(SupportsMicMix())
    {
        mMicRingBuffer.Allocate(1, 2 * sizeof(Float32), kLoopbackRingBufferFrameSize);
        mMicSampleTime = 0;
        mMicRingAllocated = true;
        mMicMixScratchFrames = kLoopbackRingBufferFrameSize;
        mMicMixScratch.reset(new Float32[mMicMixScratchFrames * 2]);
    }
}

#pragma mark Property Operations

bool	NovaLINK_Device::HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	This object owns several API-level objects. So the first thing to do is to figure out
	//	which object this request is really for. Note that mObjectID is an invariant as this
	//	driver's structure does not change dynamically. It will always have the parts it has.
	bool theAnswer = false;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_HasProperty(inObjectID, inClientPID, inAddress);
	}
    else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).HasProperty(inObjectID, inClientPID, inAddress);
	}

	return theAnswer;
}

bool	NovaLINK_Device::IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	bool theAnswer = false;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_IsPropertySettable(inObjectID, inClientPID, inAddress);
	}
	else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).IsPropertySettable(inObjectID, inClientPID, inAddress);
	}

	return theAnswer;
}

UInt32	NovaLINK_Device::GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	UInt32 theAnswer = 0;

	if(inObjectID == mObjectID)
	{
		theAnswer = Device_GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}
	else
	{
		theAnswer = GetOwnedObjectByID(inObjectID).GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	}

	return theAnswer;
}

void	NovaLINK_Device::GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
    ThrowIfNULL(outData, std::runtime_error("!outData"), "NovaLINK_Device::GetPropertyData: !outData");
    
	if(inObjectID == mObjectID)
	{
		Device_GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
	else
	{
		GetOwnedObjectByID(inObjectID).GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
	}
}

void	NovaLINK_Device::SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
    ThrowIfNULL(inData, std::runtime_error("no data"), "NovaLINK_Device::SetPropertyData: no data");
    
	if(inObjectID == mObjectID)
	{
		Device_SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
	}
	else
    {
        GetOwnedObjectByID(inObjectID).SetPropertyData(inObjectID,
                                                       inClientPID,
                                                       inAddress,
                                                       inQualifierDataSize,
                                                       inQualifierData,
                                                       inDataSize,
                                                       inData);
		if(IsStreamID(inObjectID))
		{
            // When one of the stream's sample rate changes, set the new sample rate for both
            // streams and the device. The streams check the new format before this point but don't
            // change until the device tells them to, as it has to get the host to pause IO first.
            if(inAddress.mSelector == kAudioStreamPropertyVirtualFormat ||
               inAddress.mSelector == kAudioStreamPropertyPhysicalFormat)
            {
                const AudioStreamBasicDescription* theNewFormat =
                    reinterpret_cast<const AudioStreamBasicDescription*>(inData);
                RequestSampleRate(theNewFormat->mSampleRate);
            }
		}
	}
}

#pragma mark Device Property Operations

bool	NovaLINK_Device::Device_HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
        case kAudioDevicePropertyStreams:
        case kAudioDevicePropertyIcon:
        case kAudioObjectPropertyCustomPropertyInfoList:
        case kAudioDeviceCustomPropertyDeviceAudibleState:
        case kAudioDeviceCustomPropertyMusicPlayerProcessID:
        case kAudioDeviceCustomPropertyMusicPlayerBundleID:
        case kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp:
        case kAudioDeviceCustomPropertyEnabledOutputControls:
			theAnswer = true;
			break;

        case kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost:
        case kAudioDeviceCustomPropertyInjectMicAudio:
			theAnswer = SupportsMicMix();
			break;
			
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
		case kAudioDevicePropertyPreferredChannelsForStereo:
		case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
			theAnswer = (inAddress.mScope == kAudioObjectPropertyScopeInput) || (inAddress.mScope == kAudioObjectPropertyScopeOutput);
			break;
			
		default:
			theAnswer = NovaLINK_AbstractDevice::HasProperty(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

bool	NovaLINK_Device::Device_IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	bool theAnswer = false;
	switch(inAddress.mSelector)
    {
		case kAudioDevicePropertyStreams:
		case kAudioDevicePropertyLatency:
		case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyPreferredChannelsForStereo:
        case kAudioDevicePropertyPreferredChannelLayout:
		case kAudioDevicePropertyDeviceCanBeDefaultDevice:
		case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyIcon:
        case kAudioObjectPropertyCustomPropertyInfoList:
        case kAudioDeviceCustomPropertyDeviceAudibleState:
        case kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp:
			theAnswer = false;
			break;

        case kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost:
			theAnswer = false;
			break;
            
        case kAudioDevicePropertyNominalSampleRate:
        case kAudioDeviceCustomPropertyMusicPlayerProcessID:
        case kAudioDeviceCustomPropertyMusicPlayerBundleID:
        case kAudioDeviceCustomPropertyEnabledOutputControls:
			theAnswer = true;
			break;

        case kAudioDeviceCustomPropertyInjectMicAudio:
			theAnswer = SupportsMicMix();
			break;
		
		default:
			theAnswer = NovaLINK_AbstractDevice::IsPropertySettable(inObjectID, inClientPID, inAddress);
			break;
	};
	return theAnswer;
}

UInt32	NovaLINK_Device::Device_GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required. There is more detailed commentary about each property in the
	//	Device_GetPropertyData() method.
	
	UInt32 theAnswer = 0;

	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyOwnedObjects:
            {
                switch(inAddress.mScope)
                {
                    case kAudioObjectPropertyScopeGlobal:
                        theAnswer = GetNumberOfSubObjects() * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeInput:
                        theAnswer = kNumberOfInputSubObjects * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeOutput:
                        theAnswer = kNumberOfOutputStreams * sizeof(AudioObjectID);
                        theAnswer += GetNumberOfOutputControls() * sizeof(AudioObjectID);
                        break;

					default:
						break;
                };
            }
			break;

        case kAudioDevicePropertyStreams:
            {
                switch(inAddress.mScope)
                {
                    case kAudioObjectPropertyScopeGlobal:
                        theAnswer = kNumberOfStreams * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeInput:
                        theAnswer = kNumberOfInputStreams * sizeof(AudioObjectID);
                        break;
                        
                    case kAudioObjectPropertyScopeOutput:
                        theAnswer = kNumberOfOutputStreams * sizeof(AudioObjectID);
                        break;

					default:
						break;
                };
            }
			break;

        case kAudioObjectPropertyControlList:
            theAnswer = GetNumberOfOutputControls() * sizeof(AudioObjectID);
            break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			theAnswer = 1 * sizeof(AudioValueRange);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			theAnswer = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			theAnswer = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
			break;

        case kAudioDevicePropertyIcon:
            theAnswer = sizeof(CFURLRef);
            break;
            
        case kAudioObjectPropertyCustomPropertyInfoList:
            theAnswer = sizeof(AudioServerPlugInCustomPropertyInfo) * (SupportsMicMix() ? 7 : 5);
            break;
            
        case kAudioDeviceCustomPropertyDeviceAudibleState:
            theAnswer = sizeof(CFNumberRef);
            break;

        case kAudioDeviceCustomPropertyMusicPlayerProcessID:
            theAnswer = sizeof(CFPropertyListRef);
			break;
            
        case kAudioDeviceCustomPropertyMusicPlayerBundleID:
            theAnswer = sizeof(CFStringRef);
            break;
            
        case kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp:
            theAnswer = sizeof(CFBooleanRef);
            break;

        case kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost:
            theAnswer = sizeof(CFBooleanRef);
            break;

        case kAudioDeviceCustomPropertyEnabledOutputControls:
            theAnswer = sizeof(CFArrayRef);
            break;

        case kAudioDeviceCustomPropertyInjectMicAudio:
            theAnswer = sizeof(CFDataRef);
            break;
		
		default:
			theAnswer = NovaLINK_AbstractDevice::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
			break;
	};

	return theAnswer;
}

void	NovaLINK_Device::Device_GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	//	For each object, this driver implements all the required properties plus a few extras that
	//	are useful but not required.
	//	Also, since most of the data that will get returned is static, there are few instances where
	//	it is necessary to lock the state mutex.

	UInt32 theNumberItemsToFetch;
	UInt32 theItemIndex;

	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyName:
			//	This is the human readable name of the device. Note that in this case we return a
			//	value that is a key into the localizable strings in this bundle. This allows us to
			//	return a localized name for the device.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyName for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceName;
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in. Note that in this case
			//	we return a value that is a key into the localizable strings in this bundle. This
			//	allows us to return a localized name for the manufacturer.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer for the device");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR(kDeviceManufacturerName);
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	The device owns its streams and controls. Note that what is returned here
			//	depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all objects
                    {
                        CAMutex::Locker theStateLocker(mStateMutex);

                        if(theNumberItemsToFetch > GetNumberOfSubObjects())
                        {
                            theNumberItemsToFetch = GetNumberOfSubObjects();
                        }

                        //	fill out the list with as many objects as requested, which is everything
                        if(theNumberItemsToFetch > 0)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
                        }

                        if(theNumberItemsToFetch > 1)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStream.GetObjectID();
                        }

                        // If at least one of the controls is enabled, and there's room, return one.
						if(theNumberItemsToFetch > 2)
						{
							if(mVolumeControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[2] = mVolumeControl.GetObjectID();
							}
							else if(mMuteControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[2] = mMuteControl.GetObjectID();
							}
						}

						// If both controls are enabled, and there's room, return the mute control as well.
                        if(theNumberItemsToFetch > 3 && mVolumeControl.IsActive() && mMuteControl.IsActive())
                        {
							reinterpret_cast<AudioObjectID*>(outData)[3] = mMuteControl.GetObjectID();
                        }
                    }
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputSubObjects)
					{
						theNumberItemsToFetch = kNumberOfInputSubObjects;
					}
					
					//	fill out the list with the right objects
					if(theNumberItemsToFetch > 0)
					{
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
                    {
                        CAMutex::Locker theStateLocker(mStateMutex);

                        if(theNumberItemsToFetch > GetNumberOfOutputControls())
                        {
                            theNumberItemsToFetch = GetNumberOfOutputControls();
                        }

                        //	fill out the list with the right objects
                        if(theNumberItemsToFetch > 0)
                        {
                            reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStream.GetObjectID();
                        }

						// If at least one of the controls is enabled, and there's room, return one.
						if(theNumberItemsToFetch > 1)
						{
							if(mVolumeControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[1] = mVolumeControl.GetObjectID();
							}
							else if(mMuteControl.IsActive())
							{
								reinterpret_cast<AudioObjectID*>(outData)[1] = mMuteControl.GetObjectID();
							}
						}

						// If both controls are enabled, and there's room, return the mute control as well.
						if(theNumberItemsToFetch > 2 && mVolumeControl.IsActive() && mMuteControl.IsActive())
						{
							reinterpret_cast<AudioObjectID*>(outData)[2] = mMuteControl.GetObjectID();
						}
                    }
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioDevicePropertyDeviceUID:
			//	This is a CFString that is a persistent token that can identify the same
			//	audio device across boot sessions. Note that two instances of the same
			//	device must have different values for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceUID for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceUID;
			outDataSize = sizeof(CFStringRef);
			break;

		case kAudioDevicePropertyModelUID:
			//	This is a CFString that is a persistent token that can identify audio
			//	devices that are the same kind of device. Note that two instances of the
			//	save device must have the same value for this property.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyModelUID for the device");
            *reinterpret_cast<CFStringRef*>(outData) = mDeviceModelUID;
			outDataSize = sizeof(CFStringRef);
			break;
            
		case kAudioDevicePropertyDeviceIsRunning:
			//	This property returns whether or not IO is running for the device.
            ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyDeviceIsRunning for the device");
            *reinterpret_cast<UInt32*>(outData) = mClients.ClientsRunningIO() ? 1 : 0;
            outDataSize = sizeof(UInt32);
			break;

        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            // See NovaLINK_AbstractDevice::GetPropertyData.
			//
			// We don't allow the UI Sounds instance of NovaLINK_Device to be set as the default device
			// so that it doesn't appear in the list of devices, which would just be confusing to
			// users. (And it wouldn't make sense to set it as the default device anyway.)
			//
			// Instead, NovaLINKApp sets the UI Sounds device as the "system default" (see
			// kAudioDevicePropertyDeviceCanBeDefaultSystemDevice) so apps will use it for
			// UI-related sounds.
            ThrowIf(inDataSize < sizeof(UInt32),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "NovaLINK_Device::GetPropertyData: not enough space for the return value of "
                    "kAudioDevicePropertyDeviceCanBeDefaultDevice for the device");
            // TODO: Add a field for this and set it in NovaLINK_Device::StaticInitializer so we don't
            //       have to handle a specific instance differently here.
            *reinterpret_cast<UInt32*>(outData) = (GetObjectID() == kObjectID_Device_UI_Sounds ? 0 : 1);
            outDataSize = sizeof(UInt32);
            break;

		case kAudioDevicePropertyStreams:
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
			
			//	Note that what is returned here depends on the scope requested.
			switch(inAddress.mScope)
			{
				case kAudioObjectPropertyScopeGlobal:
					//	global scope means return all streams
					if(theNumberItemsToFetch > kNumberOfStreams)
					{
						theNumberItemsToFetch = kNumberOfStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					if(theNumberItemsToFetch > 1)
					{
						reinterpret_cast<AudioObjectID*>(outData)[1] = mOutputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeInput:
					//	input scope means just the objects on the input side
					if(theNumberItemsToFetch > kNumberOfInputStreams)
					{
						theNumberItemsToFetch = kNumberOfInputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mInputStream.GetObjectID();
					}
					break;
					
				case kAudioObjectPropertyScopeOutput:
					//	output scope means just the objects on the output side
					if(theNumberItemsToFetch > kNumberOfOutputStreams)
					{
						theNumberItemsToFetch = kNumberOfOutputStreams;
					}
					
					//	fill out the list with as many objects as requested
					if(theNumberItemsToFetch > 0)
					{
						reinterpret_cast<AudioObjectID*>(outData)[0] = mOutputStream.GetObjectID();
					}
					break;
			};
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioObjectID);
			break;

		case kAudioObjectPropertyControlList:
            {
                //	Calculate the number of items that have been requested. Note that this
                //	number is allowed to be smaller than the actual size of the list, in which
                //	case only that many items will be returned.
                theNumberItemsToFetch = inDataSize / sizeof(AudioObjectID);
                if(theNumberItemsToFetch > 2)
                {
                    theNumberItemsToFetch = 2;
                }

                UInt32 theNumberOfItemsFetched = 0;

                CAMutex::Locker theStateLocker(mStateMutex);
                
                //	fill out the list with as many objects as requested
                if(theNumberItemsToFetch > 0)
                {
					if(mVolumeControl.IsActive())
                    {
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mVolumeControl.GetObjectID();
                        theNumberOfItemsFetched++;
                    }
                    else if(mMuteControl.IsActive())
                    {
                        reinterpret_cast<AudioObjectID*>(outData)[0] = mMuteControl.GetObjectID();
                        theNumberOfItemsFetched++;
                    }
                }

                if(theNumberItemsToFetch > 1 && mVolumeControl.IsActive() && mMuteControl.IsActive())
                {
                    reinterpret_cast<AudioObjectID*>(outData)[1] = mMuteControl.GetObjectID();
                    theNumberOfItemsFetched++;
                }
                
                //	report how much we wrote
                outDataSize = theNumberOfItemsFetched * sizeof(AudioObjectID);
            }
			break;

        // TODO: Should we return the real kAudioDevicePropertyLatency and/or
        //       kAudioDevicePropertySafetyOffset for the real/wrapped output device?
        //       If so, should we also add on the extra latency added by NovaLINK? 

		case kAudioDevicePropertyNominalSampleRate:
			//	This property returns the nominal sample rate of the device.
            ThrowIf(inDataSize < sizeof(Float64),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyNominalSampleRate for the device");

            *reinterpret_cast<Float64*>(outData) = GetSampleRate();
            outDataSize = sizeof(Float64);
			break;

		case kAudioDevicePropertyAvailableNominalSampleRates:
			//	This returns all nominal sample rates the device supports as an array of
			//	AudioValueRangeStructs. Note that for discrete sampler rates, the range
			//	will have the minimum value equal to the maximum value.
            //
            //  NovaLINKDevice supports any sample rate so it can be set to match the output
            //  device when in loopback mode.
			
			//	Calculate the number of items that have been requested. Note that this
			//	number is allowed to be smaller than the actual size of the list. In such
			//	case, only that number of items will be returned
			theNumberItemsToFetch = inDataSize / sizeof(AudioValueRange);
			
			//	clamp it to the number of items we have
			if(theNumberItemsToFetch > 1)
			{
				theNumberItemsToFetch = 1;
			}
			
			//	fill out the return array
			if(theNumberItemsToFetch > 0)
			{
                // 0 would cause divide-by-zero errors in other NovaLINK_Device functions (and
                // wouldn't make sense anyway).
                ((AudioValueRange*)outData)[0].mMinimum = 1.0;
                // Just in case DBL_MAX would cause problems in a client for some reason,
                // use an arbitrary very large number instead. (It wouldn't make sense to
                // actually set the sample rate this high, but I don't know what a
                // reasonable maximum would be.)
                ((AudioValueRange*)outData)[0].mMaximum = 1000000000.0;
			}
			
			//	report how much we wrote
			outDataSize = theNumberItemsToFetch * sizeof(AudioValueRange);
			break;

		case kAudioDevicePropertyPreferredChannelsForStereo:
			//	This property returns which two channels to use as left/right for stereo
			//	data by default. Note that the channel numbers are 1-based.
			ThrowIf(inDataSize < (2 * sizeof(UInt32)), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelsForStereo for the device");
			((UInt32*)outData)[0] = 1;
			((UInt32*)outData)[1] = 2;
			outDataSize = 2 * sizeof(UInt32);
			break;

		case kAudioDevicePropertyPreferredChannelLayout:
			//	This property returns the default AudioChannelLayout to use for the device
			//	by default. For this device, we return a stereo ACL.
			{
				UInt32 theACLSize = offsetof(AudioChannelLayout, mChannelDescriptions) + (2 * sizeof(AudioChannelDescription));
				ThrowIf(inDataSize < theACLSize, CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyPreferredChannelLayout for the device");
				((AudioChannelLayout*)outData)->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
				((AudioChannelLayout*)outData)->mChannelBitmap = 0;
				((AudioChannelLayout*)outData)->mNumberChannelDescriptions = 2;
				for(theItemIndex = 0; theItemIndex < 2; ++theItemIndex)
				{
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelLabel = kAudioChannelLabel_Left + theItemIndex;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mChannelFlags = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[0] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[1] = 0;
					((AudioChannelLayout*)outData)->mChannelDescriptions[theItemIndex].mCoordinates[2] = 0;
				}
				outDataSize = theACLSize;
			}
			break;

		case kAudioDevicePropertyZeroTimeStampPeriod:
			//	This property returns how many frames the HAL should expect to see between
			//	successive sample times in the zero time stamps this device provides.
			ThrowIf(inDataSize < sizeof(UInt32), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyZeroTimeStampPeriod for the device");
			*reinterpret_cast<UInt32*>(outData) = kLoopbackRingBufferFrameSize;
			outDataSize = sizeof(UInt32);
            break;
            
        case kAudioDevicePropertyIcon:
            {
                // This property is a CFURL that points to the device's icon in the plugin's resource bundle
                ThrowIf(inDataSize < sizeof(CFURLRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDevicePropertyIcon for the device");
                
                CFBundleRef theBundle = CFBundleGetBundleWithIdentifier(NovaLINK_PlugIn::GetInstance().GetBundleID());
                ThrowIf(theBundle == NULL, CAException(kAudioHardwareUnspecifiedError), "NovaLINK_Device::Device_GetPropertyData: could not get the plugin bundle for kAudioDevicePropertyIcon");
                
                CFURLRef theURL = CFBundleCopyResourceURL(theBundle, CFSTR("DeviceIcon.icns"), NULL, NULL);
                ThrowIf(theURL == NULL, CAException(kAudioHardwareUnspecifiedError), "NovaLINK_Device::Device_GetPropertyData: could not get the URL for kAudioDevicePropertyIcon");
                
                *reinterpret_cast<CFURLRef*>(outData) = theURL;
                outDataSize = sizeof(CFURLRef);
            }
            break;
            
        case kAudioObjectPropertyCustomPropertyInfoList:
            theNumberItemsToFetch = inDataSize / sizeof(AudioServerPlugInCustomPropertyInfo);
            
            //	clamp it to the number of items we have
            {
                UInt32 theInfoCount = SupportsMicMix() ? 7 : 5;
                if(theNumberItemsToFetch > theInfoCount)
                {
                    theNumberItemsToFetch = theInfoCount;
                }
            }
            
            if(theNumberItemsToFetch > 0)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[0].mSelector = kAudioDeviceCustomPropertyMusicPlayerProcessID;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[0].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[0].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 1)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mSelector = kAudioDeviceCustomPropertyMusicPlayerBundleID;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFString;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[1].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 2)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[2].mSelector = kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[2].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[2].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 3)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[3].mSelector = kAudioDeviceCustomPropertyDeviceAudibleState;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[3].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[3].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 4)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[4].mSelector = kAudioDeviceCustomPropertyEnabledOutputControls;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[4].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[4].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 5)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[5].mSelector = kAudioDeviceCustomPropertyInjectMicAudio;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[5].mPropertyDataType = kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[5].mQualifierDataType = kAudioServerPlugInCustomPropertyDataTypeNone;
            }
            if(theNumberItemsToFetch > 6)
            {
                ((AudioServerPlugInCustomPropertyInfo*)outData)[6].mSelector =
                        kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[6].mPropertyDataType =
                        kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                ((AudioServerPlugInCustomPropertyInfo*)outData)[6].mQualifierDataType =
                        kAudioServerPlugInCustomPropertyDataTypeNone;
            }

            outDataSize = theNumberItemsToFetch * sizeof(AudioServerPlugInCustomPropertyInfo);
            break;
            
        case kAudioDeviceCustomPropertyDeviceAudibleState:
            {
                ThrowIf(inDataSize < sizeof(CFNumberRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyDeviceAudibleState for the device");

                // The audible state is read without locking to avoid priority inversions on the IO threads.
                NovaLINKDeviceAudibleState theAudibleState = mAudibleState.GetState();
                *reinterpret_cast<CFNumberRef*>(outData) =
                        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &theAudibleState);
                outDataSize = sizeof(CFNumberRef);
            }
            break;
            
        case kAudioDeviceCustomPropertyMusicPlayerProcessID:
            {
                ThrowIf(inDataSize < sizeof(CFNumberRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyMusicPlayerProcessID for the device");
                CAMutex::Locker theStateLocker(mStateMutex);
                pid_t pid = mClients.GetMusicPlayerProcessIDProperty();
                *reinterpret_cast<CFNumberRef*>(outData) = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
                outDataSize = sizeof(CFNumberRef);
            }
            break;
            
        case kAudioDeviceCustomPropertyMusicPlayerBundleID:
            {
                ThrowIf(inDataSize < sizeof(CFStringRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyMusicPlayerBundleID for the device");
                CAMutex::Locker theStateLocker(mStateMutex);
                *reinterpret_cast<CFStringRef*>(outData) = mClients.CopyMusicPlayerBundleIDProperty();
                outDataSize = sizeof(CFStringRef);
            }
            break;
            
        case kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp:
            ThrowIf(inDataSize < sizeof(CFBooleanRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp for the device");
            *reinterpret_cast<CFBooleanRef*>(outData) = mClients.ClientsOtherThanNovaLINKAppRunningIO() ? kCFBooleanTrue : kCFBooleanFalse;
            outDataSize = sizeof(CFBooleanRef);
            break;

        case kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost:
            ThrowIf(inDataSize < sizeof(CFBooleanRef),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "NovaLINK_Device::Device_GetPropertyData: not enough space for "
                    "kAudioDeviceCustomPropertyInputIsRunningSomewhereOtherThanPassthroughHost");
            *reinterpret_cast<CFBooleanRef*>(outData) =
                    mClients.ClientsOtherThanPassthroughHostReadingInput() ? kCFBooleanTrue : kCFBooleanFalse;
            outDataSize = sizeof(CFBooleanRef);
            break;

        case kAudioDeviceCustomPropertyEnabledOutputControls:
            {
                ThrowIf(inDataSize < sizeof(CFArrayRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for the return value of kAudioDeviceCustomPropertyEnabledOutputControls for the device");
                CACFArray theEnabledControls(2, true);

				{
					CAMutex::Locker theStateLocker(mStateMutex);
					theEnabledControls.AppendCFType(mVolumeControl.IsActive() ? kCFBooleanTrue : kCFBooleanFalse);
					theEnabledControls.AppendCFType(mMuteControl.IsActive() ? kCFBooleanTrue : kCFBooleanFalse);
				}

                *reinterpret_cast<CFArrayRef*>(outData) = theEnabledControls.CopyCFArray();
                outDataSize = sizeof(CFArrayRef);
            }
            break;

        case kAudioDeviceCustomPropertyInjectMicAudio:
            {
                // Write-only from the client's perspective; return empty data so GetProperty succeeds.
                ThrowIf(inDataSize < sizeof(CFDataRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_GetPropertyData: not enough space for kAudioDeviceCustomPropertyInjectMicAudio");
                *reinterpret_cast<CFDataRef*>(outData) = CFDataCreate(kCFAllocatorDefault, nullptr, 0);
                outDataSize = sizeof(CFDataRef);
            }
            break;

		default:
			NovaLINK_AbstractDevice::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	NovaLINK_Device::Device_SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	switch(inAddress.mSelector)
	{
        case kAudioDevicePropertyNominalSampleRate:
            ThrowIf(inDataSize < sizeof(Float64),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "NovaLINK_Device::Device_SetPropertyData: wrong size for the data for kAudioDevicePropertyNominalSampleRate");
            RequestSampleRate(*reinterpret_cast<const Float64*>(inData));
            break;
            
        case kAudioDeviceCustomPropertyMusicPlayerProcessID:
            {
                ThrowIf(inDataSize < sizeof(CFNumberRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_SetPropertyData: wrong size for the data for kAudioDeviceCustomPropertyMusicPlayerProcessID");
                
                CFNumberRef pidRef = *reinterpret_cast<const CFNumberRef*>(inData);
                
                ThrowIf(pidRef == NULL, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Device::Device_SetPropertyData: null reference given for kAudioDeviceCustomPropertyMusicPlayerProcessID");
                ThrowIf(CFGetTypeID(pidRef) != CFNumberGetTypeID(), CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Device::Device_SetPropertyData: CFType given for kAudioDeviceCustomPropertyMusicPlayerProcessID was not a CFNumber");
                
                // Get the pid out of the CFNumber we received
                // (Not using CACFNumber::GetSInt32 here because it would return 0 if CFNumberGetValue didn't write to our
                // pid variable, and we want that to be an error.)
                pid_t pid = INT_MIN;
                // CFNumberGetValue docs: "If the conversion is lossy, or the value is out of range, false is returned."
                Boolean success = CFNumberGetValue(pidRef, kCFNumberIntType, &pid);
                
                ThrowIf(!success, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Device::Device_SetPropertyData: probable error from CFNumberGetValue when reading pid for kAudioDeviceCustomPropertyMusicPlayerProcessID");
                
                CAMutex::Locker theStateLocker(mStateMutex);
                
                bool propertyWasChanged = false;
                
                try
                {
                    propertyWasChanged = mClients.SetMusicPlayer(pid);
                }
                catch(NovaLINK_InvalidClientPIDException)
                {
                    Throw(CAException(kAudioHardwareIllegalOperationError));
                }
                
                if(propertyWasChanged)
                {
                    // Send notification
                    CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
                        AudioObjectPropertyAddress theChangedProperties[] = { kNovaLINKMusicPlayerProcessIDAddress, kNovaLINKMusicPlayerBundleIDAddress };
                        NovaLINK_PlugIn::Host_PropertiesChanged(inObjectID, 2, theChangedProperties);
                    });
                }
            }
            break;
            
        case kAudioDeviceCustomPropertyMusicPlayerBundleID:
            {
                ThrowIf(inDataSize < sizeof(CFStringRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_Device::Device_SetPropertyData: wrong size for the data for kAudioDeviceCustomPropertyMusicPlayerBundleID");
            
                CFStringRef theBundleIDRef = *reinterpret_cast<const CFStringRef*>(inData);
                
                ThrowIfNULL(theBundleIDRef, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Device::Device_SetPropertyData: kAudioDeviceCustomPropertyMusicPlayerBundleID cannot be set to NULL");
                ThrowIf(CFGetTypeID(theBundleIDRef) != CFStringGetTypeID(), CAException(kAudioHardwareIllegalOperationError), "NovaLINK_Device::Device_SetPropertyData: CFType given for kAudioDeviceCustomPropertyMusicPlayerBundleID was not a CFString");
                
                CAMutex::Locker theStateLocker(mStateMutex);
                
                CFRetain(theBundleIDRef);
                CACFString bundleID(theBundleIDRef);
                
                bool propertyWasChanged = mClients.SetMusicPlayer(bundleID);
                
                if(propertyWasChanged)
                {
                    // Send notification
                    CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
                        AudioObjectPropertyAddress theChangedProperties[] = { kNovaLINKMusicPlayerBundleIDAddress, kNovaLINKMusicPlayerProcessIDAddress };
                        NovaLINK_PlugIn::Host_PropertiesChanged(inObjectID, 2, theChangedProperties);
                    });
                }
            }
            break;
            
        case kAudioDeviceCustomPropertyEnabledOutputControls:
            {
                ThrowIf(inDataSize < sizeof(CFArrayRef),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "NovaLINK_Device::Device_SetPropertyData: wrong size for the data for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                CFArrayRef theEnabledControlsRef = *reinterpret_cast<const CFArrayRef*>(inData);

                ThrowIfNULL(theEnabledControlsRef,
                            CAException(kAudioHardwareIllegalOperationError),
                            "NovaLINK_Device::Device_SetPropertyData: null reference given for "
                            "kAudioDeviceCustomPropertyEnabledOutputControls");
                ThrowIf(CFGetTypeID(theEnabledControlsRef) != CFArrayGetTypeID(),
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: CFType given for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls was not a CFArray");

                CACFArray theEnabledControls(theEnabledControlsRef, false);

                ThrowIf(theEnabledControls.GetNumberItems() != 2,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: Expected the CFArray given for "
                        "kAudioDeviceCustomPropertyEnabledOutputControls to have exactly 2 elements");

                bool theVolumeControlEnabled;
                bool didGetBool = theEnabledControls.GetBool(kNovaLINKEnabledOutputControlsIndex_Volume,
															 theVolumeControlEnabled);
                ThrowIf(!didGetBool,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: Expected CFBoolean for volume elem of "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                bool theMuteControlEnabled;
                didGetBool = theEnabledControls.GetBool(kNovaLINKEnabledOutputControlsIndex_Mute,
														theMuteControlEnabled);
                ThrowIf(!didGetBool,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: Expected CFBoolean for mute elem of "
                        "kAudioDeviceCustomPropertyEnabledOutputControls");

                RequestEnabledControls(theVolumeControlEnabled, theMuteControlEnabled);
            }
            break;

        case kAudioDeviceCustomPropertyInjectMicAudio:
            {
                ThrowIf(!SupportsMicMix(),
                        CAException(kAudioHardwareUnknownPropertyError),
                        "NovaLINK_Device::Device_SetPropertyData: mic inject is only supported on the main device");
                ThrowIf(inDataSize < sizeof(CFDataRef),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "NovaLINK_Device::Device_SetPropertyData: wrong size for kAudioDeviceCustomPropertyInjectMicAudio");

                CFDataRef theAudioData = *reinterpret_cast<const CFDataRef*>(inData);
                ThrowIfNULL(theAudioData,
                            CAException(kAudioHardwareIllegalOperationError),
                            "NovaLINK_Device::Device_SetPropertyData: null CFData for kAudioDeviceCustomPropertyInjectMicAudio");
                ThrowIf(CFGetTypeID(theAudioData) != CFDataGetTypeID(),
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: expected CFData for kAudioDeviceCustomPropertyInjectMicAudio");

                const CFIndex theByteCount = CFDataGetLength(theAudioData);
                ThrowIf(theByteCount < 0,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: negative CFData length for mic inject");
                // Interleaved stereo Float32 — reject partial frames.
                ThrowIf((theByteCount % static_cast<CFIndex>(2 * sizeof(Float32))) != 0,
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_Device::Device_SetPropertyData: mic inject CFData size must be a multiple of one stereo Float32 frame");

                const UInt32 theFrameCount =
                        static_cast<UInt32>(static_cast<size_t>(theByteCount) / (2 * sizeof(Float32)));
                if(theFrameCount == 0)
                {
                    break;
                }

                const Float32* theSamples = reinterpret_cast<const Float32*>(CFDataGetBytePtr(theAudioData));
                ThrowIfNULL(theSamples,
                            CAException(kAudioHardwareIllegalOperationError),
                            "NovaLINK_Device::Device_SetPropertyData: CFDataGetBytePtr returned null");

                InjectMicAudio(theSamples, theFrameCount);
            }
            break;

		default:
			NovaLINK_AbstractDevice::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
    };
}

#pragma mark IO Operations

void	NovaLINK_Device::StartIO(UInt32 inClientID)
{
    bool clientIsPassthroughHost, novaLINKAppHasClientRegistered;
    
    {
        CAMutex::Locker theStateLocker(mStateMutex);
        
        // An overview of the process this function is part of:
        //   - A client starts IO.
        //   - The plugin host (the HAL) calls the StartIO function in NovaLINK_PlugInInterface, which calls this function.
        //   - NovaLINKDriver sends a message to NovaLINKApp telling it to start the (real) audio hardware.
        //   - NovaLINKApp starts the hardware and, after the hardware is ready, replies to NovaLINKDriver's message.
        //   - NovaLINKDriver lets the host know that it's ready to do IO by returning from StartIO.
        
        // Update our client data.
        //
        // We add the work to the task queue, rather than doing it here, because BeginIOOperation and EndIOOperation
        // also add this task to the queue and the updates should be done in order.
        bool didStartIO = mTaskQueue.QueueSync_StartClientIO(&mClients, inClientID);
        
        // We only tell the hardware to start if this is the first time IO has been started.
        if(didStartIO)
        {
            kern_return_t theError = _HW_StartIO();
            ThrowIfKernelError(theError,
                               CAException(theError),
                               "NovaLINK_Device::StartIO: Failed to start because of an error calling down to the driver.");
        }
        
        clientIsPassthroughHost = mClients.IsPassthroughHost(inClientID);
        novaLINKAppHasClientRegistered = mClients.NovaLINKAppHasClientRegistered();
    }
    
    // Request that playthrough start. Prefer the companion app when it is registered; otherwise the
    // privileged XPCHelper hosts fallback playthrough to the system's non-NovaLINK default output.
    // On modern macOS we must not block StartIO waiting for the real output device — that deadlocks
    // with the HAL and freezes clients (Chrome/YouTube).
    if(!clientIsPassthroughHost)
    {
        DebugMsg("NovaLINK_Device::StartIO: Requesting playthrough start (non-blocking). appRegistered=%d",
                 novaLINKAppHasClientRegistered);
        const bool forUISoundsDevice = (GetObjectID() == kObjectID_Device_UI_Sounds);
        // Run the XPC round-trip off the StartIO stack so the HAL can finish client StartIO
        // immediately. Dropped initial frames are preferable to a hung Chrome/YouTube tab.
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
            UInt64 theXPCError = novaLINKAppHasClientRegistered
                    ? StartNovaLINKAppPlayThroughSync(forUISoundsDevice)
                    : StartFallbackPlayThroughSync(forUISoundsDevice);

            switch(theXPCError)
            {
                case kNovaLINKXPC_Success:
                    DebugMsg("NovaLINK_Device::StartIO: Playthrough ready.");
                    break;

                case kNovaLINKXPC_MessageFailure:
                    LogWarning("NovaLINK_Device::StartIO: Couldn't reach playthrough host via XPC.");
                    break;

                case kNovaLINKXPC_Timeout:
                    LogWarning("NovaLINK_Device::StartIO: Timed out waiting for playthrough host via XPC.");
                    break;

                case kNovaLINKXPC_ReturningEarlyError:
                    DebugMsg("NovaLINK_Device::StartIO: Playthrough host returned early (async start).");
                    break;

                default:
                    LogError("NovaLINK_Device::StartIO: Playthrough host failed to start the output device. theXPCError=%llu",
                             theXPCError);
                    break;
            }
        });
    }
}

void	NovaLINK_Device::StopIO(UInt32 inClientID)
{
    CAMutex::Locker theStateLocker(mStateMutex);
    
    // Update our client data.
    //
    // We add the work to the task queue, rather than doing it here, because BeginIOOperation and EndIOOperation also
    // add this task to the queue and the updates should be done in order.
    bool didStopIO = mTaskQueue.QueueSync_StopClientIO(&mClients, inClientID);
	
	//	we tell the hardware to stop if this is the last stop call
	if(didStopIO)
	{
		_HW_StopIO();
	}
}

void	NovaLINK_Device::GetZeroTimeStamp(Float64& outSampleTime, UInt64& outHostTime, UInt64& outSeed)
{
	// accessing the buffers requires holding the IO mutex
	CAMutex::Locker theIOLocker(mIOMutex);
    
    if(mWrappedAudioEngine != NULL)
    {
    }
    else
    {
        // Without a wrapped device, we base our timing on the host. This is mostly from Apple's NullAudio.c sample code
    	UInt64 theCurrentHostTime;
    	Float64 theHostTicksPerRingBuffer;
    	Float64 theHostTickOffset;
    	UInt64 theNextHostTime;
    	
    	//	get the current host time
        theCurrentHostTime = CAHostTimeBase::GetTheCurrentTime();
    	
    	//	calculate the next host time
    	theHostTicksPerRingBuffer = mLoopbackTime.hostTicksPerFrame * kLoopbackRingBufferFrameSize;
    	theHostTickOffset = static_cast<Float64>(mLoopbackTime.numberTimeStamps + 1) * theHostTicksPerRingBuffer;
    	theNextHostTime = mLoopbackTime.anchorHostTime + static_cast<UInt64>(theHostTickOffset);
    	
    	//	go to the next time if the next host time is less than the current time
    	if(theNextHostTime <= theCurrentHostTime)
    	{
            mLoopbackTime.numberTimeStamps++;
    	}
    	
    	//	set the return values
    	outSampleTime = static_cast<Float64>(mLoopbackTime.numberTimeStamps) *
                static_cast<Float64>(kLoopbackRingBufferFrameSize);
    	const UInt64 theHostTimeOffset =
                static_cast<UInt64>(static_cast<Float64>(mLoopbackTime.numberTimeStamps) * theHostTicksPerRingBuffer);
    	outHostTime = mLoopbackTime.anchorHostTime + theHostTimeOffset;
        // TODO: I think we should increment outSeed whenever this device switches to/from having a wrapped engine
    	outSeed = 1;
    }
}

void	NovaLINK_Device::WillDoIOOperation(UInt32 inOperationID, bool& outWillDo, bool& outWillDoInPlace) const
{
	switch(inOperationID)
	{
        case kAudioServerPlugInIOOperationThread:
        case kAudioServerPlugInIOOperationReadInput:
        case kAudioServerPlugInIOOperationProcessOutput:
		case kAudioServerPlugInIOOperationWriteMix:
			outWillDo = true;
			outWillDoInPlace = true;
			break;

        case kAudioServerPlugInIOOperationProcessMix:
            outWillDo = mVolumeControl.WillApplyVolumeToAudioRT();
            outWillDoInPlace = true;
            break;

		case kAudioServerPlugInIOOperationCycle:
        case kAudioServerPlugInIOOperationConvertInput:
        case kAudioServerPlugInIOOperationProcessInput:
		case kAudioServerPlugInIOOperationMixOutput:
		case kAudioServerPlugInIOOperationConvertMix:
		default:
			outWillDo = false;
			outWillDoInPlace = true;
			break;
			
	};
}

void	NovaLINK_Device::BeginIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID)
{
	#pragma unused(inIOBufferFrameSize, inIOCycleInfo)
    
    if(inOperationID == kAudioServerPlugInIOOperationThread)
    {
        // Update this client's IO state and send notifications if that changes the value of
        // kAudioDeviceCustomPropertyDeviceIsRunning or
        // kAudioDeviceCustomPropertyDeviceIsRunningSomewhereOtherThanNovaLINKApp. We have to do this here
        // as well as in StartIO because the HAL only calls StartIO/StopIO with the first/last clients.
        //
        // We perform the update async because it isn't real-time safe, but we can't just dispatch it with
        // dispatch_async because that isn't real-time safe either. (Apparently even constructing a block
        // isn't.)
        //
        // We don't have to hold the IO mutex here because mTaskQueue and mClients don't change and
        // adding a task to mTaskQueue is thread safe.
        mTaskQueue.QueueAsync_StartClientIO(&mClients, inClientID);
    }
}

void	NovaLINK_Device::DoIOOperation(AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer)
{
    #pragma unused(inStreamObjectID, ioSecondaryBuffer)
    
	switch(inOperationID)
	{
		case kAudioServerPlugInIOOperationReadInput:
            {
                // Capture clients (Zoom/OBS) trigger mic inject demand; output-only clients do not.
                // Only queue once per IO session — ReadInput runs every cycle.
                if(SupportsMicMix() && mClients.ClientShouldMarkInputIORT(inClientID))
                {
                    mTaskQueue.QueueAsync_StartClientInputIO(&mClients, inClientID);
                }

                CAMutex::Locker theIOLocker(mIOMutex);

                // Copy the audio data out of our ring buffer.
                //
                // Take the IO mutex because, in testing, not taking it seemed to make this function
                // occasionally miss its deadline and cause an audio glitch. It's hard to be sure
                // that was actually the cause, but it's probably not worth the risk anyway.
                //
                // If an IO operation misses its deadline, the host will log this message:
                //     Audio IO Overload inputs: '<private>' outputs: '<private>' cause: 'Unknown'
                //     prewarming: no recovering: no
                //
                // Passthrough hosts (App / XPCHelper) get desktop loopback only so speakers do not
                // play the injected mic. Other clients (Zoom, OBS, …) get desktop + mic.
                const bool mixMic = SupportsMicMix() && !mClients.IsPassthroughHost(inClientID);
                ReadInputData(inIOBufferFrameSize,
                              inIOCycleInfo.mInputTime.mSampleTime,
                              ioMainBuffer,
                              mixMic);
            }
			break;
            
        case kAudioServerPlugInIOOperationProcessOutput:
            {
                bool theClientIsMusicPlayer = mClients.IsMusicPlayerRT(inClientID);
                
                CAMutex::Locker theIOLocker(mIOMutex);
                // Called in this IO operation so we can get the music player client's data separately
				mAudibleState.UpdateWithClientIO(theClientIsMusicPlayer,
												 inIOBufferFrameSize,
												 inIOCycleInfo.mOutputTime.mSampleTime,
												 reinterpret_cast<const Float32*>(ioMainBuffer));
            }
            break;

        case kAudioServerPlugInIOOperationProcessMix:
            {
                // Check the arguments.
                ThrowIfNULL(ioMainBuffer,
                            CAException(kAudioHardwareIllegalOperationError),
                            "NovaLINK_Device::DoIOOperation: Buffer for "
                                    "kAudioServerPlugInIOOperationProcessMix must not be null");

                CAMutex::Locker theIOLocker(mIOMutex);

                // We ask to do this IO operation so this device can apply its own volume to the
                // stream. Currently, only the UI sounds device does.
                mVolumeControl.ApplyVolumeToAudioRT(reinterpret_cast<Float32*>(ioMainBuffer),
                                                    inIOBufferFrameSize);
            }
            break;

        case kAudioServerPlugInIOOperationWriteMix:
            {
                CAMutex::Locker theIOLocker(mIOMutex);

                bool didChangeState =
                        mAudibleState.UpdateWithMixedIO(
                                inIOBufferFrameSize,
                                inIOCycleInfo.mOutputTime.mSampleTime,
                                reinterpret_cast<const Float32*>(ioMainBuffer));

                if(didChangeState)
                {
                    // Send notifications.
                    mTaskQueue.QueueAsync_SendPropertyNotification(
							kAudioDeviceCustomPropertyDeviceAudibleState, GetObjectID());
                }

                // Copy the audio data into our ring buffer.
                WriteOutputData(inIOBufferFrameSize,
                                inIOCycleInfo.mOutputTime.mSampleTime,
                                ioMainBuffer);

                if(mAudioCapture.IsCapturing()) {
                    mAudioCapture.EnqueueAudioData(reinterpret_cast<const Float32*>(ioMainBuffer),
                                                   inIOBufferFrameSize);
                }
            }
			break;

		default:
            // Note that this will only log the error in debug builds.
			DebugMsg("NovaLINK_Device::DoIOOperation: Unexpected IO operation: %u", inOperationID);
			break;
	};
}

void	NovaLINK_Device::EndIOOperation(UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo& inIOCycleInfo, UInt32 inClientID)
{
    #pragma unused(inIOBufferFrameSize, inIOCycleInfo)

    if(inOperationID == kAudioServerPlugInIOOperationThread)
    {
        // Tell NovaLINK_Clients that this client has stopped IO. Queued async because we have to be real-time safe here.
        //
        // We don't have to hold the IO mutex here because mTaskQueue and mClients don't change and adding a task to
        // mTaskQueue is thread safe.
        mTaskQueue.QueueAsync_StopClientIO(&mClients, inClientID);
    }
}

void	NovaLINK_Device::ReadInputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, void* outBuffer, bool inMixMic)
{
    // Wrap the provided buffer in an AudioBufferList.
    AudioBufferList abl = {};
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = 2;
    // Each frame is 2 Float32 samples (one per channel). The number of frames * the number
    // of bytes per frame = the size of outBuffer in bytes.
    abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(inIOBufferFrameSize * sizeof(Float32) * 2);
    abl.mBuffers[0].mData = outBuffer;

    // Copy the audio data from our ring buffer into the provided buffer.
    //
    // Use the HAL-provided sample time as-is. Seeking to "liveEdge" when the reader
    // falls behind skips/repeats frames and produces severe noise on remote capture
    // (osxaudiosrc). CARingBuffer already returns silence (CPUOverload) for out-of-
    // bounds reads; that is preferable to discontinuous PCM.
    CARingBufferError err =
            mLoopbackRingBuffer.Fetch(&abl,
                                      inIOBufferFrameSize,
                                      static_cast<CARingBuffer::SampleTime>(inSampleTime));

    // Handle errors.
    switch (err)
    {
        case kCARingBufferError_CPUOverload:
            // Write silence to the buffer.
            memset(outBuffer, 0, abl.mBuffers[0].mDataByteSize);
            break;
        case kCARingBufferError_TooMuch:
            // Should be impossible, but handle it just in case. Write silence to the buffer and
            // return an error code.
            memset(outBuffer, 0, abl.mBuffers[0].mDataByteSize);
            Throw(CAException(kAudioHardwareIllegalOperationError));
        case kCARingBufferError_OK:
            break;
        default:
            throw CAException(kAudioHardwareUnspecifiedError);
    }

    if(!inMixMic || !mMicRingAllocated || mMicSampleTime < static_cast<CARingBuffer::SampleTime>(inIOBufferFrameSize))
    {
        return;
    }

    if(mMicMixScratchFrames < inIOBufferFrameSize || !mMicMixScratch)
    {
        return;
    }

    // Mix the most recently injected mic frames on top of the desktop loopback.
    const CARingBuffer::SampleTime micReadTime = mMicSampleTime - inIOBufferFrameSize;
    Float32* const micScratch = mMicMixScratch.get();
    AudioBufferList micAbl = {};
    micAbl.mNumberBuffers = 1;
    micAbl.mBuffers[0].mNumberChannels = 2;
    micAbl.mBuffers[0].mDataByteSize = abl.mBuffers[0].mDataByteSize;
    micAbl.mBuffers[0].mData = micScratch;

    const CARingBufferError micErr =
            mMicRingBuffer.Fetch(&micAbl, inIOBufferFrameSize, micReadTime);
    if(micErr != kCARingBufferError_OK)
    {
        return;
    }

    Float32* const outSamples = static_cast<Float32*>(outBuffer);
    const UInt32 sampleCount = inIOBufferFrameSize * 2;
    for(UInt32 i = 0; i < sampleCount; i++)
    {
        const Float32 mixed = outSamples[i] + micScratch[i];
        outSamples[i] = (mixed > 1.0f) ? 1.0f : ((mixed < -1.0f) ? -1.0f : mixed);
    }
}

void	NovaLINK_Device::InjectMicAudio(const Float32* inSamples, UInt32 inFrameCount)
{
    if(!mMicRingAllocated || inFrameCount == 0)
    {
        return;
    }

    // Cap a single inject so a misbehaving client cannot overrun the ring in one call.
    if(inFrameCount > kLoopbackRingBufferFrameSize / 2)
    {
        inFrameCount = kLoopbackRingBufferFrameSize / 2;
    }

    CAMutex::Locker theIOLocker(mIOMutex);

    AudioBufferList abl = {};
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = 2;
    abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(inFrameCount * sizeof(Float32) * 2);
    abl.mBuffers[0].mData = const_cast<Float32*>(inSamples);

    const CARingBufferError err =
            mMicRingBuffer.Store(&abl, inFrameCount, mMicSampleTime);
    if(err == kCARingBufferError_OK || err == kCARingBufferError_CPUOverload)
    {
        mMicSampleTime += inFrameCount;
    }
}

void	NovaLINK_Device::WriteOutputData(UInt32 inIOBufferFrameSize, Float64 inSampleTime, const void* inBuffer)
{
    // Wrap the provided buffer in an AudioBufferList.
    AudioBufferList abl = {};
    abl.mNumberBuffers = 1;
    abl.mBuffers[0].mNumberChannels = 2;
    // Each frame is 2 Float32 samples (one per channel). The number of frames * the number
    // of bytes per frame = the size of inBuffer in bytes.
    abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(inIOBufferFrameSize * sizeof(Float32) * 2);
    abl.mBuffers[0].mData = const_cast<void *>(inBuffer);

    // Copy the audio data from the provided buffer into our ring buffer.
    CARingBufferError err =
            mLoopbackRingBuffer.Store(&abl,
                                      inIOBufferFrameSize,
                                      static_cast<CARingBuffer::SampleTime>(inSampleTime));

    // Return an error code if we failed to store the data. (But ignore CPU overload, which would be
    // temporary.)
    if (err != kCARingBufferError_OK && err != kCARingBufferError_CPUOverload)
    {
        Throw(CAException(err));
    }
}

#pragma mark Accessors

void    NovaLINK_Device::RequestEnabledControls(bool inVolumeEnabled, bool inMuteEnabled)
{
    CAMutex::Locker theStateLocker(mStateMutex);

    bool changeVolume = (mVolumeControl.IsActive() != inVolumeEnabled);
    bool changeMute = (mMuteControl.IsActive() != inMuteEnabled);

    if(changeVolume)
    {
        DebugMsg("NovaLINK_Device::RequestEnabledControls: %s volume control",
                 (inVolumeEnabled ? "Enabling" : "Disabling"));
        mPendingOutputVolumeControlEnabled = inVolumeEnabled;
    }

    if(changeMute)
    {
        DebugMsg("NovaLINK_Device::RequestEnabledControls: %s mute control",
                 (inMuteEnabled ? "Enabling" : "Disabling"));
        mPendingOutputMuteControlEnabled = inMuteEnabled;
    }

    if(changeVolume || changeMute)
    {
        // Ask the host to stop IO (and whatever else) so we can safely update the device's list of
        // controls. See RequestDeviceConfigurationChange in AudioServerPlugIn.h.
        AudioObjectID theDeviceObjectID = GetObjectID();
        UInt64 action = static_cast<UInt64>(ChangeAction::SetEnabledControls);
        
        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false,	^{
            NovaLINK_PlugIn::Host_RequestDeviceConfigurationChange(theDeviceObjectID, action, nullptr);
        });
    }
}

Float64	NovaLINK_Device::GetSampleRate() const
{
    // The sample rate is guarded by the state lock. Note that we don't need to take the IO lock.
    CAMutex::Locker theStateLocker(mStateMutex);

    Float64 theSampleRate;

    // Report the sample rate from the wrapped device if we have one. Note that _HW_GetSampleRate
    // the device's nominal sample rate, not one calculated from its timestamps.
    if(mWrappedAudioEngine == nullptr)
    {
        theSampleRate = mLoopbackSampleRate;
    }
    else
    {
        theSampleRate = _HW_GetSampleRate();
    }

    return theSampleRate;
}

void	NovaLINK_Device::RequestSampleRate(Float64 inRequestedSampleRate)
{
    // Changing the sample rate needs to be handled via the RequestConfigChange/PerformConfigChange
    // machinery. See RequestDeviceConfigurationChange in AudioServerPlugIn.h.

	// We try to support any sample rate a real output device might.
    ThrowIf(inRequestedSampleRate < 1.0,
            CAException(kAudioDeviceUnsupportedFormatError),
            "NovaLINK_Device::RequestSampleRate: unsupported sample rate");

    DebugMsg("NovaLINK_Device::RequestSampleRate: Sample rate change requested: %f",
             inRequestedSampleRate);

    CAMutex::Locker theStateLocker(mStateMutex);

    if(inRequestedSampleRate != GetSampleRate())  // Check the sample rate will actually be changed.
    {
        mPendingSampleRate = inRequestedSampleRate;

        // Dispatch this so the change can happen asynchronously.
        auto requestSampleRate = ^{
			UInt64 action = static_cast<UInt64>(ChangeAction::SetSampleRate);
            NovaLINK_PlugIn::Host_RequestDeviceConfigurationChange(GetObjectID(), action, nullptr);
        };

        CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, requestSampleRate);
    }
}

NovaLINK_Object&  NovaLINK_Device::GetOwnedObjectByID(AudioObjectID inObjectID)
{
	// C++ is weird. See "Avoid Duplication in const and Non-const Member Functions" in Item 3 of Effective C++.
	return const_cast<NovaLINK_Object&>(static_cast<const NovaLINK_Device&>(*this).GetOwnedObjectByID(inObjectID));
}

const NovaLINK_Object&  NovaLINK_Device::GetOwnedObjectByID(AudioObjectID inObjectID) const
{
	if(inObjectID == mInputStream.GetObjectID())
	{
		return mInputStream;
	}
	else if(inObjectID == mOutputStream.GetObjectID())
	{
		return mOutputStream;
	}
	else if(inObjectID == mVolumeControl.GetObjectID())
	{
		return mVolumeControl;
	}
	else if(inObjectID == mMuteControl.GetObjectID())
	{
		return mMuteControl;
	}
	else
	{
		LogError("NovaLINK_Device::GetOwnedObjectByID: Unknown object ID. inObjectID = %u", inObjectID);
		Throw(CAException(kAudioHardwareBadObjectError));
	}
}

UInt32	NovaLINK_Device::GetNumberOfSubObjects() const
{
	return kNumberOfInputSubObjects + GetNumberOfOutputSubObjects();
}

UInt32	NovaLINK_Device::GetNumberOfOutputSubObjects() const
{
	return kNumberOfOutputStreams + GetNumberOfOutputControls();
}

UInt32	NovaLINK_Device::GetNumberOfOutputControls() const
{
	CAMutex::Locker theStateLocker(mStateMutex);

	UInt32 theAnswer = 0;

	if(mVolumeControl.IsActive())
	{
		theAnswer++;
	}

	if(mMuteControl.IsActive())
	{
		theAnswer++;
	}

    return theAnswer;
}

void    NovaLINK_Device::SetEnabledControls(bool inVolumeEnabled, bool inMuteEnabled)
{
    CAMutex::Locker theStateLocker(mStateMutex);

    if(mVolumeControl.IsActive() != inVolumeEnabled)
    {
        DebugMsg("NovaLINK_Device::SetEnabledControls: %s the volume control",
                 inVolumeEnabled ? "Enabling" : "Disabling");

        if(inVolumeEnabled)
		{
			mVolumeControl.Activate();
		}
		else
		{
			mVolumeControl.Deactivate();
		}
    }

    if(mMuteControl.IsActive() != inMuteEnabled)
    {
        DebugMsg("NovaLINK_Device::SetEnabledControls: %s the mute control",
                 inMuteEnabled ? "Enabling" : "Disabling");

        if(inMuteEnabled)
		{
			mMuteControl.Activate();
		}
		else
		{
			mMuteControl.Deactivate();
		}
    }
}

void NovaLINK_Device::SetSampleRate(Float64 inSampleRate, bool force)
{
    // We try to support any sample rate a real output device might.
    ThrowIf(inSampleRate < 1.0,
            CAException(kAudioDeviceUnsupportedFormatError),
            "NovaLINK_Device::SetSampleRate: unsupported sample rate");

    CAMutex::Locker theStateLocker(mStateMutex);

    Float64 theCurrentSampleRate = GetSampleRate();

    if((inSampleRate != theCurrentSampleRate) || force)  // Check whether we need to change it.
    {
        DebugMsg("NovaLINK_Device::SetSampleRate: Changing the sample rate from %f to %f",
                 theCurrentSampleRate,
                 inSampleRate);

        // Update the sample rate on the wrapped device if we have one.
        if(mWrappedAudioEngine != nullptr)
        {
            kern_return_t theError = _HW_SetSampleRate(inSampleRate);
            ThrowIfKernelError(theError,
                               CAException(kAudioHardwareUnspecifiedError),
                               "NovaLINK_Device::SetSampleRate: Error setting the sample rate on the "
                               "wrapped audio device.");
        }

        // Update the sample rate for loopback.
        mLoopbackSampleRate = inSampleRate;
        InitLoopback();

        // Update the streams.
        mInputStream.SetSampleRate(inSampleRate);
        mOutputStream.SetSampleRate(inSampleRate);
    }
    else
    {
        DebugMsg("NovaLINK_Device::SetSampleRate: The sample rate is already set to %f", inSampleRate);
    }
}

bool    NovaLINK_Device::IsStreamID(AudioObjectID inObjectID) const noexcept
{
    return (inObjectID == mInputStream.GetObjectID()) || (inObjectID == mOutputStream.GetObjectID());
}

#pragma mark Hardware Accessors

// TODO: Out of laziness, some of these hardware functions do more than their names suggest

void	NovaLINK_Device::_HW_Open()
{
}

void	NovaLINK_Device::_HW_Close()
{
}

kern_return_t	NovaLINK_Device::_HW_StartIO()
{
	NovaLINKAssert(mStateMutex.IsOwnedByCurrentThread(),
              "NovaLINK_Device::_HW_StartIO: Called without taking the state mutex");

    if(mWrappedAudioEngine != nullptr)
    {
    }
    
    // Reset the loopback timing values
    mLoopbackTime.numberTimeStamps = 0;
    mLoopbackTime.anchorHostTime = CAHostTimeBase::GetTheCurrentTime();
    // ...and the most-recent audible/silent sample times. mAudibleState is usually guarded by the
	// IO mutex, but we haven't started IO yet (and this function can only be called by one thread
	// at a time).
	NovaLINKAssert(mIOMutex.IsFree(), "NovaLINK_Device::_HW_StartIO: IO mutex taken before starting IO");
    mAudibleState.Reset();
    
    return KERN_SUCCESS;
}

void	NovaLINK_Device::_HW_StopIO()
{
    if(mWrappedAudioEngine != NULL)
    {
    }
}

Float64	NovaLINK_Device::_HW_GetSampleRate() const
{
    // This function should only be called when wrapping a device.
    ThrowIf(mWrappedAudioEngine == nullptr,
            CAException(kAudioHardwareUnspecifiedError),
            "NovaLINK_Device::_HW_GetSampleRate: No wrapped audio device");

    return static_cast<Float64>(mWrappedAudioEngine->GetSampleRate());
}

kern_return_t	NovaLINK_Device::_HW_SetSampleRate(Float64 inNewSampleRate)
{
    // This function should only be called when wrapping a device.
    ThrowIf(mWrappedAudioEngine == nullptr,
            CAException(kAudioHardwareUnspecifiedError),
            "NovaLINK_Device::_HW_SetSampleRate: No wrapped audio device");

    return mWrappedAudioEngine->SetSampleRate(inNewSampleRate);
}

UInt32	NovaLINK_Device::_HW_GetRingBufferFrameSize() const
{
    return (mWrappedAudioEngine != NULL) ? mWrappedAudioEngine->GetSampleBufferFrameSize() : 0;
}

#pragma mark Implementation

void	NovaLINK_Device::AddClient(const AudioServerPlugInClientInfo* inClientInfo)
{
    DebugMsg("NovaLINK_Device::AddClient: Adding client %u (%s)",
             inClientInfo->mClientID,
             (inClientInfo->mBundleID == NULL ?
                 "no bundle ID" :
                 CFStringGetCStringPtr(inClientInfo->mBundleID, kCFStringEncodingUTF8)));
    
    CAMutex::Locker theStateLocker(mStateMutex);

    mClients.AddClient(inClientInfo);
}

void	NovaLINK_Device::RemoveClient(const AudioServerPlugInClientInfo* inClientInfo)
{
    DebugMsg("NovaLINK_Device::RemoveClient: Removing client %u (%s)",
             inClientInfo->mClientID,
             CFStringGetCStringPtr(inClientInfo->mBundleID, kCFStringEncodingUTF8));
    
    CAMutex::Locker theStateLocker(mStateMutex);

    // If we're removing NovaLINKApp, reenable all of NovaLINKDevice's controls.
    if(mClients.IsNovaLINKApp(inClientInfo->mClientID))
    {
        RequestEnabledControls(true, true);
    }

    mClients.RemoveClient(inClientInfo->mClientID);
}

void	NovaLINK_Device::PerformConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeInfo)
    DebugMsg("NovaLINK_Device::PerformConfigChange: inChangeAction = %llu", inChangeAction);

    // Apply a change requested with NovaLINK_PlugIn::Host_RequestDeviceConfigurationChange. See
    // PerformDeviceConfigurationChange in AudioServerPlugIn.h.

    switch(static_cast<ChangeAction>(inChangeAction))
    {
        case ChangeAction::SetSampleRate:
            SetSampleRate(mPendingSampleRate);
            break;

        case ChangeAction::SetEnabledControls:
            SetEnabledControls(mPendingOutputVolumeControlEnabled,
                               mPendingOutputMuteControlEnabled);
            break;
    }
}

void	NovaLINK_Device::AbortConfigChange(UInt64 inChangeAction, void* inChangeInfo)
{
	#pragma unused(inChangeAction, inChangeInfo)
	
	//	this device doesn't need to do anything special if a change request gets aborted
}


void NovaLINK_Device::StartAudioCapture(NovaLINK_AudioCapture::AudioDataCallback callback) {
    mAudioCapture.StartCapture(callback);
}

void NovaLINK_Device::StopAudioCapture() {
    mAudioCapture.StopCapture();
}
