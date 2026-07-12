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
//  NovaLINK_PlugIn.cpp
//  NovaLINKDriver
//
//  Copyright © 2016, 2017 Kyle Neideck
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_PlugIn.cpp from Apple's SimpleAudioDriver Plug-In sample code.
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

//	Self Include
#include "NovaLINK_PlugIn.h"

//  Local Includes
#include "NovaLINK_Device.h"
#include "NovaLINK_NullDevice.h"

//  PublicUtility Includes
#include "CAException.h"
#include "CADebugMacros.h"
#include "CAPropertyAddress.h"
#include "CADispatchQueue.h"


#pragma mark Construction/Destruction

pthread_once_t				NovaLINK_PlugIn::sStaticInitializer = PTHREAD_ONCE_INIT;
NovaLINK_PlugIn*					NovaLINK_PlugIn::sInstance = NULL;
AudioServerPlugInHostRef	NovaLINK_PlugIn::sHost = NULL;

NovaLINK_PlugIn& NovaLINK_PlugIn::GetInstance()
{
    pthread_once(&sStaticInitializer, StaticInitializer);
    return *sInstance;
}

void	NovaLINK_PlugIn::StaticInitializer()
{
    try
    {
        sInstance = new NovaLINK_PlugIn;
        sInstance->Activate();
    }
    catch(...)
    {
        DebugMsg("NovaLINK_PlugIn::StaticInitializer: failed to create the plug-in");
        delete sInstance;
        sInstance = NULL;
    }
}

NovaLINK_PlugIn::NovaLINK_PlugIn()
:
	NovaLINK_Object(kAudioObjectPlugInObject, kAudioPlugInClassID, kAudioObjectClassID, 0),
	mMutex("NovaLINK_PlugIn")
{
}

NovaLINK_PlugIn::~NovaLINK_PlugIn()
{
}

void	NovaLINK_PlugIn::Deactivate()
{
	CAMutex::Locker theLocker(mMutex);
	NovaLINK_Object::Deactivate();
    // TODO:
	//_RemoveAllDevices();
}

#pragma mark Property Operations

bool	NovaLINK_PlugIn::HasProperty(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyManufacturer:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
        case kAudioObjectPropertyCustomPropertyInfoList:
        case kAudioPlugInCustomPropertyNullDeviceActive:
			theAnswer = true;
			break;
		
		default:
			theAnswer = NovaLINK_Object::HasProperty(inObjectID, inClientPID, inAddress);
	};
	return theAnswer;
}

bool	NovaLINK_PlugIn::IsPropertySettable(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress) const
{
	bool theAnswer = false;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyManufacturer:
		case kAudioPlugInPropertyDeviceList:
		case kAudioPlugInPropertyTranslateUIDToDevice:
        case kAudioPlugInPropertyResourceBundle:
        case kAudioObjectPropertyCustomPropertyInfoList:
			theAnswer = false;
			break;

        case kAudioPlugInCustomPropertyNullDeviceActive:
            theAnswer = true;
            break;
		
		default:
			theAnswer = NovaLINK_Object::IsPropertySettable(inObjectID, inClientPID, inAddress);
	};
	return theAnswer;
}

UInt32	NovaLINK_PlugIn::GetPropertyDataSize(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData) const
{
	UInt32 theAnswer = 0;
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyManufacturer:
			theAnswer = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
		case kAudioPlugInPropertyDeviceList:
            // The plug-in owns the main NovaLINK_Device, the instance of NovaLINK_Device that handles UI
            // sounds and, if it's enabled, the null device.
            theAnswer = (NovaLINK_NullDevice::GetInstance().IsActive() ? 3 : 2) * sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:
			theAnswer = sizeof(AudioObjectID);
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			theAnswer = sizeof(CFStringRef);
			break;

        case kAudioObjectPropertyCustomPropertyInfoList:
            theAnswer = sizeof(AudioServerPlugInCustomPropertyInfo);
            break;

        case kAudioPlugInCustomPropertyNullDeviceActive:
            theAnswer = sizeof(CFBooleanRef);
            break;
		
		default:
			theAnswer = NovaLINK_Object::GetPropertyDataSize(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData);
	};
	return theAnswer;
}

