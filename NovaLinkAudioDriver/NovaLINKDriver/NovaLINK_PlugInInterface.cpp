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
//  NovaLINK_PlugInInterface.cpp
//  NovaLINKDriver
//
//  Copyright © 2016, 2017 Kyle Neideck
//  Copyright (C) 2013 Apple Inc. All Rights Reserved.
//
//  Based largely on SA_PlugIn.cpp from Apple's SimpleAudioDriver Plug-In sample code.
//  https://developer.apple.com/library/mac/samplecode/AudioDriverExamples
//

//	System Includes
#include <CoreAudio/AudioServerPlugIn.h>

//	PublicUtility Includes
#include "CADebugMacros.h"
#include "CAException.h"

//  Local Includes
#include "NovaLINK_Types.h"
#include "NovaLINK_Object.h"
#include "NovaLINK_PlugIn.h"
#include "NovaLINK_Device.h"
#include "NovaLINK_NullDevice.h"


#pragma mark COM Prototypes

//	Entry points for the COM methods
extern "C" void*	NovaLINK_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID);
static HRESULT		NovaLINK_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface);
static ULONG		NovaLINK_AddRef(void* inDriver);
static ULONG		NovaLINK_Release(void* inDriver);
static OSStatus		NovaLINK_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost);
static OSStatus		NovaLINK_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID);
static OSStatus		NovaLINK_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID);
static OSStatus		NovaLINK_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus		NovaLINK_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo);
static OSStatus		NovaLINK_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static OSStatus		NovaLINK_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo);
static Boolean		NovaLINK_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress);
static OSStatus		NovaLINK_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable);
static OSStatus		NovaLINK_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize);
static OSStatus		NovaLINK_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData);
static OSStatus		NovaLINK_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData);
static OSStatus		NovaLINK_StartIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus		NovaLINK_StopIO(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID);
static OSStatus		NovaLINK_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, Float64* outSampleTime, UInt64* outHostTime, UInt64* outSeed);
static OSStatus		NovaLINK_WillDoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, Boolean* outWillDo, Boolean* outWillDoInPlace);
static OSStatus		NovaLINK_BeginIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);
static OSStatus		NovaLINK_DoIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, AudioObjectID inStreamObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo, void* ioMainBuffer, void* ioSecondaryBuffer);
static OSStatus		NovaLINK_EndIOOperation(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt32 inClientID, UInt32 inOperationID, UInt32 inIOBufferFrameSize, const AudioServerPlugInIOCycleInfo* inIOCycleInfo);

#pragma mark The COM Interface

static AudioServerPlugInDriverInterface	gAudioServerPlugInDriverInterface =
{
	NULL,
	NovaLINK_QueryInterface,
	NovaLINK_AddRef,
	NovaLINK_Release,
	NovaLINK_Initialize,
	NovaLINK_CreateDevice,
	NovaLINK_DestroyDevice,
	NovaLINK_AddDeviceClient,
	NovaLINK_RemoveDeviceClient,
	NovaLINK_PerformDeviceConfigurationChange,
	NovaLINK_AbortDeviceConfigurationChange,
	NovaLINK_HasProperty,
	NovaLINK_IsPropertySettable,
	NovaLINK_GetPropertyDataSize,
	NovaLINK_GetPropertyData,
	NovaLINK_SetPropertyData,
	NovaLINK_StartIO,
	NovaLINK_StopIO,
	NovaLINK_GetZeroTimeStamp,
	NovaLINK_WillDoIOOperation,
	NovaLINK_BeginIOOperation,
	NovaLINK_DoIOOperation,
	NovaLINK_EndIOOperation
};
static AudioServerPlugInDriverInterface*	gAudioServerPlugInDriverInterfacePtr	= &gAudioServerPlugInDriverInterface;
static AudioServerPlugInDriverRef			gAudioServerPlugInDriverRef				= &gAudioServerPlugInDriverInterfacePtr;
static UInt32								gAudioServerPlugInDriverRefCount		= 1;

// TODO: This name is a bit misleading because the devices are actually owned by the plug-in.
static NovaLINK_Object& NovaLINK_LookUpOwnerObject(AudioObjectID inObjectID)
{
    switch(inObjectID)
    {
        case kObjectID_PlugIn:
            return NovaLINK_PlugIn::GetInstance();
            
        case kObjectID_Device:
        case kObjectID_Stream_Input:
        case kObjectID_Stream_Output:
        case kObjectID_Volume_Output_Master:
        case kObjectID_Mute_Output_Master:
            return NovaLINK_Device::GetInstance();

        case kObjectID_Device_UI_Sounds:
        case kObjectID_Stream_Input_UI_Sounds:
        case kObjectID_Stream_Output_UI_Sounds:
        case kObjectID_Volume_Output_Master_UI_Sounds:
            return NovaLINK_Device::GetUISoundsInstance();

        case kObjectID_Device_Null:
        case kObjectID_Stream_Null:
            return NovaLINK_NullDevice::GetInstance();
    }
    
    DebugMsg("NovaLINK_LookUpOwnerObject: unknown object");
    Throw(CAException(kAudioHardwareBadObjectError));
}

static NovaLINK_AbstractDevice& NovaLINK_LookUpDevice(AudioObjectID inObjectID)
{
    switch(inObjectID)
    {
        case kObjectID_Device:
            return NovaLINK_Device::GetInstance();

        case kObjectID_Device_UI_Sounds:
            return NovaLINK_Device::GetUISoundsInstance();

        case kObjectID_Device_Null:
            return NovaLINK_NullDevice::GetInstance();
    }

    DebugMsg("NovaLINK_LookUpDevice: unknown device");
    Throw(CAException(kAudioHardwareBadDeviceError));
}

#pragma mark Factory

extern "C"
void*	NovaLINK_Create(CFAllocatorRef inAllocator, CFUUIDRef inRequestedTypeUUID)
{
	//	This is the CFPlugIn factory function. Its job is to create the implementation for the given
	//	type provided that the type is supported. Because this driver is simple and all its
	//	initialization is handled via static initialization when the bundle is loaded, all that
	//	needs to be done is to return the AudioServerPlugInDriverRef that points to the driver's
	//	interface. A more complicated driver would create any base line objects it needs to satisfy
	//	the IUnknown methods that are used to discover that actual interface to talk to the driver.
	//	The majority of the driver's initialization should be handled in the Initialize() method of
	//	the driver's AudioServerPlugInDriverInterface.
	
	#pragma unused(inAllocator)
    
    void* theAnswer = NULL;
    if(CFEqual(inRequestedTypeUUID, kAudioServerPlugInTypeUUID))
    {
		theAnswer = gAudioServerPlugInDriverRef;
        
        NovaLINK_PlugIn::GetInstance();
    }
    return theAnswer;
}

#pragma mark Inheritence