void	NovaLINK_PlugIn::GetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32& outDataSize, void* outData) const
{
	switch(inAddress.mSelector)
	{
		case kAudioObjectPropertyManufacturer:
			//	This is the human readable name of the maker of the plug-in.
			ThrowIf(inDataSize < sizeof(CFStringRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_PlugIn::GetPropertyData: not enough space for the return value of kAudioObjectPropertyManufacturer");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR("THENURIM");
			outDataSize = sizeof(CFStringRef);
			break;
			
		case kAudioObjectPropertyOwnedObjects:
            // Fall through because this plug-in object only owns the devices.
		case kAudioPlugInPropertyDeviceList:
            {
    			AudioObjectID* theReturnedDeviceList = reinterpret_cast<AudioObjectID*>(outData);
                if(inDataSize >= 3 * sizeof(AudioObjectID))
                {
                    if(NovaLINK_NullDevice::GetInstance().IsActive())
                    {
                        theReturnedDeviceList[0] = kObjectID_Device;
                        theReturnedDeviceList[1] = kObjectID_Device_UI_Sounds;
                        theReturnedDeviceList[2] = kObjectID_Device_Null;
                        
                        //	say how much we returned
                        outDataSize = 3 * sizeof(AudioObjectID);
                    }
                    else
                    {
                        theReturnedDeviceList[0] = kObjectID_Device;
                        theReturnedDeviceList[1] = kObjectID_Device_UI_Sounds;

                        //	say how much we returned
                        outDataSize = 2 * sizeof(AudioObjectID);
                    }
                }
                else if(inDataSize >= 2 * sizeof(AudioObjectID))
                {
                    theReturnedDeviceList[0] = kObjectID_Device;
                    theReturnedDeviceList[1] = kObjectID_Device_UI_Sounds;

                    //	say how much we returned
                    outDataSize = 2 * sizeof(AudioObjectID);
                }
                else if(inDataSize >= sizeof(AudioObjectID))
                {
                    theReturnedDeviceList[0] = kObjectID_Device;
                    outDataSize = sizeof(AudioObjectID);
                }
                else
                {
                    outDataSize = 0;
                }
            }
			break;
			
		case kAudioPlugInPropertyTranslateUIDToDevice:
            {
                //	This property translates the UID passed in the qualifier as a CFString into the
                //	AudioObjectID for the device the UID refers to or kAudioObjectUnknown if no device
                //	has the UID.
                ThrowIf(inQualifierDataSize < sizeof(CFStringRef), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_PlugIn::GetPropertyData: the qualifier size is too small for kAudioPlugInPropertyTranslateUIDToDevice");
                ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_PlugIn::GetPropertyData: not enough space for the return value of kAudioPlugInPropertyTranslateUIDToDevice");

                CFStringRef theUID = *reinterpret_cast<const CFStringRef*>(inQualifierData);
                AudioObjectID* outID = reinterpret_cast<AudioObjectID*>(outData);

                if(CFEqual(theUID, NovaLINK_Device::GetInstance().CopyDeviceUID()))
                {
                    DebugMsg("NovaLINK_PlugIn::GetPropertyData: Returning NovaLINKDevice for "
                             "kAudioPlugInPropertyTranslateUIDToDevice");
                    *outID = kObjectID_Device;
                }
                else if(CFEqual(theUID, NovaLINK_Device::GetUISoundsInstance().CopyDeviceUID()))
                {
                    DebugMsg("NovaLINK_PlugIn::GetPropertyData: Returning NovaLINKUISoundsDevice for "
                             "kAudioPlugInPropertyTranslateUIDToDevice");
                    *outID = kObjectID_Device_UI_Sounds;
                }
                else if(NovaLINK_NullDevice::GetInstance().IsActive() &&
                        CFEqual(theUID, NovaLINK_NullDevice::GetInstance().CopyDeviceUID()))
                {
                    DebugMsg("NovaLINK_PlugIn::GetPropertyData: Returning null device for "
                             "kAudioPlugInPropertyTranslateUIDToDevice");
                    *outID = kObjectID_Device_Null;
                }
                else
                {
                    LogWarning("NovaLINK_PlugIn::GetPropertyData: Returning kAudioObjectUnknown for "
                               "kAudioPlugInPropertyTranslateUIDToDevice");
                    *outID = kAudioObjectUnknown;
                }

                outDataSize = sizeof(AudioObjectID);
            }
			break;
			
		case kAudioPlugInPropertyResourceBundle:
			//	The resource bundle is a path relative to the path of the plug-in's bundle.
			//	To specify that the plug-in bundle itself should be used, we just return the
			//	empty string.
			ThrowIf(inDataSize < sizeof(AudioObjectID), CAException(kAudioHardwareBadPropertySizeError), "NovaLINK_GetPlugInPropertyData: not enough space for the return value of kAudioPlugInPropertyResourceBundle");
			*reinterpret_cast<CFStringRef*>(outData) = CFSTR("");
			outDataSize = sizeof(CFStringRef);
			break;

        case kAudioObjectPropertyCustomPropertyInfoList:
            if(inDataSize >= sizeof(AudioServerPlugInCustomPropertyInfo))
            {
                AudioServerPlugInCustomPropertyInfo* outCustomProperties =
                    reinterpret_cast<AudioServerPlugInCustomPropertyInfo*>(outData);

                outCustomProperties[0].mSelector = kAudioPlugInCustomPropertyNullDeviceActive;
                outCustomProperties[0].mPropertyDataType =
                    kAudioServerPlugInCustomPropertyDataTypeCFPropertyList;
                outCustomProperties[0].mQualifierDataType =
                    kAudioServerPlugInCustomPropertyDataTypeNone;

                outDataSize = sizeof(AudioServerPlugInCustomPropertyInfo);
            }
            else
            {
                outDataSize = 0;
            }
            break;

        case kAudioPlugInCustomPropertyNullDeviceActive:
            ThrowIf(inDataSize < sizeof(CFBooleanRef),
                    CAException(kAudioHardwareBadPropertySizeError),
                    "NovaLINK_PlugIn::GetPropertyData: not enough space for the return value of "
                    "kAudioPlugInCustomPropertyNullDeviceActive");
            *reinterpret_cast<CFBooleanRef*>(outData) =
                NovaLINK_NullDevice::GetInstance().IsActive() ? kCFBooleanTrue : kCFBooleanFalse;
            outDataSize = sizeof(CFBooleanRef);
            break;

		default:
			NovaLINK_Object::GetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, outDataSize, outData);
			break;
	};
}

void	NovaLINK_PlugIn::SetPropertyData(AudioObjectID inObjectID, pid_t inClientPID, const AudioObjectPropertyAddress& inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	switch(inAddress.mSelector)
	{
        case kAudioPlugInCustomPropertyNullDeviceActive:
            {
                ThrowIf(inDataSize < sizeof(CFBooleanRef),
                        CAException(kAudioHardwareBadPropertySizeError),
                        "NovaLINK_PlugIn::SetPropertyData: wrong size for the data for "
                        "kAudioPlugInCustomPropertyNullDeviceActive");

                CFBooleanRef theIsActiveRef = *reinterpret_cast<const CFBooleanRef*>(inData);

                ThrowIfNULL(theIsActiveRef,
                            CAException(kAudioHardwareIllegalOperationError),
                            "NovaLINK_PlugIn::SetPropertyData: null reference given for "
                            "kAudioPlugInCustomPropertyNullDeviceActive");
                ThrowIf(CFGetTypeID(theIsActiveRef) != CFBooleanGetTypeID(),
                        CAException(kAudioHardwareIllegalOperationError),
                        "NovaLINK_PlugIn::SetPropertyData: CFType given for "
                        "kAudioPlugInCustomPropertyNullDeviceActive was not a CFBoolean");

                bool theIsActive = CFBooleanGetValue(theIsActiveRef);

                if(theIsActive != NovaLINK_NullDevice::GetInstance().IsActive())
                {
                    // Activate/deactivate the Null Device. We only make it active for a short
                    // period, while changing output device in NovaLINKApp, so it can be hidden from the
                    // user.
                    if(theIsActive)
                    {
                        DebugMsg("NovaLINK_PlugIn::SetPropertyData: Activating null device");
                        NovaLINK_NullDevice::GetInstance().Activate();
                    }
                    else
                    {
                        DebugMsg("NovaLINK_PlugIn::SetPropertyData: Deactivating null device");
                        NovaLINK_NullDevice::GetInstance().Deactivate();
                    }

                    // Send notifications.
                    CADispatchQueue::GetGlobalSerialQueue().Dispatch(false, ^{
                        AudioObjectPropertyAddress theChangedProperties[] = {
                            CAPropertyAddress(kAudioObjectPropertyOwnedObjects),
                            CAPropertyAddress(kAudioPlugInPropertyDeviceList)
                        };

                        Host_PropertiesChanged(GetObjectID(), 2, theChangedProperties);
                    });
                }
            }
            break;
            
		default:
			NovaLINK_Object::SetPropertyData(inObjectID, inClientPID, inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			break;
	};
}