static HRESULT	NovaLINK_QueryInterface(void* inDriver, REFIID inUUID, LPVOID* outInterface)
{
	//	This function is called by the HAL to get the interface to talk to the plug-in through.
	//	AudioServerPlugIns are required to support the IUnknown interface and the
	//	AudioServerPlugInDriverInterface. As it happens, all interfaces must also provide the
	//	IUnknown interface, so we can always just return the single interface we made with
	//	gAudioServerPlugInDriverInterfacePtr regardless of which one is asked for.

	HRESULT theAnswer = 0;
	CFUUIDRef theRequestedUUID = NULL;
	
	try
	{
		//	validate the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_QueryInterface: bad driver reference");
		ThrowIfNULL(outInterface, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_QueryInterface: no place to store the returned interface");

    	//	make a CFUUIDRef from inUUID
    	theRequestedUUID = CFUUIDCreateFromUUIDBytes(NULL, inUUID);
    	ThrowIf(theRequestedUUID == NULL, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_QueryInterface: failed to create the CFUUIDRef");

		//	AudioServerPlugIns only support two interfaces, IUnknown (which has to be supported by all
		//	CFPlugIns) and AudioServerPlugInDriverInterface (which is the actual interface the HAL will
		//	use).
		ThrowIf(!CFEqual(theRequestedUUID, IUnknownUUID) && !CFEqual(theRequestedUUID, kAudioServerPlugInDriverInterfaceUUID), CAException(E_NOINTERFACE), "NovaLINK_QueryInterface: requested interface is unsupported");
		ThrowIf(gAudioServerPlugInDriverRefCount == UINT32_MAX, CAException(E_NOINTERFACE), "NovaLINK_QueryInterface: the ref count is maxxed out");
		
		//	do the work
		++gAudioServerPlugInDriverRefCount;
		*outInterface = gAudioServerPlugInDriverRef;
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
    
    if(theRequestedUUID != NULL)
    {
    	CFRelease(theRequestedUUID);
    }
		
	return theAnswer;
}

static ULONG	NovaLINK_AddRef(void* inDriver)
{
	//	This call returns the resulting reference count after the increment.
	
	ULONG theAnswer = 0;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "NovaLINK_AddRef: bad driver reference");
	FailIf(gAudioServerPlugInDriverRefCount == UINT32_MAX, Done, "NovaLINK_AddRef: out of references");

	//	increment the refcount
	++gAudioServerPlugInDriverRefCount;
	theAnswer = gAudioServerPlugInDriverRefCount;

Done:
	return theAnswer;
}

static ULONG	NovaLINK_Release(void* inDriver)
{
	//	This call returns the resulting reference count after the decrement.

	ULONG theAnswer = 0;
	
	//	check the arguments
	FailIf(inDriver != gAudioServerPlugInDriverRef, Done, "NovaLINK_Release: bad driver reference");
	FailIf(gAudioServerPlugInDriverRefCount == UINT32_MAX, Done, "NovaLINK_Release: out of references");

	//	decrement the refcount
	//	Note that we don't do anything special if the refcount goes to zero as the HAL
	//	will never fully release a plug-in it opens. We keep managing the refcount so that
	//	the API semantics are correct though.
	--gAudioServerPlugInDriverRefCount;
	theAnswer = gAudioServerPlugInDriverRefCount;

Done:
	return theAnswer;
}

#pragma mark Basic Operations

static OSStatus	NovaLINK_Initialize(AudioServerPlugInDriverRef inDriver, AudioServerPlugInHostRef inHost)
{
	//	The job of this method is, as the name implies, to get the driver initialized. One specific
	//	thing that needs to be done is to store the AudioServerPlugInHostRef so that it can be used
	//	later. Note that when this call returns, the HAL will scan the various lists the driver
	//	maintains (such as the device list) to get the initial set of objects the driver is
	//	publishing. So, there is no need to notifiy the HAL about any objects created as part of the
	//	execution of this method.

	OSStatus theAnswer = 0;
	
	try
	{
		// Check the arguments.
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_Initialize: bad driver reference");
		
		// Store the AudioServerPlugInHostRef.
		NovaLINK_PlugIn::GetInstance().SetHost(inHost);
        
        // Init/activate the devices.
        NovaLINK_Device::GetInstance();
        NovaLINK_Device::GetUISoundsInstance();
        NovaLINK_NullDevice::GetInstance();
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_CreateDevice(AudioServerPlugInDriverRef inDriver, CFDictionaryRef inDescription, const AudioServerPlugInClientInfo* inClientInfo, AudioObjectID* outDeviceObjectID)
{
	//	This method is used to tell a driver that implements the Transport Manager semantics to
	//	create an AudioEndpointDevice from a set of AudioEndpoints. Since this driver is not a
	//	Transport Manager, we just return kAudioHardwareUnsupportedOperationError.
	
	#pragma unused(inDriver, inDescription, inClientInfo, outDeviceObjectID)
	
	return kAudioHardwareUnsupportedOperationError;
}

static OSStatus	NovaLINK_DestroyDevice(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID)
{
	//	This method is used to tell a driver that implements the Transport Manager semantics to
	//	destroy an AudioEndpointDevice. Since this driver is not a Transport Manager, we just check
	//	the arguments and return kAudioHardwareUnsupportedOperationError.
	
	#pragma unused(inDriver, inDeviceObjectID)
	
	return kAudioHardwareUnsupportedOperationError;
}

static OSStatus	NovaLINK_AddDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
	//	This method is used to inform the driver about a new client that is using the given device.
	//	This allows the device to act differently depending on who the client is.
	
	OSStatus theAnswer = 0;
	
	try
	{
		// Check the arguments.
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_AddDeviceClient: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_AddDeviceClient: unknown device");
		
		// Inform the device.
        NovaLINK_LookUpDevice(inDeviceObjectID).AddClient(inClientInfo);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
    catch(NovaLINK_InvalidClientException)
    {
        theAnswer = kAudioHardwareIllegalOperationError;
    }
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_RemoveDeviceClient(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, const AudioServerPlugInClientInfo* inClientInfo)
{
	//	This method is used to inform the driver about a client that is no longer using the given
	//	device.
	
	OSStatus theAnswer = 0;
	
	try
	{
		// Check the arguments.
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_RemoveDeviceClient: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_RemoveDeviceClient: unknown device");
		
        // Inform the device.
        NovaLINK_LookUpDevice(inDeviceObjectID).RemoveClient(inClientInfo);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
    }
    catch(NovaLINK_InvalidClientException)
    {
        theAnswer = kAudioHardwareIllegalOperationError;
    }
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_PerformDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
	//	This method is called to tell the device that it can perform the configuration change that it
	//	had requested via a call to the host method, RequestDeviceConfigurationChange(). The
	//	arguments, inChangeAction and inChangeInfo are the same as what was passed to
	//	RequestDeviceConfigurationChange().
	//
	//	The HAL guarantees that IO will be stopped while this method is in progress. The HAL will
	//	also handle figuring out exactly what changed for the non-control related properties. This
	//	means that the only notifications that would need to be sent here would be for either
	//	custom properties the HAL doesn't know about or for controls.

	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_PerformDeviceConfigurationChange: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_PerformDeviceConfigurationChange: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).PerformConfigChange(inChangeAction, inChangeInfo);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_AbortDeviceConfigurationChange(AudioServerPlugInDriverRef inDriver, AudioObjectID inDeviceObjectID, UInt64 inChangeAction, void* inChangeInfo)
{
	//	This method is called to tell the driver that a request for a config change has been denied.
	//	This provides the driver an opportunity to clean up any state associated with the request.

	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_PerformDeviceConfigurationChange: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_PerformDeviceConfigurationChange: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).AbortConfigChange(inChangeAction, inChangeInfo);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

#pragma mark Property Operations

static Boolean	NovaLINK_HasProperty(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress)
{
	//	This method returns whether or not the given object has the given property.
	
	Boolean theAnswer = false;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_HasProperty: bad driver reference");
		ThrowIfNULL(inAddress, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_HasProperty: no address");
		
		theAnswer = NovaLINK_LookUpOwnerObject(inObjectID).HasProperty(inObjectID, inClientProcessID, *inAddress);
	}
	catch(const CAException& inException)
	{
		theAnswer = false;
	}
	catch(...)
	{
		LogError("NovaLINK_PlugInInterface::NovaLINK_HasProperty: unknown exception. (object: %u, address: %u)",
				 inObjectID,
				 inAddress ? inAddress->mSelector : 0);
		theAnswer = false;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_IsPropertySettable(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, Boolean* outIsSettable)
{
	//	This method returns whether or not the given property on the object can have its value
	//	changed.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_IsPropertySettable: bad driver reference");
		ThrowIfNULL(inAddress, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_IsPropertySettable: no address");
		ThrowIfNULL(outIsSettable, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_IsPropertySettable: no place to put the return value");
        
        NovaLINK_Object& theAudioObject = NovaLINK_LookUpOwnerObject(inObjectID);
		if(theAudioObject.HasProperty(inObjectID, inClientProcessID, *inAddress))
		{
			*outIsSettable = theAudioObject.IsPropertySettable(inObjectID, inClientProcessID, *inAddress);
		}
		else
		{
			theAnswer = kAudioHardwareUnknownPropertyError;
		}
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		LogError("NovaLINK_PlugInInterface::NovaLINK_IsPropertySettable: unknown exception. (object: %u, address: %u)",
				 inObjectID,
				 inAddress ? inAddress->mSelector : 0);
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_GetPropertyDataSize(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32* outDataSize)
{
	//	This method returns the byte size of the property's data.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_GetPropertyDataSize: bad driver reference");
		ThrowIfNULL(inAddress, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_GetPropertyDataSize: no address");
		ThrowIfNULL(outDataSize, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_GetPropertyDataSize: no place to put the return value");
        
        NovaLINK_Object& theAudioObject = NovaLINK_LookUpOwnerObject(inObjectID);
		if(theAudioObject.HasProperty(inObjectID, inClientProcessID, *inAddress))
		{
			*outDataSize = theAudioObject.GetPropertyDataSize(inObjectID, inClientProcessID, *inAddress, inQualifierDataSize, inQualifierData);
		}
		else
		{
			theAnswer = kAudioHardwareUnknownPropertyError;
		}
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		LogError("NovaLINK_PlugInInterface::NovaLINK_GetPropertyDataSize: unknown exception. (object: %u, address: %u)",
				 inObjectID,
				 inAddress ? inAddress->mSelector : 0);
		theAnswer = kAudioHardwareUnspecifiedError;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_GetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, UInt32* outDataSize, void* outData)
{
	//	This method fetches the data for a given property
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_GetPropertyData: bad driver reference");
		ThrowIfNULL(inAddress, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_GetPropertyData: no address");
		ThrowIfNULL(outDataSize, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_GetPropertyData: no place to put the return value size");
		ThrowIfNULL(outData, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_GetPropertyData: no place to put the return value");
		
        NovaLINK_Object& theAudioObject = NovaLINK_LookUpOwnerObject(inObjectID);
		if(theAudioObject.HasProperty(inObjectID, inClientProcessID, *inAddress))
		{
			theAudioObject.GetPropertyData(inObjectID, inClientProcessID, *inAddress, inQualifierDataSize, inQualifierData, inDataSize, *outDataSize, outData);
		}
		else
		{
			theAnswer = kAudioHardwareUnknownPropertyError;
		}
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		LogError("NovaLINK_PlugInInterface::NovaLINK_GetPropertyData: unknown exception. (object: %u, address: %u)",
                 inObjectID,
				 inAddress ? inAddress->mSelector : 0);
		theAnswer = kAudioHardwareUnspecifiedError;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_SetPropertyData(AudioServerPlugInDriverRef inDriver, AudioObjectID inObjectID, pid_t inClientProcessID, const AudioObjectPropertyAddress* inAddress, UInt32 inQualifierDataSize, const void* inQualifierData, UInt32 inDataSize, const void* inData)
{
	//	This method changes the value of the given property

	OSStatus theAnswer = 0;

	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef, CAException(kAudioHardwareBadObjectError), "NovaLINK_SetPropertyData: bad driver reference");
        ThrowIfNULL(inAddress, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_SetPropertyData: no address");
        ThrowIfNULL(inData, CAException(kAudioHardwareIllegalOperationError), "NovaLINK_SetPropertyData: no data");
		
        NovaLINK_Object& theAudioObject = NovaLINK_LookUpOwnerObject(inObjectID);
		if(theAudioObject.HasProperty(inObjectID, inClientProcessID, *inAddress))
		{
			if(theAudioObject.IsPropertySettable(inObjectID, inClientProcessID, *inAddress))
			{
				theAudioObject.SetPropertyData(inObjectID, inClientProcessID, *inAddress, inQualifierDataSize, inQualifierData, inDataSize, inData);
			}
			else
			{
				theAnswer = kAudioHardwareUnsupportedOperationError;
			}
		}
		else
		{
			theAnswer = kAudioHardwareUnknownPropertyError;
		}
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		LogError("NovaLINK_PlugInInterface::NovaLINK_SetPropertyData: unknown exception. (object: %u, address: %u)",
				 inObjectID,
				 inAddress ? inAddress->mSelector : 0);
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

#pragma mark IO Operations

static OSStatus	NovaLINK_StartIO(AudioServerPlugInDriverRef inDriver,
                            AudioObjectID inDeviceObjectID,
                            UInt32 inClientID)
{
	//	This call tells the device that IO is starting for the given client. When this routine
	//	returns, the device's clock is running and it is ready to have data read/written. It is
	//	important to note that multiple clients can have IO running on the device at the same time.
	//	So, work only needs to be done when the first client starts. All subsequent starts simply
	//	increment the counter.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_StartIO: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_StartIO: unknown device");
		
		//	tell the device to do the work
        NovaLINK_LookUpDevice(inDeviceObjectID).StartIO(inClientID);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_StopIO(AudioServerPlugInDriverRef inDriver,
                           AudioObjectID inDeviceObjectID,
                           UInt32 inClientID)
{
	//	This call tells the device that the client has stopped IO. The driver can stop the hardware
	//	once all clients have stopped.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_StopIO: bad driver reference");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_StopIO: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).StopIO(inClientID);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_GetZeroTimeStamp(AudioServerPlugInDriverRef inDriver,
                                     AudioObjectID inDeviceObjectID,
                                     UInt32 inClientID,
                                     Float64* outSampleTime,
                                     UInt64* outHostTime,
                                     UInt64* outSeed)
{
    #pragma unused(inClientID)
	//	This method returns the current zero time stamp for the device. The HAL models the timing of
	//	a device as a series of time stamps that relate the sample time to a host time. The zero
	//	time stamps are spaced such that the sample times are the value of
	//	kAudioDevicePropertyZeroTimeStampPeriod apart. This is often modeled using a ring buffer
	//	where the zero time stamp is updated when wrapping around the ring buffer.

	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_GetZeroTimeStamp: bad driver reference");
		ThrowIfNULL(outSampleTime,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_GetZeroTimeStamp: no place to put the sample time");
		ThrowIfNULL(outHostTime,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_GetZeroTimeStamp: no place to put the host time");
		ThrowIfNULL(outSeed,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_GetZeroTimeStamp: no place to put the seed");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_GetZeroTimeStamp: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).GetZeroTimeStamp(*outSampleTime, *outHostTime, *outSeed);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_WillDoIOOperation(AudioServerPlugInDriverRef inDriver,
                                      AudioObjectID inDeviceObjectID,
                                      UInt32 inClientID,
                                      UInt32 inOperationID,
                                      Boolean* outWillDo,
                                      Boolean* outWillDoInPlace)
{
	#pragma unused(inClientID)
	//	This method returns whether or not the device will do a given IO operation.

	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_WillDoIOOperation: bad driver reference");
		ThrowIfNULL(outWillDo,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_WillDoIOOperation: no place to put the will-do return value");
		ThrowIfNULL(outWillDoInPlace,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_WillDoIOOperation: no place to put the in-place return value");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_WillDoIOOperation: unknown device");
		
		//	tell the device to do the work
		bool willDo = false;
		bool willDoInPlace = false;
		NovaLINK_LookUpDevice(inDeviceObjectID).WillDoIOOperation(inOperationID, willDo, willDoInPlace);
		
		//	set the return values
		*outWillDo = willDo;
		*outWillDoInPlace = willDoInPlace;
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		theAnswer = kAudioHardwareUnspecifiedError;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_BeginIOOperation(AudioServerPlugInDriverRef inDriver,
                                     AudioObjectID inDeviceObjectID,
                                     UInt32 inClientID,
                                     UInt32 inOperationID,
                                     UInt32 inIOBufferFrameSize,
                                     const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
	// This is called at the beginning of an IO operation.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_BeginIOOperation: bad driver reference");
		ThrowIfNULL(inIOCycleInfo,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_BeginIOOperation: no cycle info");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_BeginIOOperation: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).BeginIOOperation(inOperationID,
                                                            inIOBufferFrameSize,
                                                            *inIOCycleInfo,
                                                            inClientID);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		DebugMsg("NovaLINK_PlugInInterface::NovaLINK_BeginIOOperation: unknown exception. (device: %s, operation: %u)",
				 (inDeviceObjectID == kObjectID_Device ? "NovaLINKDevice" : "other"),
				 inOperationID);
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

static OSStatus	NovaLINK_DoIOOperation(AudioServerPlugInDriverRef inDriver,
                                  AudioObjectID inDeviceObjectID,
                                  AudioObjectID inStreamObjectID,
                                  UInt32 inClientID,
                                  UInt32 inOperationID,
                                  UInt32 inIOBufferFrameSize,
                                  const AudioServerPlugInIOCycleInfo* inIOCycleInfo,
                                  void* ioMainBuffer,
                                  void* ioSecondaryBuffer)
{
	//	This is called to actually perform a given IO operation.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_EndIOOperation: bad driver reference");
		ThrowIfNULL(inIOCycleInfo,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_EndIOOperation: no cycle info");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_EndIOOperation: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).DoIOOperation(inStreamObjectID,
                                                         inClientID,
                                                         inOperationID,
                                                         inIOBufferFrameSize,
                                                         *inIOCycleInfo,
                                                         ioMainBuffer,
                                                         ioSecondaryBuffer);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		DebugMsg("NovaLINK_PlugInInterface::NovaLINK_DoIOOperation: unknown exception. (device: %s, operation: %u)",
				 (inDeviceObjectID == kObjectID_Device ? "NovaLINKDevice" : "other"),
				 inOperationID);
		theAnswer = kAudioHardwareUnspecifiedError;
	}

	return theAnswer;
}

static OSStatus	NovaLINK_EndIOOperation(AudioServerPlugInDriverRef inDriver,
                                   AudioObjectID inDeviceObjectID,
                                   UInt32 inClientID,
                                   UInt32 inOperationID,
                                   UInt32 inIOBufferFrameSize,
                                   const AudioServerPlugInIOCycleInfo* inIOCycleInfo)
{
	// This is called at the end of an IO operation.
	
	OSStatus theAnswer = 0;
	
	try
	{
		//	check the arguments
		ThrowIf(inDriver != gAudioServerPlugInDriverRef,
                CAException(kAudioHardwareBadObjectError),
                "NovaLINK_EndIOOperation: bad driver reference");
		ThrowIfNULL(inIOCycleInfo,
                    CAException(kAudioHardwareIllegalOperationError),
                    "NovaLINK_EndIOOperation: no cycle info");
		ThrowIf(inDeviceObjectID != kObjectID_Device && inDeviceObjectID != kObjectID_Device_UI_Sounds && inDeviceObjectID != kObjectID_Device_Null,
                CAException(kAudioHardwareBadDeviceError),
                "NovaLINK_EndIOOperation: unknown device");
		
		//	tell the device to do the work
		NovaLINK_LookUpDevice(inDeviceObjectID).EndIOOperation(inOperationID,
                                                          inIOBufferFrameSize,
                                                          *inIOCycleInfo,
                                                          inClientID);
	}
	catch(const CAException& inException)
	{
		theAnswer = inException.GetError();
	}
	catch(...)
	{
		DebugMsg("NovaLINK_PlugInInterface::NovaLINK_EndIOOperation: unknown exception. (device: %s, operation: %u)",
				 (inDeviceObjectID == kObjectID_Device ? "NovaLINKDevice" : "other"),
				 inOperationID);
		theAnswer = kAudioHardwareUnspecifiedError;
	}
	
	return theAnswer;
}

